/* --COPYRIGHT--,BSD
 * Copyright (c) 2017, Texas Instruments Incorporated
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * --/COPYRIGHT--*/
/******************************************************************************
 * MSP432 UART - PC Echo with 12MHz BRCLK
 *
 * Description: This demo echoes back characters received via a PC serial port.
 * SMCLK/DCO is used as a clock source and the device is put in LPM0
 * The auto-clock enable feature is used by the eUSCI and SMCLK is turned off
 * when the UART is idle and turned on when a receive edge is detected.
 * Note that level shifter hardware is needed to shift between RS232 and MSP
 * voltage levels.
 *
 *               MSP432P401
 *             -----------------
 *            |                 |
 *            |                 |
 *            |                 |
 *       RST -|     P1.3/UCA0TXD|----> PC (echo)
 *            |                 |
 *            |                 |
 *            |     P1.2/UCA0RXD|<---- PC
 *            |                 |
 *
 *
 *
 * This program will take a set of time values as an input over serial,
 * perform FFT on the array and return the magnitude of the frequency
 * spectrum over serial
 *
 * All values are encoded as Q12 fixed point values
 *
 * This will demonstrate correct FFT computation using QMath library
 *
 *
 *******************************************************************************/
/* DriverLib Includes */
#include <ti/devices/msp432p4xx/driverlib/driverlib.h>
#include <ti/devices/msp432p4xx/inc/msp432.h>

/* Standard Includes */
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

//![Simple UART Config]
/* UART Configuration Parameter. These are the configuration parameters to
 * make the eUSCI A UART module to operate with a 9600 baud rate. These
 * values were calculated using the online calculator that TI provides
 * at:
 *http://software-dl.ti.com/msp430/msp430_public_sw/mcu/msp430/MSP430BaudRateConverter/index.html
 */
const eUSCI_UART_Config uartConfig =
{
        EUSCI_A_UART_CLOCKSOURCE_SMCLK,          // SMCLK Clock Source
        78,                                     // BRDIV = 78
        2,                                       // UCxBRF = 2
        0,                                       // UCxBRS = 0
        EUSCI_A_UART_NO_PARITY,                  // No Parity
        EUSCI_A_UART_LSB_FIRST,                  // LSB First
        EUSCI_A_UART_ONE_STOP_BIT,               // One stop bit
        EUSCI_A_UART_MODE,                       // UART mode
        EUSCI_A_UART_OVERSAMPLING_BAUDRATE_GENERATION  // Oversampling
};
//![Simple UART Config]

 /* Select the global Q value */
 #define GLOBAL_Q    12

/* Select FIXED point option for kissFFT */
#define FIXED_POINT 16

#include "kissFFT/kiss_fftr.h"
#include "kissFFT/kiss_fft_guts.h"

 /* Include the iqmathlib header files */
 #include <ti/iqmathlib/QmathLib.h>
 #include <ti/iqmathlib/IQmathLib.h>

 /* Specify the sample size and sample frequency. */
 #define SAMPLES         1024          // power of 2 no larger than 256
 #define SAMPLE_FREQ     8192            // no larger than 16384

volatile int bytes = 0;
volatile char msg;
char *information_bytes;

int main(void)
    {
    /* Halting WDT  */
    MAP_WDT_A_holdTimer();

    /* Selecting P1.2 and P1.3 in UART mode */
    MAP_GPIO_setAsPeripheralModuleFunctionInputPin(GPIO_PORT_P1,
            GPIO_PIN2 | GPIO_PIN3, GPIO_PRIMARY_MODULE_FUNCTION);

    /* Setting DCO to 12MHz */
    CS_setDCOCenteredFrequency(CS_DCO_FREQUENCY_12);

    //![Simple UART Example]
    /* Configuring UART Module */
    MAP_UART_initModule(EUSCI_A0_BASE, &uartConfig);

    /* Enable UART module */
    MAP_UART_enableModule(EUSCI_A0_BASE);

    /* Enabling interrupts */
    MAP_UART_enableInterrupt(EUSCI_A0_BASE, EUSCI_A_UART_RECEIVE_INTERRUPT);
    MAP_Interrupt_enableInterrupt(INT_EUSCIA0);
    //MAP_Interrupt_enableSleepOnIsrExit();
    MAP_Interrupt_enableMaster();   
    //![Simple UART Example]

    volatile uint32_t i;

    // Stop watchdog timer
    WDT_A_hold(WDT_A_BASE);

    const int rcvMessageSize = SAMPLES;//Size of received message array
    const int sndMessageSize = SAMPLES/2;//Size of sending message array

    kiss_fft_scalar  in[SAMPLES];
    kiss_fft_cpx  out[SAMPLES];
    kiss_fftr_cfg  kiss_fftr_state;
    kiss_fftr_state = kiss_fftr_alloc(SAMPLES,0,0,0);

    information_bytes = (char*)&in;

    while(1)
    {
        int16_t i;

        /* Disable WDT. */
        WDT_A->CTL = WDT_A_CTL_PW | WDT_A_CTL_HOLD;

        if (bytes == 2*rcvMessageSize)
        {

            kiss_fftr(kiss_fftr_state,in,out);

            /* Calculate the magnitude and phase angle of the results. */
            for (i = 0; i < SAMPLES/2; i++) {
                in[i] = _Qmag(out[i].r, out[i].i);
            }

            //Transmit
            int sendMsgCount;
            for (sendMsgCount = 0; sendMsgCount < sndMessageSize; sendMsgCount++)
            {
                UART_transmitData(EUSCI_A0_BASE, (in[sendMsgCount]%256));
                UART_transmitData(EUSCI_A0_BASE, (in[sendMsgCount]/256));
                //UART_transmitData(EUSCI_A0_BASE, '\n');
            }

            bytes = 0;

            i = 1;

        }

    }
}

void EUSCIA0_IRQHandler(void)
{
   if (UCA0IFG & UCRXIFG)
    {
        while(!(UCA0IFG&UCTXIFG));//Busy wait

        msg = UCA0RXBUF;
        information_bytes[bytes] = msg;
        bytes++;
    }
}
