#include "jdlow.h"

#include <string.h>

//#define LOG(msg, ...) DMESG("JD: " msg, ##__VA_ARGS__)
#define LOG(...) ((void)0)

#define ERROR(msg, ...)                                                                            \
    do {                                                                                           \
        signal_error();                                                                            \
        DMESG("JD-ERROR: " msg, ##__VA_ARGS__);                                                    \
    } while (0)

#define RAM_FUNC __attribute__((noinline, long_call, section(".data")))

#define JD_STATUS_RX_ACTIVE 0x01
#define JD_STATUS_TX_ACTIVE 0x02
#define JD_STATUS_TX_QUEUED 0x04

static jd_frame_t _rxBuffer[2];
static jd_frame_t *rxFrame = &_rxBuffer[0];
static void set_tick_timer(uint8_t statusClear);
static volatile uint8_t status;

static jd_frame_t *txFrame;
static uint64_t nextAnnounce;
static uint8_t txPending;
static uint8_t annCounter;

static jd_diagnostics_t jd_diagnostics;

jd_diagnostics_t *jd_get_diagnostics(void) {
    jd_diagnostics.bus_state = 0; // TODO?
    return &jd_diagnostics;
}

static void pulse1(void) {
    log_pin_set(1, 1);
    log_pin_set(1, 0);
}

static void signal_error(void) {
    log_pin_set(2, 1);
    log_pin_set(2, 0);
}

static void signal_write(int v) {
    log_pin_set(4, v);
}

static void signal_read(int v) {
    // log_pin_set(0, v);
}
static void pulse_log_pin(void) {}

static void check_announce(void) {
    if (tim_get_micros() > nextAnnounce) {
        // pulse_log_pin();
        if (nextAnnounce)
            app_queue_annouce();
        nextAnnounce = tim_get_micros() + 499000 + (jd_random() & 0x7ff);
    }
}

void jd_init(void) {
    DMESG("JD: init");
    tim_init();
    set_tick_timer(0);
    uart_init();
    check_announce();
}

int jd_is_running(void) {
    return nextAnnounce != 0;
}

int jd_is_busy(void) {
    return status != 0;
}

static void tx_done(void) {
    signal_write(0);
    set_tick_timer(JD_STATUS_TX_ACTIVE);
}

void jd_tx_completed(int errCode) {
    LOG("tx done: %d", errCode);
    app_frame_sent(txFrame);
    txFrame = NULL;
    tx_done();
}

static void tick(void) {
    check_announce();
    set_tick_timer(0);
}

static void flush_tx_queue(void) {
    // pulse1();
    if (annCounter++ == 0)
        check_announce();

    LOG("flush %d", status);
    target_disable_irq();
    if (status & (JD_STATUS_RX_ACTIVE | JD_STATUS_TX_ACTIVE)) {
        target_enable_irq();
        return;
    }
    status |= JD_STATUS_TX_ACTIVE;
    target_enable_irq();

    txPending = 0;
    if (!txFrame) {
        txFrame = app_pull_frame();
        if (!txFrame) {
            tx_done();
            return;
        }
    }

    signal_write(1);
    if (uart_start_tx(txFrame, JD_FRAME_SIZE(txFrame)) < 0) {
        // ERROR("race on TX");
        jd_diagnostics.bus_lo_error++;
        tx_done();
        txPending = 1;
        return;
    }

    set_tick_timer(0);
}

static void set_tick_timer(uint8_t statusClear) {
    target_disable_irq();
    if (statusClear) {
        // LOG("st %d @%d", statusClear, status);
        status &= ~statusClear;
    }
    if ((status & JD_STATUS_RX_ACTIVE) == 0) {
        if (txPending && !(status & JD_STATUS_TX_ACTIVE)) {
            pulse1();
            // the JD_WR_OVERHEAD value should be such, that the time from pulse1() above
            // to beginning of low-pulse generated by the current device is exactly 150us
            // (when the line below is uncommented)
            // tim_set_timer(150 - JD_WR_OVERHEAD, flush_tx_queue);
            status |= JD_STATUS_TX_QUEUED;
            tim_set_timer(jd_random_around(150) - JD_WR_OVERHEAD, flush_tx_queue);
        } else {
            status &= ~JD_STATUS_TX_QUEUED;
            tim_set_timer(10000, tick);
        }
    }
    target_enable_irq();
}

static void rx_timeout(void) {
    target_disable_irq();
    jd_diagnostics.bus_timeout_error++;
    ERROR("RX timeout");
    uart_disable();
    signal_read(0);
    set_tick_timer(JD_STATUS_RX_ACTIVE);
    target_enable_irq();
    signal_error();
}

static void setup_rx_timeout(void) {
    uint32_t *p = (uint32_t *)rxFrame;
    if (p[0] == 0 && p[1] == 0)
        rx_timeout(); // didn't get any data after lo-pulse
    // got the size - set timeout for whole packet
    tim_set_timer(JD_FRAME_SIZE(rxFrame) * 12 + 60, rx_timeout);
}

void jd_line_falling() {
    LOG("line fall");
    //log_pin_set(1, 1);
    pulse_log_pin();
    signal_read(1);

    // target_disable_irq();
    // no need to disable IRQ - we're at the highest IRQ level
    if (status & JD_STATUS_RX_ACTIVE)
        jd_panic();
    status |= JD_STATUS_RX_ACTIVE;

    // 1us faster than memset() on SAMD21
    uint32_t *p = (uint32_t *)rxFrame;
    p[0] = 0;
    p[1] = 0;
    p[2] = 0;
    p[3] = 0;

    // otherwise we can enable RX in the middle of LO pulse
    if (uart_wait_high() < 0) {
        // line didn't get high in 1ms or so - bail out
        rx_timeout();
        return;
    }
    // pulse1();
    // target_wait_us(2);

    uart_start_rx(rxFrame, sizeof(*rxFrame));
    //log_pin_set(1, 0);

    tim_set_timer(100, setup_rx_timeout);

    // target_enable_irq();
}

void jd_rx_completed(int dataLeft) {
    if (annCounter++ == 0)
        check_announce();

    LOG("rx cmpl");
    jd_frame_t *frame = rxFrame;

    if (rxFrame == &_rxBuffer[0])
        rxFrame = &_rxBuffer[1];
    else
        rxFrame = &_rxBuffer[0];

    signal_read(0);
    set_tick_timer(JD_STATUS_RX_ACTIVE);

    if (dataLeft < 0) {
        ERROR("rx error: %d", dataLeft);
        jd_diagnostics.bus_uart_error++;
        return;
    }

    uint32_t txSize = sizeof(*frame) - dataLeft;
    uint32_t declaredSize = JD_FRAME_SIZE(frame);
    if (txSize < declaredSize) {
        ERROR("frame too short");
        jd_diagnostics.bus_uart_error++;
        return;
    }
    uint16_t crc = jd_crc16((uint8_t *)frame + 2, declaredSize - 2);
    if (crc != frame->crc) {
        ERROR("crc mismatch");
        jd_diagnostics.bus_uart_error++;
        return;
    }

    jd_diagnostics.packets_received++;

    // pulse1();
    int err = app_handle_frame(frame);

    if (err)
        jd_diagnostics.packets_dropped++;
}

void jd_packet_ready(void) {
    target_disable_irq();
    txPending = 1;
    if (status == 0)
        set_tick_timer(0);
    target_enable_irq();
}
