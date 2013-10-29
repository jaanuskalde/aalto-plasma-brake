#include <msp430.h> 
#include "util.h"

//constants
#define C_TEMP_OFFSET 242
#define C_TEMP_MULTIPLIER 7

////Pins for in board components
#define GATE_P_RESISTOR 0x04	//PORT1
#define GATE_N_RESISTOR 0x08	//PORT1
#define GATE_P_INDUCTOR 0x10	//PORT1
#define GATE_N_INDUCTOR 0x20	//PORT1
#define LOCK_ENDMASS 0x10	//PORT3
#define LOCK_REEL 0x04	//PORT2
#define FB 0x01		//PORT1
#define BOOST 0x01	//PORTJ

//Pins for high voltage board.
#define I_NEG 0x02	//PORT1
#define I_POS 0x04	//PORT1
#define I_ANODE 0x01	//PORT3
#define EMITTER0 0x08	//PORT2
#define EMITTER1 0x80	//PORT2
#define EMITTER2 0x10	//PORT2
#define EMITTER3 0x02	//PORT3
#define NEGHV 0x80	//PORT3
#define POSHV 0x20	//PORT3

//It's the one guy!

//enum for states
enum possible_states {
	STOP	= 0,
	FORWARD	= 1,
	REVERSE	= 2
};

enum possible_states state = STOP;

// I wanna be the guy!
// counter var
volatile unsigned int i;

unsigned int reel_state_count = 0;//how long have we been in this state
unsigned int reel_count = 0;//how many half circles have we reeled
unsigned int to_reel_count = 0;//where to reel
unsigned long hv_count = 0;

unsigned int to_burn = 0;
unsigned int burn_remaining = 0;

void main(void)
{
	//I do small pulse and then wait for this amount of microseconds.
	static const unsigned int lookup_microseconds_wait[] =
	{
			140, 132, 122, 114, 108, 100, 94, 88, 82, 76, 72, 66, 62, 58, 54, 52, 48, 44, 42, 38, 36, 34, 32, 30, 28, 26, 24, 22, 20, 20, 18, 16, 16, 14, 14, 12, 12, 12, 10, 10, 8, 8, 8, 8, 6, 6, 6, 6, 4, 4, 4, 4, 4, 4, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2
	};

	init_osc();

	//////Set inputs and outputs.
	P1DIR = 0x0000;
	P2DIR = 0x0000;
	PJDIR = 0x0000;
	P1OUT = 0x0000;
	P2OUT = 0x0000;
	PJOUT = 0x0000;
	P3OUT = 0x0000;

	//Set HV board pins
	P2DIR |= EMITTER0 | EMITTER1 | EMITTER2;
	P3DIR |= EMITTER3 | NEGHV | POSHV;

	//burners
	P2OUT &= ~LOCK_REEL;
	P3OUT &= ~LOCK_ENDMASS;
	P3DIR |= LOCK_ENDMASS;
	P2DIR |= LOCK_REEL;


	//Set boost converter enable pin to output.
	PJOUT &= ~BOOST;
	PJDIR |= BOOST;

	//Pins assosiated with mosfet gates.
	P1OUT &= ~GATE_P_RESISTOR & ~GATE_P_INDUCTOR;		//Turn off P channels, we want no short circuits.
    P1OUT |= GATE_N_RESISTOR | GATE_N_INDUCTOR;		//Turn off N channels, they have inverters on gate.
	P1DIR |= GATE_P_RESISTOR | GATE_N_RESISTOR | GATE_P_INDUCTOR | GATE_N_INDUCTOR;
	//All Gate controls are now outputs, beware.

	init_adc();
	init_i2c(0x10);

    for (;;)
    {
    	feed();//the watchdog

    	//Main switch, do not pull.
    	switch (state)
		{
			case STOP:
				//Turn off all transistors and boost converter.
				P1OUT &= (~GATE_P_RESISTOR & ~GATE_P_INDUCTOR);	//Turn off P channels
			    P1OUT |= (GATE_N_RESISTOR | GATE_N_INDUCTOR);		//Turn off N channels, they have inverters on gate.
				PJOUT &= 0xfffe;
				delay_microseconds(2000);
				break;
			case FORWARD:
			//case REVERSE:
				//Turn on the boost converter.
				PJOUT |= 0x0001;

				ADC10IE &= ~ADC10IE0; //Disable ADC interrupt
				//Generate the spikes.
				for (i = 0; i < 100; i++)
				{
					//Pull inductor high for 1 microsecond.
					__bic_SR_register(GIE);//interrupts off
					P1OUT |= GATE_P_INDUCTOR;
					__delay_cycles(1);

					//Let it float for x amount of microseconds.
					//X comes form lookup.
					P1OUT &= ~GATE_P_INDUCTOR;
					__bis_SR_register(GIE);//interrupts on
					delay_microseconds(lookup_microseconds_wait[i]);
				}
				ADC10IE |= ADC10IE0; //Enable ADC interrupt

				//Empty the motor through resistor. 60 us should be enough.
				P1OUT &= ~GATE_N_RESISTOR;
				DELAY_US(60);
				P1OUT |= GATE_N_RESISTOR;

				break;
			case REVERSE:
				//Turn on the boost converter.
				PJOUT |= 0x0001;

				//Charge the motor through resistor. 60 us should be enough.
				P1OUT |= GATE_P_RESISTOR;
				DELAY_US(80);
				P1OUT &= ~GATE_P_RESISTOR;


				ADC10IE &= ~ADC10IE0; //Disable ADC interrupt
				//Generate the spikes.
				for (i = 0; i < 100; i++)
				{
					//Pull inductor low for n microsecond.
					//It is different from other one because of the driver.
					__bic_SR_register(GIE);//interrupts off
					P1OUT &= ~GATE_N_INDUCTOR;
					delay_microseconds(4);

					//Let it float for x amount of microseconds.
					//X comes form lookup.
					P1OUT |= GATE_N_INDUCTOR;
					__bis_SR_register(GIE);//interrupts on
					delay_microseconds(lookup_microseconds_wait[i]);
				}
				ADC10IE |= ADC10IE0; //Enable ADC interrupt
				break;
		}

    	//burning logic
    	if (burn_remaining > 0)
		{
    		burn_remaining--;

    		if (to_burn) P3OUT |= LOCK_ENDMASS;
    		else P2OUT |= LOCK_REEL;
		}
		else
		{
			P3OUT &= ~LOCK_ENDMASS;
			P2OUT &= ~LOCK_REEL;
		}

    	//HV connecting
    	if (hv_count)
    	{
    		hv_count--;

    		if (hv_count == 0)//if time is up, shut down the thing
    		{
				P3OUT &= ~NEGHV;
				P3OUT &= ~POSHV;
    		}
    	}

    	//Analog logic and controller
    	if (to_reel_count > reel_count)
    	{
    		state = FORWARD;

    		//if state is same, increase the counter
    		if (((1 & reel_count) && (ADC_Result[0] >= 512)) || (!(1 & reel_count) && (ADC_Result[0] < 512)))
    		{
    			reel_state_count ++;

    			if (reel_state_count > 100)//we have changed permanately
    			{
    				reel_count++;

    				if (reel_count >= to_reel_count)
    				{
    					state = STOP;
    				}
    			}
    		}
    		else//if it is different, reset the counter
    		{
    			reel_state_count = 0;
    		}
    		//TODO analog analyzing for counting the rows
    	}
    	else state = STOP;

    	//command decoder
    	if (input_state == WAITING)//we are not receiving at the moment
    	if (input_pointer)
    	if (input_buffer[0])////we have a packet
    	{
    		unsigned int tmp[7];
    		int tmp_pnt = 0;
			__bic_SR_register(GIE);//interrupts off
			tmp[0] = input_buffer[0];
			tmp[1] = input_buffer[1];
			tmp[2] = input_buffer[2];
			tmp[3] = input_buffer[3];
			tmp[4] = input_buffer[4];
			tmp[5] = input_buffer[5];
			tmp[6] = input_buffer[6];
			tmp_pnt = input_pointer;
			input_buffer[0] = input_buffer[1] = input_buffer[2] = input_buffer[3] = input_buffer[4] = input_buffer[5] = input_buffer[6] = 0;
			input_pointer = 0;
			input_state = NO;
			__bis_SR_register(GIE);//interrupts on

//			if (tmp_pnt-1 == tmp[0])//data len is ok
//			{
				unsigned int checksum = 0;
				for (i = 0; i < tmp_pnt; i++)
				{
					checksum ^= tmp[i];
				}

				if (checksum == 0)//checksum is ok
				{
					///main package decoding begins
					switch (tmp[1])
					{
						case 0x01://STATUS
							send3(tmp[1], state);
							break;
						case 0x04://STANDBY
							state = STOP; //stop reeling
							P3OUT &= ~EMITTER3 & ~NEGHV & ~POSHV; //turn off HV
							P2OUT &= ~EMITTER0 & ~EMITTER1 &  ~EMITTER2; //and all emitters
						//fall through switch
						case 0x02://ON
							send(tmp[1]);
							break;
						case 0x10://Burn reel lock
							to_burn = 0;
							burn_remaining = 100;//TODO - precise value
							send(tmp[1]);
							break;
						case 0x11://Burn endmass lock
							to_burn = 1;
							burn_remaining = 100;//TODO - precise value
							send(tmp[1]);
							break;
						case 0x20://Reel motor
							to_reel_count += tmp[3]*2;
							send(tmp[1]);
							break;
						case 0x21://Get reel count
							send4(tmp[1], (unsigned char)(reel_count >> 9), (unsigned char) (reel_count >> 1));
							break;
						case 0x31://Turn on the electron emitters in configuration x
							if (tmp[3] & 1) P2OUT |= EMITTER0; else P2OUT &= ~EMITTER0;
							if (tmp[3] & 2) P2OUT |= EMITTER1; else P2OUT &= ~EMITTER1;
							if (tmp[3] & 4) P2OUT |= EMITTER2; else P2OUT &= ~EMITTER2;
							if (tmp[3] & 8) P3OUT |= EMITTER3; else P3OUT &= ~EMITTER3;
							send(tmp[1]);
							break;
						case 0x32://Turn on HV converters in configuration x
							if (tmp[3] == 1) //turn on pos HV
							{
								P3OUT &= ~NEGHV;
								P3OUT |= POSHV;
							}
							else if (tmp[3] == 2)//turn on neg HV
							{
								P3OUT &= ~POSHV;
								P3OUT |= NEGHV;
							}
							else//turn both off
							{
								P3OUT &= ~NEGHV;
								P3OUT &= ~POSHV;
							}
							send(tmp[1]);
							break;
						case 0x36://Turn on pos HV converter for x ds
							P3OUT &= ~NEGHV;
							P3OUT |= POSHV;
							hv_count = ((((unsigned long) tmp[3]) << 8) | ((unsigned long) tmp[4])) * 50;
							send(tmp[1]);
							break;
						case 0x37://Turn off pos HV converter for x ds
							P3OUT &= ~POSHV;
							P3OUT |= NEGHV;
							hv_count = ((((unsigned long) tmp[3]) << 8) | ((unsigned long) tmp[4])) * 50;
							send(tmp[1]);
							break;
						case 0x40: //Get currents
							send5(tmp[1], (unsigned char) (ADC_Result[3]>>2),  (unsigned char) (ADC_Result[2]>>2),  (unsigned char) (ADC_Result[1]>>2));
							break;
						case 0x50: //Get processor temp
							i = ADC_Result[4];
							i -= C_TEMP_OFFSET;
							i *= C_TEMP_MULTIPLIER;
							i /= 8;
							send4(tmp[1], (unsigned char)(i >> 1), (unsigned char)(i<<7));
							break;
						default:
							send2(tmp[1], 1); //Send error 1 - Command not recognized
					}
				}
				else send2(tmp[1], 3);
//			}
//			else send2(tmp[1], 2);//send error nr 2 - data len is bad
			_no_operation();
    	}
    }
}
