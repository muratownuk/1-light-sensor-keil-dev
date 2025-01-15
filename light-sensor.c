/*
    Code: for C8051F005
*/
/*
    includes
*/
#include <c8051F000.h>

/*
    16-bit SFR definitions
*/
sfr16 RCAP2         =   0xCA;           // timer 2 reload address
sfr16 T2            =   0xCC;           // timer 2 counter address
sfr16 TMR3RL        =   0x92;           // timer 3 reload address
sfr16 TMR3          =   0x94;           // timer 3 counter address
sfr16 ADC0          =   0xBE;           // data address of ADC after SAR 

/*
    sbit definitions
*/
// outputs to BCD decoder that controls 7-seg display
sbit decA           =   P1^0;
sbit decB           =   P1^1;
sbit decC           =   P1^2;
sbit decD           =   P1^3;

// 1=PRESSED calibration button (on C8051F005 development board)
sbit S1             =   P1^7; 

// calibration LED and /SYSCLK outputs
sbit sysclk_comp    =   P0^0;           // not using this in program 
sbit calib_LED      =   P0^1;

/*
    function prototypes
*/
void vWatchdog(bit status);
void vOSC_Init(void);
void vPort_Init(void);
void vTimer2_Init(void);
void vTimer3_Init(unsigned int count);
void vTimer3_ISR(void);
void vADC0_Init(void);
void vADC0_ISR(void);
void vADC0_enable(void);
void vADC0_disable(void);
void vInterrupt_enable(void);
void vInterrupt_disable(void);
void vDisplayNumber(unsigned char number);
void vDelay_ms(unsigned int ms);
int iScale(int measurement); 
void vCalibrate(void);

/*
    global constants
*/
#define     OFF             0
#define     ON              1
#define     PRESSED         1
#define     CALIB_MIN       1
#define     CALIB_MAX       2

#define     SYSCLK          16000000                // chip operating freq.
#define     MS_DELAY        1000                    
#define     SAMPLE_DELAY    50                      // delay for ADC sample
#define     SAMPLE_RATE     MS_DELAY*SAMPLE_DELAY   // sample frequency (Hz)
#define     INT_DEC         256                     // integrate/decimate
#define     RELOAD_VAL      -((SYSCLK/12)/MS_DELAY) // RCAP2 reload value
#define     ADC_INT         15                      // ADCINT interrupt vector
#define     TIMER3_INT      14                      // TF3 interrupt vector
#define     MIN_LIGHT       610                     // minimum light intensity
#define     MAX_LIGHT       4095                    // maximum light intensity

/*
    global variables
*/
int Result;                             // ADC0 decimated value
int min, max;                           // min/max light intensities
bit calibrate=OFF;                      // calibration flag

// calibrate state possible values: 0=off, 1=min value and 2=max value.
unsigned char calibrate_state=0;

/*
    main
*/
void main(void){

    static int measured_result;         // measured voltage in mV

    // initial min/max values
    min=MIN_LIGHT;
    max=MAX_LIGHT;

    // initialize C8051F005
    vWatchdog(OFF);                     // disable watchdog timer
    vOSC_Init();                        // initialize oscillator (16MHz)
    vPort_Init();                       // initialize ports used
    vTimer2_Init();                     // initialize timer 2 for delay
    vTimer3_Init(SYSCLK/SAMPLE_RATE);   // initialize timer 3 for ADC
    vADC0_Init();                       // initialize ADC0

    vDisplayNumber(0);                  // display number 0 on 7-seg. (init)
    vADC0_enable();                     // enable ADC0
    vInterrupt_enable();                // enable global interrupts

    while(1){

        vInterrupt_disable();           // disable interrupts to process ADC0

        /*********************************************************************
        * the 12-bit ADC value is averaged across INT_DEC measurements. the  *
        * result is then stored in <Result>, and is right-justified. the     *
        * measured voltage applied to AIN0 is then:                          *
        *                                                                    *
        * measured_result(mV)=Vref*(Result/(2^12-1))                         *
        * measured_result(mV)=Result*(2400/4095)                             *
        *                                                                    *
        * where Vref in mV, Result in bits and 2^12-1 in bits.               *
        *********************************************************************/

        measured_result=Result;         // get raw value (0-4095)

        // scale measurement to 0-9 and display on 7-seg. LED.
        vDisplayNumber(iScale(measured_result));

        // handle min/max light intensity calibration
        if(calibrate==ON)
            calibrate_state++;
        
        if(calibrate_state>0){
            vCalibrate();
            calibrate=OFF;
        } 
        
        vInterrupt_enable();            // enable interrupts for processing
        // delay 50ms before taking another sample
        vDelay_ms(SAMPLE_DELAY); 

    }

}

/*
    routines
*/
/*
    vWatchdog:
    turn watchdog timer ON or OFF.

    parameters: status
        bit status: 
        pass ON(1) or OFF(0) to keep watchdog ON or OFF, respectively.
    return    : none
*/
void vWatchdog(bit status){

    if(status==ON)
        return;                     // watchdog is enabled on power-on
    
    else{
        WDTCN=0xDE;                 // disable watchdog timer
        WDTCN=0xAD;
    }

}

/*
    vOSC_Init:
    use internal oscillator (OSCICN) at 16.0 MHz (SYSCLK) and turn off 
    external oscillator (OSCXCN).

    parameters: none
    return    : none
*/
void vOSC_Init(void){

    OSCXCN&=0x00;                       // turn off external oscillator
    OSCICN|=0x83;                       // CLKSL=0 (internal oscillator @16MHz)

}

/*
    vPort_Init:
    enable crossbar (XBARE) for port outputs and use ports P0.0-3 and P1.0-3 
    as outputs (push-pull) and the higher bits as open drain. Initiallize the 
    P1.0-3 outputs all to low. set P1.4-7 latches high for use as inputs.
    route /SYSCLK to P0.0 to see system frequency on oscilloscope.

    parameters: none
    return    : none

*/
void vPort_Init(void){ 
    
    XBR1=0x80;                          // route /SYSCLK to P0.0
    XBR2=0x40;                          // enable weak pull-up and cross-bar
    PRT0CF=0x0F;                        // enable P0.0-3 as push-pull outputs
    PRT1CF=0x0F;                        // enable P1.0-3 as push-pull outputs
    P0&=~0x0F;                          // turn P0.0-3 outputs low
    P1&=~0x0F;                          // turn P1.0-7 outputs low
    P1|=0xF0;                           // set P1.4-7 latches to 1 for input

}

/*
    vTimer2_Init:
    initialize timer 2 to use it to create millisecond(s) delays. timer 2 in 
    mode 1 with auto-reload. Disable timer 2 interrupts (ET2) as we will use 
    timer 2 flag (TF2) directly. 

    parameters: none
    return    : none

*/
void vTimer2_Init(void){ 

    ET2=0;                              // disable T2 interrupt (EA=0 anyways)
    // make sure T2M=0 in clock control reg, this uses SYSCLK/12 as time base.
    CKCON&=~0x20; 
    // make sure T2CON (timer 2 control reg) is in auto-reload mode and
    // timer 2 is incremented by clock defined by T2M (CKCON.5)
    CPRL2=0; 
    CT2=0; 
    // load the reload value for T2 in RCAP2. timer 2 overflows at 1KHz.
    RCAP2=RELOAD_VAL;
    T2=RCAP2;                           // initial load value for T2 

}

/*
    vTimer3_Init:
    initialize timer 3 to use it to trigger ADC0 conversion start. Auto-reload
    specified by <counts> using SYSCLK as timebase. No interrupt generated.

    parameters: count.
        unsigned int count:
        when timer 3 overflows. counts down from MSB.
    return    : none

*/
void vTimer3_Init(unsigned int count){ 

    // stop timer 3, timer 3 mode (T3M) as SYSCLK, clear timer 3 flag (TF3) 
    TMR3CN=0x02;
    EIE2|=0x01;                         // enable timer 3 (ET3) interrupt    
    TMR3RL=-count;                      // load timer 3 reload value
    TMR3=TMR3RL;                        // initial load value for timer 3

    TMR3CN|=0x04;                       // start timer 3

}

/*
    vTimer3_ISR:
    increments calibrate_state if switch S1 (P1.7) is pressed, which will go
    into calibrate mode.

    parameters: none
    return    : none
*/
void vTimer3_ISR(void) interrupt TIMER3_INT{

    TMR3CN&=~0x80;                      // clear timer 3 overflow (TF3) flag

    if(S1==PRESSED)                     // enter calibration mode
        calibrate=ON;

}

/*
    vADC0_Init:
    configure ADC0 to use timer 3 overflows as conversion start source. 
    generate interrupt on converstion complete and to use right-justified 
    output mode. Enable ADC end of converstion interrupt. leave ADC disabled.

    parameters: none
    return    : none

*/
void vADC0_Init(void){ 

    
    AMX0SL=0x00;                        // select AIN0 as input to ADC0
    ADC0CF|=0x80;                       // SAR Conv. clk = 16 SYSCLK cycles
    ADC0CF&=~0x07;                      // Amp. gain = 1
    // enable internal temp. sensor, bias Vref  and Vref. buffer.
    REF0CN=0x07;
    // ADC0 disabled, ADC track mode normal, ADC conversion on timer 3 
    // overflow, ADC data right-justified.
    ADC0CN=0x04;
    EIE2|=0x02;                         // enable ADC0 conversion interrupt

}

/*
    vADC0_ISR:
    handles interrupt generated by ADC0. we take the ADC0 sample, add it to 
    running total <accumulator> and decrement our local decimation counter
    <int_dec>. when <int_dec> reaches 0, we post the decimated result in
    the global variables <Result>. This ISR is vectored when ADCINT=1 in 
    ADC0CN.5.

    parameters: none
    return    : none

*/
void vADC0_ISR(void) interrupt ADC_INT{ 

    // integrate/decimate counter. post new result when int_dec=0.
    static int int_dec=INT_DEC;
    // <acccumulator> is where we integrate ADC sample.
    static long accumulator=0L;

    ADCINT=0;                           // clear ADC conversion complete flag

    // read ADC0 val. and sum 
    accumulator+=ADC0;                  // this happens int_dec=2^8=256 times

    if(--int_dec==0){                   // post result when int_dec=0
        
        // 8 times right-shift (>>) == accumulator/(2^8)=accumulator/256
        Result=accumulator>>8;

        // reset local variables
        int_dec=INT_DEC;
        accumulator=0L;
        
    }

}

/*
    vADC0_enable:
    enables ADC0.

    parameters: none
    return    : none
*/
void vADC0_enable(void){
    ADCEN=1;
}

/*
    vADC0_disable:
    disables ADC0.

    parameters: none
    return    : none
*/
void vADC0_disable(void){
    ADCEN=0;
}

/*
    vInterrupt_enable:
    enable global interrupts (EA=1).

    parameters: none
    return    : none
*/
void vInterrupt_enable(void){
    EA=1;
}

/*
    vInterrupt_disable:
    disable global interrupts (EA=0).

    parameters: none
    return    : none
*/
void vInterrupt_disable(void){
    EA=0;
}

/*
    vDisplayNumber:
    generate port outputs suitable for BCD-decoder then display the number on 
    the 7-segment LED display.

    parameters: number.
        unsigned char number: 
        number to display on 7-segment display (0-9). 
    return    : none  

*/
void vDisplayNumber(unsigned char number){ 

    switch(number){
        case 0:
            decA=0; decB=0; decC=0; decD=0;
            break;
        case 1:
            decA=1; decB=0; decC=0; decD=0;
            break;
        case 2:
            decA=0; decB=1; decC=0; decD=0;
            break;
        case 3:
            decA=1; decB=1; decC=0; decD=0;
            break;
        case 4:
            decA=0; decB=0; decC=1; decD=0;
            break;
        case 5:
            decA=1; decB=0; decC=1; decD=0;
            break;
        case 6:
            decA=0; decB=1; decC=1; decD=0;
            break;
        case 7:
            decA=1; decB=1; decC=1; decD=0;
            break;
        case 8:
            decA=0; decB=0; decC=0; decD=1;
            break;
        case 9:
            decA=1; decB=0; decC=0; decD=1;
            break;
        default:
            decA=0; decB=0; decC=0; decD=1;
    }

}

/*
    vDelay_ms:
    create milliseconds delay with this function using timer 2.

    parameters: ms.
        unsigned int ms:
        desired number of milliseconds.
    return    : none

*/
void vDelay_ms(unsigned int ms){ 
    
    TR2=1;                              // start timer 2
    while(ms){
        TF2=0;                          // clear timer 2 flag
        while(!TF2)
            ;                           // wait until T2 overflows               
        ms--;                           // decrement millisecond count
    }
    TR2=0;                              // stop timer 2

}

/*
    iScale:
    scales the output of the ADC0 value to a value between 0-9 (7-segment 
    display).

    parameters: measurement
        int measurement:
        raw measured value (0-4095).
    return: 
        int value (scaled) between 0 and 9.

*/
int iScale(int measurement){

    int scaled;                         // scaled value to return
    int measure=measurement; 

    if(measurement>=max)
        measure=max;

    /*************************************************************************
     * scaling equation used:                                                *
     *                                                                       *
     * f(x)=(((b-a)(x-min))/(max-min))+a                                     *
     * where b=9, a=0, x=measure, min and max are known hence:               *
     * f(x)=(9*(measure-min))/(max-min)                                      *
     *                                                                       *
     * since we need accurate result, the math is done in float data type    *
     * and casted back to int.                                               *
     ************************************************************************/

    // scale raw value from 0-4095 to 0-9.
    scaled=(int)(9.0*(measure-min))/(max-min);

    if(scaled<0)
        scaled=0;                       // can't have negative scaled number
    
    return scaled;

}

/*
    vCalibrate:
    calibrates light intensity by adjusting min and max values. triggered by 
    P1.7 push-button on the developement board. calib_LED is turned ON when in 
    this mode.

    parameters: none
    return    : none

*/
void vCalibrate(void){

    calib_LED=ON;                       // indicate calibration mode
    vDisplayNumber(8);

    // timer 3 overflow will poll button, so its better off disabled
    EIE2&=~0x01;                        // disable timer 3 (ET3) interrupt
    if(EA==OFF)
        vInterrupt_enable();            // we only want ADC0 samples 
    
    vDelay_ms(1000);                    // wait 1 second

    if(calibrate_state==CALIB_MIN){     // calibrate min

        vDisplayNumber(0);              // 0 indicates min value acceptance
        while(!S1)
            ;                           // wait for button (P1.7) press
        vDelay_ms(500);
        min=Result;
        calibrate_state++;
        
    }

    vDelay_ms(1000);                    // wait 1 second
    
    if(calibrate_state==CALIB_MAX){     // calibrate max

        vDisplayNumber(9);              // 9 indicates max value acceptance
        while(!S1)
            ;                           // wait for button (P1.7) press
        vDelay_ms(500);
        max=Result;
        calibrate_state=0;
        
    }

    if(calibrate_state>CALIB_MAX)       // make sure always [0,2]
        calibrate_state=0;

    vDelay_ms(1000);                    // wait 1 second

    vInterrupt_disable();
    EIE2|=0x01;                         // enable timer 3 (ET3) interrupt

    calib_LED=OFF;

}































