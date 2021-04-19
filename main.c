 /************************************************************************/
 /* INCLUDED LIBRARIES/HEADER FILES                                      */
 /************************************************************************/
#include <avr/io.h>
#include <avr/interrupt.h>

#include <stdio.h>

#include "serial.h"
#include "timer.h"
#include "wave.h"
#include "buffer.h"
#include "adc.h"
#include "lib/fatfs/diskio.h"
/************************************************************************/
/* ENUM DEFINITIONS                                                     */
/************************************************************************/
enum {
	DVR_STOPPED,
	DVR_RECORDING,
	DVR_PLAYING
};

/************************************************************************/
/* GLOBAL VARIABLES                                                     */
/************************************************************************/
volatile uint16_t countpage = 0;
uint16_t pageCount = 0;	// Page counter - used to terminate recording
uint16_t newPage = 0;	// Flag that indicates a new page is available for read/write
uint8_t stop = 0;		// Flag that indicates playback/recording is complete
volatile uint8_t overflow_counter = 0;
volatile uint8_t overflow_reset  = 2;

/************************************************************************/
/* FUNCTION PROTOTYPES                                                  */
/************************************************************************/
void pageFull();
void pageEmpty();

/************************************************************************/
/* INITIALISATION FUNCTIONS                                             */
/************************************************************************/

// Initialise PLL (required by USB serial interface, PWM)
void pll_init() {
	PLLFRQ = 0x6A; // PLL = 96 MHz, USB = 48 MHz, TIM4 = 64 MHz
}

// Configure system clock for 16 MHz
void clock_init() {
	CLKPR = 0x80;	// Prektcaler change enable
	CLKPR = 0x00;	// Prescaler /1, 16 MHz
}



// Initialise DVR subsystems and enable interrupts
void init() {
	cli();			// Disable interrupts
	clock_init();	// Configure clocks
	pll_init();     // Configure PLL (used by Timer4 and USB serial)
	serial_init();	// Initialise USB serial interface (debug)
	timer_init();	// Initialise timer (used by FatFs library)
	buffer_init(pageFull, pageEmpty);  // Initialise circular buffer (must specify callback functions)
	adc_init();		// Initialise ADC
	//userio_init();  // Initialise LEDs
	sei();			// Enable interrupts
	
DDRF &= 0b10001111;    // Pushbuttons 1 to 3 - PORTF 6-4 as inputs
DDRD |= 0b11110000;		// Set PORTD 7-4 as outputs (LEDs)
	
	// Must be called after interrupts are enabled
	wave_init();	// Initialise WAVE file interface
}

/************************************************************************/
/* CALLBACK FUNCTIONS FOR CIRCULAR BUFFER                               */
/************************************************************************/

// CALLED FROM BUFFER MODULE WHEN A PAGE IS FILLED WITH RECORDED SAMPLES
void pageFull() {
	if(!(--pageCount)) {
		// If all pages have been read
		adc_stop();		// Stop recording (disable new ADC conversions)
		stop = 1;		// Flag recording complete
	} else {
		newPage = 1;	// Flag new page is ready to write to SD card
	}
}

// CALLED FROM BUFFER MODULE WHEN A NEW PAGE HAS BEEN EMPTIED
void pageEmpty() {
	// TODO: Implement code to handle "page empty" callback 
	if(!(--pageCount)) //If all pages have been read
		stop = 1;
	else
		newPage = 1;  // Flag new page is ready to write to SD card
}

/************************************************************************/
/* RECORD/PLAYBACK ROUTINES                                             */
/************************************************************************/

// Initiates a record cycle
void dvr_record() {
	buffer_reset();		// Reset buffer state
	countpage = 0;
	pageCount = 305;	// Maximum record time of 10 sec
	newPage = 0;		// Clear new page flag
	
	wave_create();		// Create new wave file on the SD card
	adc_start();		// Begin sampling

	// TODO: Add code to handle LEDs
	PORTD &= 0b10001111; // all LEDs off state
	PORTD |= (1<<PIND5);
}

// TODO: Implement code to initiate playback and to stop recording/playback.

void PWM_init() {
	
	cli();

	// Configure system clock for 16 MHz
	CLKPR = 0x80;	// Prescaler change enable
	CLKPR = 0x00;	// Prescaler /1, 16 MHz
	
	DDRF &= 0b10001111;    // Pushbuttons 1 to 3 - PORTF 6-4 as inputs
	DDRD |= 0b11110000;		// Set PORTD 7-4 as outputs (LEDs)
	DDRB |= 0b01000000;	   // JOUT - PORTB 6 as an output

OCR1A = 511;       //TOP, 15.625kHz
OCR1B = 512*0.5;        //50% duty cycle
TIMSK1 |= 0b00000001; //Enable overflow interrupt
TCCR1A = 0b00100011; //Fast PWM (TOP = OCR1A) set OC1B on TOP, reset on CMP
TCCR1B = 0b00011001; //Fast PWM (TOP = OCR1A), /1 prescaler
TCNT1 = 0x00;  // reset timer

serial_init();	// Initialise USB serial interface (debug)

sei();
}

void PWM_stop(){
		TCCR1A = 0;
		TIMSK1 = 0;
		OCR1B = 0;
		TCNT1 = 0;
}

ISR(TIMER1_OVF_vect) {
	overflow_counter++;
	
	if (overflow_counter == overflow_reset) {
	uint8_t output = buffer_dequeue();		//dequeue here
	OCR1B = output ;
	overflow_counter =0;
	
	}
	

}//ISR

void playback() {
	buffer_reset();
	pageCount = countpage;
	
	overflow_reset = 2;
	overflow_counter = 0;
	PORTD |= 0b00010000;
	wave_open();
	wave_read(buffer_writePage(), 1024);
	newPage = 0;
	PWM_init();
}



/************************************************************************/
/* MAIN LOOP (CODE ENTRY)                                               */
/************************************************************************/
int main(void) {
	
	uint8_t state = DVR_STOPPED;
	uint8_t pb = 0x00;
	uint8_t pb_prev = 0x00;
	uint8_t pb_rise = 0x00;
	
	// Initialisation
	init();
	
	PORTD |= (1<<PIND6);
	printf("Your SD card is not plugged in properly. Try again!\n");
	
	// Loop forever (state machine)
	// Loop forever (state machine)
	for(;;) {
		
		//Debouncing code
		pb = pb_debounced;
		pb_rise = (pb & (pb ^ pb_prev)); //Rising edge
		pb_prev = pb;
		
		
		// Switch depending on state
		switch (state) {
			case DVR_STOPPED:
			// TODO: Implement button/LED handling for record/playback/stop
			//S1 pressed
			if (pb_rise & (1<<PINF4))
			{
				printf("Begin Playback...");	// Output status to console
				playback();
				state = DVR_PLAYING;
				PORTD &= 0b10001111; // all LEDs off state
				PORTD |= (1<<PIND4); // LED1 on
			} 
			 if (pb_rise & (1<<PINF5))
			{
				// TODO: Implement code to initiate recording
				//S2 pressed
		// Output status to console
				dvr_record();			// Initiate recording
				state = DVR_RECORDING;
				PORTD &= 0b10001111; // all LEDs off state
				PORTD |= (1<<PIND5);  // LED2 on
				printf("Recording...");
			}
			break;
			case DVR_RECORDING:
			if (pb_rise & (1<<PINF6))
			{
				// TODO: Implement stop functionality
				//S3 pressed
				PORTD &= 0b10001111; // all LEDs off state
				PORTD |= (1<<PIND6);  //LED3 on
				
				pageCount = 1;	// Finish recording last page
			}
			// Write samples to SD card when buffer page is full
			if (newPage) {
				countpage++;
				newPage = 0;	// Acknowledge new page flag
				wave_write(buffer_readPage(), 512);
				} 
			else if (stop) {
				// Stop is flagged when the last page has been recorded
				stop = 0;							// Acknowledge stop flag
				wave_write(buffer_readPage(), 512);	// Write final page
				wave_close();						// Finalise WAVE file
				adc_stop();
				printf("DONE!\n");					// Print status to console
				while (pb_rise & (1<<PINF6)){
					printf("Please release record button ........ \n");
				continue;}
				state = DVR_STOPPED;				// Transition to stopped state
			}
			
			
			
			break;
			case DVR_PLAYING:
			if (newPage)
			{
				newPage = 0;	// Acknowledge new page flag
				wave_read(buffer_writePage(), 512);

			}	 else if (stop || (pb_rise & (1<<PINF6))){
				// TODO: Implement playback functionality
					stop = 0;       // Acknowledge stop flag
					wave_close();   // Finalise WAVE file
					PWM_stop();
					printf("DONE!\n");	 // Print status to console
					//S3 pressed
					PORTD &= 0b10001111; // all LEDs off state
					PORTD |= (1<<PIND6);  //LED3 on
					state = DVR_STOPPED;
					overflow_reset = 2;
				}
				
			// TODO: Implement playback functionality
			break;
			default:
			
			// Invalid state, return to valid idle state (stopped)
			printf("ERROR: State machine in main entered invalid state!\n");
			state = DVR_STOPPED;
			PORTD |= (1<<PIND6);   // Turn LED 3 ON
			PORTD &= 0b10001111; // all LEDs off state
			break;
		

	} // END for(;;)
	}



}

