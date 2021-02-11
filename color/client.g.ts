namespace modules {
    /**
     * Senses RGB colors
     **/
    //% fixedInstances blockGap=8
    export class ColorClient extends jacdac.SensorClient<[number,number,number]> {
        constructor(role: string) {
            super(jacdac.SRV_COLOR, role, "u0.16 u0.16 u0.16");
        }
    
        /**
        * Detected color in the RGB color space.
        */
        //% blockId=jacdaccolor_101_0
        //% group="red" blockSetVariable=myModule
        //% blockCombine block="red" callInDebugger
        get red(): number {
            const values = this.values() as any[];
            return values && values.length > 0 && values[0];
        }

        /**
        * Detected color in the RGB color space.
        */
        //% blockId=jacdaccolor_101_1
        //% group="green" blockSetVariable=myModule
        //% blockCombine block="green" callInDebugger
        get green(): number {
            const values = this.values() as any[];
            return values && values.length > 0 && values[1];
        }

        /**
        * Detected color in the RGB color space.
        */
        //% blockId=jacdaccolor_101_2
        //% group="blue" blockSetVariable=myModule
        //% blockCombine block="blue" callInDebugger
        get blue(): number {
            const values = this.values() as any[];
            return values && values.length > 0 && values[2];
        }

            
    }

    //% fixedInstance whenUsed
    export const color = new ColorClient("color");
}