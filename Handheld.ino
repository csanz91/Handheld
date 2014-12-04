#include "Wire.h"
#include <LiquidCrystal.h>
#include <SD.h>
#define DS1307_ADDRESS 0x68					//RTC I2C address

///////Constants///////
const int time_one=54, time_zero=98;				//Bits time in us
const char sd_period=4, led_period=2;				//Time periods. The interrupt triggers every 200ms
const char bit_light=4, bit_direction=5;			//Position of each function in the byte2
const char i2c_address=3, header_ones=12;

///////Pins///////
const char output_data=3, pin_emerg=2, pin_emerg_led=6, pin_rst_emerg=5, chipSelect = 10;

/*
---Pins for the sdcard, defined at Sd2PinMap.h---
CS pin to digital pin 10;
DI pin to digital pin 11;
D0 pin to digital pin 12;
SLK pin to digital pin 13;

---Pins for the LCD, defined with the function LiquidCrystal---
LCD RS pin to digital pin 0
LCD Enable pin to digital pin 1
LCD D4 pin to digital pin 7
LCD D5 pin to digital pin 4
LCD D6 pin to digital pin 8
LCD D7 pin to digital pin 9
*/

///////Flags///////
volatile char data_flag=0, sdcard_flag=0,lcd_flag=0, led_flag=0;

///////Variables///////
volatile byte byte1, byte2, byte3;
volatile int prescaler_led, prescaler_sd, emerg;
int second, minute, hour, day, month, year, weekday, month_day, state_led, no_card=1;

///////the logging file//////
File logfile;
char filename[] = "sdXX.log";

//Set up the LCD
LiquidCrystal lcd(0, 1, 7, 4, 8, 9);


void setup()
{
	//Start the I2C protocol
	Wire.begin();  //Start as master
	
	//Pin Modes
	pinMode(chipSelect, OUTPUT);
	pinMode(output_data, OUTPUT);
	pinMode(pin_emerg_led, OUTPUT);
	pinMode(pin_emerg, INPUT);
	pinMode(pin_rst_emerg, INPUT);
	
	//Set up the LCD's number of columns and rows:
	lcd.begin(16, 2);
	
	// disable global interrupts
	cli();					 
		
	//Initialize Timer1
	TCCR1A = 0;				 // set entire TCCR1A register to 0
	TCCR1B = 0;				 // same for TCCR1B	
	// set compare match register to desired timer count:
	OCR1A = 3124;			//16Mhz/(1024*5)-1 -> 200ms
	// turn on CTC mode:
	TCCR1B |= (1 << WGM12);
	// Set CS10 and CS12 bits for 1024 prescaler:
	TCCR1B |=  (1 << CS12) | (1 << CS10);
	// enable timer compare interrupt:
	TIMSK1 |= (1 << OCIE1A);

	//Set up the emergency button, with the interrupt number 0
	attachInterrupt(0, int_emerg, FALLING);
	
	// enable global interrupts:
	sei();
	
	//Initialize Timer2 - PWM
	TCCR2A =  _BV(COM2B1) | _BV(WGM21) | _BV(WGM20);
	TCCR2B = _BV(WGM22) | _BV(CS22);
	OCR2A = 49;
	OCR2B = 24;
}


void loop()
{
	//Get the data form the slave and send it using the DCC protocol
	if (data_flag)
	{
		if (!emerg) 
		get_data(i2c_address);
                while (TCNT2!=23 || digitalRead(output_data));  //Let the PWM finish the current wave
                send_signal();
		data_flag=0;
	}
	
	//Flash the emergency led
	if (led_flag & emerg)
	{
		blinking_led();
		led_flag=0;
	}
	
	//Show the data in the LCD
	if (lcd_flag)
	{
		print_lcd();
		lcd_flag=0;
	}
	
	//Write the data to the sdcard
	if (sdcard_flag)
	{
		sdcard();
		sdcard_flag=0;
	}
		
	//Reset the emergency
	if (digitalRead(pin_rst_emerg))
	{
		digitalWrite(pin_emerg_led, LOW); //In case the emergency LED was turned on
		emerg=0;
	}
	
}

void print_lcd()
{
	char buffer[15];
	
	//First line
	lcd.setCursor(0, 0);
	sprintf(buffer,"SPEED %2d LIGHT %1d",byte2&0x0F, bitRead(byte2,bit_light));
	lcd.print(buffer);
	
	//Second line
	lcd.setCursor(0, 1);
	sprintf(buffer,"DIR %1c ",char_dir());
	lcd.print(buffer);
	
	//Blink the "EMERGENCY" word at the same time than the LED
	if (emerg && state_led)
	{
		lcd.print(" EMERGENCY");
	}
	else
	{
		lcd.print("          ");
	}
	
}


char char_dir()   //Return a graphical character depending on the train's direction
{
	if (bitRead(byte2,bit_direction))
	{
		return '>';
	}
	else
	{
		return '<';
	}
}

void get_data(int address)
{
	Wire.requestFrom(address, 3);    // request 3 bytes

	if(Wire.available())
	{
		byte1=Wire.read();
		byte2=Wire.read();
		byte3=Wire.read();
        }else							//If the communication stops, send the emergency signal to the train
	{
		byte2=0b01000001;
		byte3=byte1^byte2;
	}
}

void send_signal()  //Send the signal
{

        TCCR2B = 0;  //Stop the PWM

	//Send the preamble
	send_preamble();
	//Send packet start bit
	send_bit(time_zero);
	//Send address data byte
	send_data_byte(byte1);
	//Send data byte start bit
	send_bit(time_zero);
	//Send instruction data byte
	send_data_byte(byte2);
	//Send data byte start bit
	send_bit(time_zero);
	//Send error detection data byte
	send_data_byte(byte3);
	//Send packet end bit
	send_bit(time_one);

        TCCR2A =  _BV(COM2B1) | _BV(WGM21) | _BV(WGM20);
        TCCR2B = _BV(WGM22) | _BV(CS22);  //Resume the PWM
}


void send_data_byte(byte data_byte)  //Send the signal's bytes
{
	for (int x=7; x>=0; x--)
	{
		if (bitRead(data_byte,x)==0)
		{
			send_bit(time_zero);
		}
		else
		{
			send_bit(time_one);
		}
	}
}


void send_preamble()   //Send the signal's preamble
{
	for (int x=header_ones;x>0;x--)
	{
		send_bit(time_one);
	}
}

void send_bit(int time)  //Generates the signal for a 1 or a 0
{	
	digitalWrite(output_data, LOW);
	delayMicroseconds(time);
	digitalWrite(output_data, HIGH);
	delayMicroseconds(time);
	digitalWrite(output_data, LOW);
}

void blinking_led()
{
	if (!state_led)
	{
		digitalWrite(pin_emerg_led, HIGH);
	}else
	{
		digitalWrite(pin_emerg_led, LOW);
	}
	
	state_led=!state_led;
		
}

void sdcard()
{
	/*As we are not going to detect if the sdcard has been extracted in this case, we have commented the parts that made this possible.
	  As consequence of this, some parts like the initiation of the sdcard in this function lose his sense,
	  however we are going to keep the code just in case we want to use it in the future.
	*/
	
	//if (digitalRead(detect_sd_pin))  //Detect if there is an sdcard
	//{
		getDate();		  //Get the date from the RTC
		
		if (no_card)      //If its the first run or if the sdcard has been extracted, create a new file 
		{
			SD.begin(chipSelect);
			new_file();
			no_card=0;
		}
		
		logfile = SD.open(filename, FILE_WRITE);
		
		if (logfile)			//If the file has been opened correctly
		{
			char buffer[80];
			sprintf(buffer,"%02d/%02d/%02d %02d:%02d:%02d - %5d%10d%6d%8d", month_day,month,year,hour,minute,second,byte2&0x0F,bitRead(byte2,bit_direction),bitRead(byte2,bit_light), byte1);
			logfile.print(buffer);
			
			if (emerg)  //Print the word "EMERGENCY"
			{
				logfile.print("  EMERGENCY");
			}
			
			logfile.println();
			logfile.close();   //Close the file to avoid data loses
		}
		
// 		}else
// 		{
// 			logfile.close();
// 			no_card=1;
// 		}

	}

void getDate(){

	// Reset the register pointer
	Wire.beginTransmission(DS1307_ADDRESS);
	Wire.write(0x00);
	Wire.endTransmission();

	Wire.requestFrom(DS1307_ADDRESS, 7);    //Request 7 bytes
	
	second=bcdToDec(Wire.read());
	minute=bcdToDec(Wire.read());
	hour=bcdToDec(Wire.read() & 0b111111);  //Mask to get only the first 6 bits
	weekday=bcdToDec(Wire.read());
	month_day=bcdToDec(Wire.read());
	month=bcdToDec(Wire.read());
	year=bcdToDec(Wire.read());
	
}

int bcdToDec(byte bcd)
{	
	return (bcd/16*10 + bcd%16);
}

void new_file()
{
	for (int i=0;i<100;i++)  //Create a new file checking the ones of the sdcard to avoid overwrite them
	{
		filename[2]=i/10+'0';
		filename[3]=i%10+'0';
		if (!SD.exists(filename))
		{
			logfile = SD.open(filename, FILE_WRITE);
			break;
		}
	}
	
	logfile.println("DD/MM/YY HH/MM/SS - SPEED DIRECTION LIGHT ADDRESS");    //Write log file's header 
	logfile.close();
	
}

void int_emerg()
{
	if (!digitalRead(pin_emerg))
	{
		byte2=0b01000001;
		byte3=byte1^byte2;
		emerg=1;
	}
}

ISR(TIMER1_COMPA_vect)
{

	data_flag=1;
	lcd_flag=1;

	if (prescaler_led++==led_period)
	{
		led_flag=1;
		prescaler_led=0;
	}
	
	if (prescaler_sd++==sd_period)
	{
		sdcard_flag=1;
		prescaler_sd=0;
	}

}
