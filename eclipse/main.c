#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ch.h>
#include <hal.h>
#include <memory_protection.h>
#include <usbcfg.h>
#include <chprintf.h>
#include <motors.h>
#include <audio/microphone.h>


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ch.h"
#include "hal.h"
#include "memory_protection.h"
#include <usbcfg.h>
#include <main.h>
#include <chprintf.h>
#include <motors.h>
#include <audio/microphone.h>


#include <audio_processing.h>
#include <fft.h>
#include <arm_math.h>


#include <travelController.h>
#include <comms.h>


/*Enable for Debugging main*/
#define DEBUG_MAIN

//uncomment to send the FFTs results from the real microphones
#define SEND_FROM_MIC

//uncomment to use double buffering to send the FFT to the computer
#define DOUBLE_BUFFERING

#define NB_BYTE_PER_CMPX_VAL			2

travCtrl_dirAngleCb_t updateAngle;

//static void timer12_start(void){
//    //General Purpose Timer configuration
//
//    //timer 12 is a 16 bit timer so we can measure time
//    //to about 65ms with a 1Mhz counter
//    static const GPTConfig gpt12cfg = {
//        1000000,        /* 1MHz timer clock in order to measure uS.*/
//        NULL,           /* Timer callback.*/
//        0,
//        0
//    };
//
//    gptStart(&GPTD12, &gpt12cfg);
//    //let the timer count to max value
//    gptStartContinuous(&GPTD12, 0xFFFF);
//}

/* Time testing :
 * systime_t time = chVTGetSystemTime();
 *  printf("It took %d", ST2MS(time-chVTGetSystemTime() ));
 *
 */
void destReachedCB(void){
	//destReached = true;
	chprintf(UART_PORT_STREAM,"---------------------------------------------------------------\n\r");
	chprintf(UART_PORT_STREAM,"-                                                             -\n\r");
	chprintf(UART_PORT_STREAM,"-                                                             -\n\r");
	chprintf(UART_PORT_STREAM,"WARNING test_destReachedCB was called and waiting 1second\n\r");
	chprintf(UART_PORT_STREAM,"-                                                             -\n\r");
	chprintf(UART_PORT_STREAM,"-                                                             -\n\r");
	chprintf(UART_PORT_STREAM,"---------------------------------------------------------------\n\r");

	chThdSleepMilliseconds(1000);
}

int main(void)
{
	halInit(); //
	chSysInit();
	mpu_init();
	//timer12_start();

	comms_start();
	chprintf(UART_PORT_STREAM,"Starting main !\n\r");

	updateAngle = travCtrl_init(destReachedCB);

	//TESTPING test travelController functions!
	//travCtrl_testAll();

	comms_printf(UART_PORT_STREAM, "In MAIN \n\r");
	comms_printf(UART_PORT_STREAM, "We will now ask for text. Enter anything then press enter\n\r");

    //send_tab is used to save the state of the buffer to send (double buffering)
    //to avoid modifications of the buffer while sending it

    static float mic_data_right[NB_BYTE_PER_CMPX_VAL*FFT_SIZE];
    static float mic_data_left[NB_BYTE_PER_CMPX_VAL*FFT_SIZE];
    static float mic_data_front[NB_BYTE_PER_CMPX_VAL*FFT_SIZE];
    static float mic_data_back[NB_BYTE_PER_CMPX_VAL*FFT_SIZE];
    static float mic_ampli_right[FFT_SIZE];
    static float mic_ampli_left[FFT_SIZE];
    static float mic_ampli_front[FFT_SIZE];
    static float mic_ampli_back[FFT_SIZE];

    Destination destination;
    int16_t audio_peak			= 0;

    destination.index		= UNINITIALIZED_INDEX;
    destination.freq			= UNINITIALIZED_FREQ;
    destination.arg			= 0;

    /* SEND_FROM_MIC */
    //starts the microphones processing thread.
    //it calls the callback given in parameter when samples are ready
    mic_start(&processAudioData);

    /* Infinite loop. */
    while (1) {
    		//comms_printf(UART_PORT_STREAM, "Now please enter anything\n\r");

        /*Waits until enough samples are collected*/
    		wait_send_to_computer();

        /*Copy buffer to avoid conflicts*/
        arm_copy_f32(get_audio_buffer_ptr(LEFT_CMPLX_INPUT), mic_data_left, NB_BYTE_PER_CMPX_VAL*FFT_SIZE);
        arm_copy_f32(get_audio_buffer_ptr(RIGHT_CMPLX_INPUT), mic_data_right, NB_BYTE_PER_CMPX_VAL*FFT_SIZE);
        arm_copy_f32(get_audio_buffer_ptr(FRONT_CMPLX_INPUT), mic_data_front, NB_BYTE_PER_CMPX_VAL*FFT_SIZE);
        arm_copy_f32(get_audio_buffer_ptr(BACK_CMPLX_INPUT), mic_data_back, NB_BYTE_PER_CMPX_VAL*FFT_SIZE);

        /*Calculating FFT and its amplitude*/
        audioCalculateFFT(mic_data_left, mic_data_right, mic_data_back, mic_data_front, mic_ampli_left, mic_ampli_right, mic_ampli_back, mic_ampli_front);

        /*Testing two sources*/
        destination.freq=998;	//corresponding to 998=400Hz, 991=500Hz, 995=450Hz
        audio_peak = audioPeak(mic_ampli_left, &destination);
        if(audio_peak==ERROR_AUDIO){
#ifdef DEBUG_MAIN
        		chprintf((BaseSequentialStream *)&SD3, "main:	Error in audioPeak\n\r\n\r");
#endif
        }
        else if(audio_peak==ERROR_AUDIO_SOURCE_NOT_FOUND){
#ifdef DEBUG_MAIN
			chprintf((BaseSequentialStream *)&SD3, "main:	Error source not found ! \n\r\n\r");
#endif
        }
        else{
        		if(destination.index==UNINITIALIZED_INDEX){
#ifdef DEBUG_MAIN
        			chprintf((BaseSequentialStream *)&SD3, "main:	UNINITIALIZED_INDEX\n\r\n\r");
#endif
        		}
        		else{
        			destination.arg = audioDetermineAngle(mic_data_left, mic_data_right, mic_data_back, mic_data_front, destination.index);
            		if(destination.arg==ERROR_AUDIO){
#ifdef DEBUG_MAIN
            			chprintf((BaseSequentialStream *)&SD3, "main:	Error in audioAnalyseDirection\n\r\n\r");
#endif
                		destination.arg = 0;
                		updateAngle(destination.arg);
              	}
            		else{
#ifdef DEBUG_MAIN
            			chprintf((BaseSequentialStream *)&SD3, "main:	Source %d :		Freq %d	:		arg  = %d\n\r", destination.index, audioConvertFreq(destination.freq), destination.arg);
#endif
                		updateAngle(destination.arg);
            		}
        		}

        }

    }
}

/*===========================================================================*/
/* Something to protect agains something...                                  */
/*===========================================================================*/
#define STACK_CHK_GUARD 0xe2dee396
uintptr_t __stack_chk_guard = STACK_CHK_GUARD;

void __stack_chk_fail(void)
{
    chSysHalt("Stack smashing detected");
}
