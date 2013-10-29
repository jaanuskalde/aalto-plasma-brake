/*
 * util.h
 *
 *  Created on: 01.08.2013
 *      Author: Jaanus
 */
#ifndef UTIL_H_
#define UTIL_H_

//Result of the ADC conversions
//0 is feedback
//1 is neg tether current
//2 is pos tether current
//3 is anode current
//4 is temperature
extern volatile unsigned int ADC_Result[5];

//info about buffers
//0 is length of the packet internals
//1 is command code
//2 is status
//3 is data/checksum
//4 is optional
//5 is optionsl
//6 is optionsl
extern volatile unsigned int input_buffer[7];
extern volatile int input_pointer;
extern volatile unsigned int output_buffer[7];
extern volatile int output_pointer;
enum input_states {NO, RECEIVING, WAITING};
extern volatile enum input_states input_state;


//Define Watchdog Timer configuration, family datasheet, pg300
#define WDTCONFIG ( WDTPW | WDTCNTCL | WDTIS2 )

//buzy wait function, use sparingly
#define DELAY_US(X) __delay_cycles((X)*5)

void init_osc(void);
void feed(void);

void init_adc(void);

void init_i2c(unsigned char address);
void send(unsigned int command);
void send2(unsigned int command, unsigned char status);
void send3(unsigned int command, unsigned char data);
void send4(unsigned int command, unsigned char data, unsigned char data2);
void send5(unsigned int command, unsigned char data, unsigned char data2, unsigned char data3);


void delay_microseconds(unsigned int);

#endif /* UTIL_H_ */
