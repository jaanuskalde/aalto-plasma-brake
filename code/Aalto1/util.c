#include <msp430.h>
#include "util.h"

volatile unsigned int ADC_Result[] = {0,0,0,0,0};
volatile unsigned int input_buffer[7];
volatile int input_pointer = 0;
volatile unsigned int output_buffer[7];
volatile int output_pointer = -1;
volatile enum input_states input_state = NO;

void init_osc(void)
{
	/// configure oscillator, family datasheet pg. 67
	// XT1 Setup
	PJSEL0 |= BIT4 + BIT5;

	CSCTL0_H = 0xA5; //password
	CSCTL5 &= ~XT1OFFG & ~ENSTFCNT1;
	SFRIFG1 &= ~OFIFG;
//	CSCTL5 |= ENSTFCNT1;
	CSCTL1 |= DCOFSEL0 + DCOFSEL1;      // Set max. DCO setting
	CSCTL4 = XT2OFF + XTS + XT1DRIVE_0;	// xt2 off + high speed mode + medium drive strength + xt1 on

	do
	{
		CSCTL0_H = 0xA5; //password
		CSCTL5 &= ~XT1OFFG & ~ENSTFCNT1;
		SFRIFG1 &= ~OFIFG;
	}
	while (SFRIFG1&OFIFG); // Test oscillator fault flag

	CSCTL2 = SELA_0 + SELS_0 + SELM_0;  // set ACLK = XT1, SMCLK = XT1, MCLK = XT1
	CSCTL3 = DIVA_0 + DIVS_0 + DIVM_0;  // set all dividers to 0


	//////Start watchdog timer.
	WDTCTL = WDTCONFIG;
	_bis_SR_register(GIE);       // Global Interrupt Enable, needed so WTD could reset the CPU
}

void feed(void)
{
	//Feed the watchdog.
	WDTCTL = WDTCONFIG;
}

void init_adc(void)
{
	  // Configure ADC
	  P1SEL1 |= BIT2 + BIT1 + BIT0;
	  P1SEL0 |= BIT2 + BIT1 + BIT0;
	  P3SEL1 |= BIT3;
	  P3SEL0 |= BIT3;

	  ADC10CTL0 |= ADC10SHT_2 + ADC10ON;        // ADC10ON, S&H=16 ADC clks
	  ADC10CTL1 |= ADC10SHP;                    // ADCCLK = MODOSC; sampling timer
	  ADC10CTL2 |= ADC10RES;                    // 10-bit conversion results
	  ADC10MCTL0 |= ADC10INCH_0;                // A1 ADC input select; Vref=AVCC
	  ADC10IE |= ADC10IE0;                      // Enable ADC conv complete interrupt
	  //maybe delay in between
	  ADC10CTL0 |= ADC10ENC + ADC10SC;        // Sampling and conversion start

	  //we need the interrupt
	  __bis_SR_register(GIE);
}

//microsecond delay function. idea from Arduino's wiring.c
void delay_microseconds(unsigned int us)
{
	//for delays smaller than 2 return yesterday
	if (--us == 0) return;
	if (--us == 0) return;

	us >>= 1;

	for (;us;us--)
	{
		_delay_cycles(7);
	}
}

inline void ADC_int(void)
{
	ADC10CTL0 &= ~ADC10ENC;
	switch(ADC10MCTL0)
	{
		case ADC10INCH_0:
			ADC_Result[0] = ADC10MEM0;
			ADC10MCTL0 = ADC10INCH_1;
			break;
		case ADC10INCH_1:
			ADC_Result[1] = ADC10MEM0;
			ADC10MCTL0 = ADC10INCH_2;
			break;
		case ADC10INCH_2:
			ADC_Result[2] = ADC10MEM0;
			ADC10MCTL0 = ADC10INCH_12;
			break;
		case ADC10INCH_12:
			ADC_Result[3] = ADC10MEM0;
			ADC10MCTL0 = ADC10INCH_10;
			break;
		case ADC10INCH_10:
			ADC_Result[4] = ADC10MEM0;
			ADC10MCTL0 = ADC10INCH_0;
			break;
	}

	ADC10CTL0 |= ADC10ENC + ADC10SC;        // Sampling and conversion start
}

// ADC10 interrupt service routine
#pragma vector=ADC10_VECTOR
__interrupt void ADC10_ISR(void)
{
  switch(__even_in_range(ADC10IV,12))
  {
    case  0: break;                          // No interrupt
    case  2: break;                          // conversion result overflow
    case  4: break;                          // conversion time overflow
    case  6: break;                          // ADC10HI
    case  8: break;                          // ADC10LO
    case 10: break;                          // ADC10IN
    case 12: ADC_int(); break;
    default: break;
  }
}

void init_i2c(unsigned char address)
{
    // Configure Pins for I2C
    P1SEL1 |= BIT6 + BIT7;                  // Pin init
    // eUSCI configuration
    UCB0CTLW0 |= UCSWRST ;	            //Software reset enabled
    UCB0CTLW0 |= UCMODE_3  + UCSYNC;	    //I2C mode, sync mode
    UCB0I2COA0 = address + UCOAEN;   	    //own address + enable
    UCB0CTLW0 &=~UCSWRST;	            //clear reset register
    UCB0IE |=  UCRXIE0 | UCTXIE0 | UCSTPIE | UCSTTIE;	//receive interrupt enable

    __bis_SR_register(GIE);        // Enable interrupts
}

void send(unsigned int command){ send2 (command, 0);}

void send2(unsigned int command, unsigned char status)
{
	output_buffer[0] = 2;
	output_buffer[1] = command;
	output_buffer[2] = status;
	output_buffer[3] = 2 ^ command ^ status;

		output_pointer = 1;
		UCB0TXBUF = output_buffer[0];

}

void send3(unsigned int command, unsigned char data)
{
	output_buffer[0] = 3;
	output_buffer[1] = command;
	output_buffer[2] = 0;
	output_buffer[3] = data;
	output_buffer[4] = 3 ^ command ^ data;

		output_pointer = 1;
		UCB0TXBUF = output_buffer[0];

}

void send4(unsigned int command, unsigned char data, unsigned char data2)
{
	output_buffer[0] = 4;
	output_buffer[1] = command;
	output_buffer[2] = 0;
	output_buffer[3] = data;
	output_buffer[4] = data2;
	output_buffer[5] = 4 ^ command ^ data ^ data2;

		output_pointer = 1;
		UCB0TXBUF = output_buffer[0];

}

void send5(unsigned int command, unsigned char data, unsigned char data2, unsigned char data3)
{
	output_buffer[0] = 5;
	output_buffer[1] = command;
	output_buffer[2] = 0;
	output_buffer[3] = data;
	output_buffer[4] = data2;
	output_buffer[5] = data3;
	output_buffer[6] = 5 ^ command ^ data ^ data2 ^ data3;

		output_pointer = 1;
		UCB0TXBUF = output_buffer[0];

}



#pragma vector = USCI_B0_VECTOR
__interrupt void USCIB0_ISR(void)
{
   switch(__even_in_range(UCB0IV,0x1E))
    {
      case 0x00: break;                     // Vector 0: No interrupts break;
      case 0x02: break;                     // Vector 2: ALIFG break;
      case 0x04: break;                     // Vector 4: NACKIFG break;
      case 0x06:                     // Vector 6: STTIFG break;
    	  //I2C start interrupt
    	  if ((UCB0CTLW0 & UCTR) == 0)
    	  {
    		  input_pointer = 0;
    		  input_state = RECEIVING;
    	  }
    	  break;
      case 0x08:                     // Vector 8: STPIFG break;
    	  output_pointer = -1;
    	  if (input_state == RECEIVING) input_state = WAITING;
    	  UCB0IFG &= ~UCSTPIFG;// Clear stop condition int flag
    	  break;
      case 0x0a: break;                     // Vector 10: RXIFG3 break;
      case 0x0c: break;                     // Vector 14: TXIFG3 break;
      case 0x0e: break;                     // Vector 16: RXIFG2 break;
      case 0x10: break;                     // Vector 18: TXIFG2 break;
      case 0x12: break;                     // Vector 20: RXIFG1 break;
      case 0x14: break;                     // Vector 22: TXIFG1 break;
      case 0x16:
    	  input_buffer[input_pointer] = UCB0RXBUF;                 // Get RX data
    	  input_pointer ++;
    	  if (input_pointer > 6) input_pointer = 6;
        break;                              // Vector 24: RXIFG0 break;
      case 0x18:                     // Vector 26: TXIFG0 break;
    	  if (output_pointer >= 0)//Transmit
    	  {
			  UCB0TXBUF = output_buffer[output_pointer];
			  output_pointer ++;
			  if (output_pointer > 6) output_pointer = 6;
    	  }
    	  else if (input_pointer == 0) UCB0TXBUF = 0; //If they ask and I will never have then they will get 0;
    	  break;
      case 0x1a: break;                     // Vector 28: BCNTIFG break;
      case 0x1c: break;                     // Vector 30: clock low timeout break;
      case 0x1e: break;                     // Vector 32: 9th bit break;
      default: break;
    }
}
