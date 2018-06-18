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
 #define SAMPLES         128          // power of 2 no larger than 256
 #define SAMPLE_FREQ     8192            // no larger than 16384

 /* Access the real and imaginary parts of an index into a complex array. */
 #define RE(x)           (((x)<<1)+0)    // access real part of index
 #define IM(x)           (((x)<<1)+1)    // access imaginary part of index

#define POINTER_CALC(x) ((x & 0xFFFE)<<1) + (x & 0x01) //Pointer address to input data into real bytes of input

 /*
  * Input and result buffers. These can be viewed in memory or printed by
  * defining ALLOW_PRINTF.
  */
 _q qInput[SAMPLES*2];                   // Input buffer of complex values
 //_q qMag[SAMPLES/2];                     // Magnitude of each frequency result


 /* Misc. definitions. */
 #define PI      3.1415926536


 extern void cFFT(_q *input, int16_t n);

//RGB select
//volatile char color = 0;
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

    //Point pointer to start of qInput array
    information_bytes = (char*)&qInput;

    //Initialise values array
    for (i = 0; i < SAMPLES; i++) {
        qInput[RE(i)] = 0;
        qInput[IM(i)] = 0;
    }

    size_t buflen = sizeof(kiss_fft_cpx)*SAMPLES;

    kiss_fft_scalar  *in = (kiss_fft_scalar*)malloc(buflen/2);
    kiss_fft_cpx  *out = (kiss_fft_cpx*)malloc(buflen);
    kiss_fftr_cfg  kiss_fftr_state;
    kiss_fftr_state = kiss_fftr_alloc(SAMPLES,0,0,0);

    while(1)
    {
        int16_t i;

        /* Disable WDT. */
        WDT_A->CTL = WDT_A_CTL_PW | WDT_A_CTL_HOLD;

        if (bytes == 2*rcvMessageSize)
        {
            /*
             * Perform a complex FFT on the input samples. The result is calculated
             * in-place and will be stored in the input buffer.
             */
            //cFFT(qInput, SAMPLES);


            for (i=0; i<SAMPLES; i++)
            {
                in[i] = qInput[RE(i)];
            }

            kiss_fftr(kiss_fftr_state,in,out);

            /* Calculate the magnitude and phase angle of the results. */
            for (i = 0; i < SAMPLES/2; i++) {
                //qMag[i] = _Qmag(qInput[RE(i)], qInput[IM(i)]);
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

            //Reset values array
            for (i = 0; i < SAMPLES; i++) {
                qInput[RE(i)] = 0;
                qInput[IM(i)] = 0;
            }

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
        information_bytes[POINTER_CALC(bytes)] = msg;
        bytes++;
    }
}

extern void cBitReverse3(_q *input, int16_t n);

/*
 * Perform in-place radix-2 DFT of the input signal with size n.
 *
 * This function has been written for any input size up to 16 bits. This function
 * can be optimized by using lookup tables with precomputed twiddle factors for
 * a fixed sized FFT, using Q15 format for the twiddle factors and inlining the
 * multiplication steps with direct access to the MPY32 hardware peripheral.
 */
void cFFT(_q *input, int16_t n)
{
    uint16_t s, s_2;                     // step
    uint16_t i, j;                      // loop counters
    _q qTAngle;                         // twiddle factor angle
    _q qTIncrement;                     // twiddle factor increment
    _q qTCos, qTSin;                    // complex components of twiddle factor
    _q qTempR, qTempI;                  // temp result complex pair

    /* Bit reverse the order of the inputs. */
    cBitReverse3(input, n);

    /* Set step to 2 and initialize twiddle angle increment. */
    s = 2;
    s_2 = 1;
    qTIncrement = _Q(-2*PI);

    while (s <= n) {
        /* Reset twiddle angle and halve increment factor. */
        qTAngle = 0;
        qTIncrement = _Qdiv2(qTIncrement);

        for (i = 0; i < s_2; i++) {
            /* Calculate twiddle factor complex components. */
            qTCos = _Qcos(qTAngle);
            qTSin = _Qsin(qTAngle);
            qTAngle += qTIncrement;

            for (j = i; j < n; j += s) {
                /* Multiply complex pairs and scale each stage. */
                if (((j+s_2) == 112) | (j == 112))
                {
                    int problem = 1;
                }
                qTempR = _Qmpy(qTCos, input[RE(j+s_2)]) - _Qmpy(qTSin, input[IM(j+s_2)]);
                qTempI = _Qmpy(qTSin, input[RE(j+s_2)]) + _Qmpy(qTCos, input[IM(j+s_2)]);
                input[RE(j+s_2)] = _Qdiv2(input[RE(j)] - qTempR);
                input[IM(j+s_2)] = _Qdiv2(input[IM(j)] - qTempI);
                input[RE(j)] = _Qdiv2(input[RE(j)] + qTempR);
                input[IM(j)] = _Qdiv2(input[IM(j)] + qTempI);
            }
        }
        /* Multiply step by 2. */
        s_2 = s;
        s = _Qmpy2(s);
    }
}

/*
 * Perform an in-place bit reversal of the complex input array with size n.
 * Use a look up table to speed up the process. Valid to 16 bits.
 */

void cBitReverse3(_q *input, int16_t n)
{
    uint16_t i, j;                      // loop counters
    uint16_t i16BitRev;                  // index bit reversal
    _q qTemp;

    extern const uint8_t ui8BitRevLUT[256];

    /* In-place bit-reversal. */
    for (i = 0; i < n; i++) {

        //Split 16 bit address into 2 bytes and reverse them
        uint8_t loiBitRev = ui8BitRevLUT[(i & 0x00FF)];
        uint8_t hiiBitRev = ui8BitRevLUT[((i & 0xFF00)>>8)];

        //Add reversed bytes into new address in reverse order
        i16BitRev = (loiBitRev << 8) + hiiBitRev;

        //Shift new address the appropriate number of bits to match length of samples
        for (j = (1<<15); j >= n; j >>= 1) {
            i16BitRev >>= 1;
        }

        //Only swap elements which have not been swapped already
        if (i < i16BitRev) {
            /* Swap inputs. */
            qTemp = input[RE(i)];
            input[RE(i)] = input[RE(i16BitRev)];
            input[RE(i16BitRev)] = qTemp;
            qTemp = input[IM(i)];
            input[IM(i)] = input[IM(i16BitRev)];
            input[IM(i16BitRev)] = qTemp;
        }
    }
}

/* 8-bit reversal lookup table. */
const uint8_t ui8BitRevLUT[256] = {
    0x00, 0x80, 0x40, 0xC0, 0x20, 0xA0, 0x60, 0xE0, 0x10, 0x90, 0x50, 0xD0, 0x30, 0xB0, 0x70, 0xF0,
    0x08, 0x88, 0x48, 0xC8, 0x28, 0xA8, 0x68, 0xE8, 0x18, 0x98, 0x58, 0xD8, 0x38, 0xB8, 0x78, 0xF8,
    0x04, 0x84, 0x44, 0xC4, 0x24, 0xA4, 0x64, 0xE4, 0x14, 0x94, 0x54, 0xD4, 0x34, 0xB4, 0x74, 0xF4,
    0x0C, 0x8C, 0x4C, 0xCC, 0x2C, 0xAC, 0x6C, 0xEC, 0x1C, 0x9C, 0x5C, 0xDC, 0x3C, 0xBC, 0x7C, 0xFC,
    0x02, 0x82, 0x42, 0xC2, 0x22, 0xA2, 0x62, 0xE2, 0x12, 0x92, 0x52, 0xD2, 0x32, 0xB2, 0x72, 0xF2,
    0x0A, 0x8A, 0x4A, 0xCA, 0x2A, 0xAA, 0x6A, 0xEA, 0x1A, 0x9A, 0x5A, 0xDA, 0x3A, 0xBA, 0x7A, 0xFA,
    0x06, 0x86, 0x46, 0xC6, 0x26, 0xA6, 0x66, 0xE6, 0x16, 0x96, 0x56, 0xD6, 0x36, 0xB6, 0x76, 0xF6,
    0x0E, 0x8E, 0x4E, 0xCE, 0x2E, 0xAE, 0x6E, 0xEE, 0x1E, 0x9E, 0x5E, 0xDE, 0x3E, 0xBE, 0x7E, 0xFE,
    0x01, 0x81, 0x41, 0xC1, 0x21, 0xA1, 0x61, 0xE1, 0x11, 0x91, 0x51, 0xD1, 0x31, 0xB1, 0x71, 0xF1,
    0x09, 0x89, 0x49, 0xC9, 0x29, 0xA9, 0x69, 0xE9, 0x19, 0x99, 0x59, 0xD9, 0x39, 0xB9, 0x79, 0xF9,
    0x05, 0x85, 0x45, 0xC5, 0x25, 0xA5, 0x65, 0xE5, 0x15, 0x95, 0x55, 0xD5, 0x35, 0xB5, 0x75, 0xF5,
    0x0D, 0x8D, 0x4D, 0xCD, 0x2D, 0xAD, 0x6D, 0xED, 0x1D, 0x9D, 0x5D, 0xDD, 0x3D, 0xBD, 0x7D, 0xFD,
    0x03, 0x83, 0x43, 0xC3, 0x23, 0xA3, 0x63, 0xE3, 0x13, 0x93, 0x53, 0xD3, 0x33, 0xB3, 0x73, 0xF3,
    0x0B, 0x8B, 0x4B, 0xCB, 0x2B, 0xAB, 0x6B, 0xEB, 0x1B, 0x9B, 0x5B, 0xDB, 0x3B, 0xBB, 0x7B, 0xFB,
    0x07, 0x87, 0x47, 0xC7, 0x27, 0xA7, 0x67, 0xE7, 0x17, 0x97, 0x57, 0xD7, 0x37, 0xB7, 0x77, 0xF7,
    0x0F, 0x8F, 0x4F, 0xCF, 0x2F, 0xAF, 0x6F, 0xEF, 0x1F, 0x9F, 0x5F, 0xDF, 0x3F, 0xBF, 0x7F, 0xFF
};
