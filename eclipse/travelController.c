/*
 * travelController.c
 *
 *  Created on: Apr 2, 2020
 *      Authors: Nicolaj Schmid & Théophane Mayaud
 * 	Project: EPFL MT BA6 penguins epuck2 project
 *
 * Introduction: This file deals with the control of the motors from the direction to be reached,
 * and stops when an obstacle/the objective is reached (detection with proximity sensor).
 * Functions prefix for this file: travCtrl_
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ch.h> //for threads

#include <motors.h>
#include <sensors/VL53L0X/VL53L0X.h>

#include <travelController.h>
#include <comms.h>

// From motors.h library functions need steps/s max speed 1100steps/s (MOTOR_SPEED_LIMIT) but for us might need less
// @warning do not set speed above MOTOR_SPEED_LIMIT - MOT_MAX_DIFF_SPS_FOR_CORRECTION otherwise it couldn't turn !
#define MOT_MAX_NEEDED_SPS 500
#define MAX_DISTANCE_VALUE_MM 500 //how far in mm should robot start to slow
#define STOP_DISTANCE_VALUE_MM 30 //how far in mm should robot stop
#define STOP_DISTANCE_AVERAGE_N 3 //to filter too high variations

#define MOT_MAX_ANGLE_TO_CORRECT 100	// this will be the max angle in ° that the correction will still change
#define MOT_MAX_DIFF_SPS_FOR_CORRECTION 300
//#define MOT_CORRECTION_EXPONENT 2.5 //can range from 1 (no less than linear) to technically anything, and with decimals
#define MOT_KP_DIFF 1	//needs >=0 value
#define MOT_KI_DIFF 0.5 //needs >=0 value
#define MOT_KI_N_ANGLES 5
#define MOT_KP_FWD 0.5 //forward speed KP needs >=0 value
#define MOT_KI_FWD 1 //forward speed KI needs >=0 value


#define MOT_CONTROLLER_PERIOD 10 //in ms, will be the interval at which controller thread will re-adjust control
#define MOT_CONTROLLER_WORKING_AREA_SIZE 1024 //128 because it should be enough !

#define IR_FRONT_RIGHT 0 //IR1 so sensor number 0
#define IR_FRONT_LEFT 7 //IR8 so sensor number 7
#define IR_CALIB_0_DISTANCE -3700 //from http://www.e-puck.org/index.php?option=com_content&view=article&id=22&Itemid=13
#define IR_CALIB_MAX_RANGE -750 //same source, actually only somewhat linear bewteen -3700 and -1000

int16_t destAngle = 0; //from -179 to +180
int16_t lastNAngles[MOT_KI_N_ANGLES] = {0}; //set all to 0
uint16_t destDistanceMM = 0;
uint16_t lastNdestDistanceMM[STOP_DISTANCE_AVERAGE_N] = {0};
int rightMotSpeed = 0; //from -126 to +126, it is an int as in motors.h
int leftMotSpeed = 0; //from -126 to +126, it is an int as in motors.h
thread_t *motCtrlThread; // pointer to motor controller thread if needed to stop it TODOPING maybe remove if not necessary anymore

travCtrl_destReached destReachedFctToCall;

/*===========================================================================*/
/* Internal functions definitions             */
/*===========================================================================*/
void dirAngleCb(int16_t newDestAngle);
bool proxDistanceUpdate(void);
void motControllerUpdate(void);
int16_t motControllerCalculatetSpeedDiff(void);
int motControllerCalculateSpeed(void);

/*===========================================================================*/
/* Private functions              */
/*===========================================================================*/

/* Working area for the motor controller. */
static THD_WORKING_AREA(waMotControllerThd, MOT_CONTROLLER_WORKING_AREA_SIZE);
/* Motor controller thread. */
static THD_FUNCTION(MotControllerThd, arg) {
	(void)arg; // silence warning about unused argument
    systime_t time;
    static bool motCtrShldContinue = true;
	while (motCtrShldContinue) {
		time = chVTGetSystemTime();
		motCtrShldContinue = proxDistanceUpdate();
		motControllerUpdate();
		chThdSleepUntilWindowed(time, time + MS2ST(MOT_CONTROLLER_PERIOD));
	}
	destReachedFctToCall();
	chThdExit(true);
}

/**
 * @brief   Updates the measured distance to object
 * @return  false if destination is not reached 0, true otherwise
*/
bool proxDistanceUpdate(void){
	bool destIsNotReached = true;

	destDistanceMM = VL53L0X_get_dist_mm();

	if(destDistanceMM<=STOP_DISTANCE_VALUE_MM){
		destIsNotReached = false;
	}

	return destIsNotReached;
}


/**
 * @brief   Updates the speed differential of the motors based on angle
 * @return	The calculated value for the new speed differential, in steps per second,
 * 			between -MOT_MAX_DIFF_SPS_FOR_CORRECTION and MOT_MAX_DIFF_SPS_FOR_CORRECTION
*/
int16_t motControllerCalculatetSpeedDiff(void){
	int16_t motSpeedDiff = 0;
	int16_t sumLastNAngles = 0;

	if(destAngle > MOT_MAX_ANGLE_TO_CORRECT)
		destAngle = MOT_MAX_ANGLE_TO_CORRECT;
	else if(destAngle < (- MOT_MAX_ANGLE_TO_CORRECT) )
		destAngle = (- MOT_MAX_ANGLE_TO_CORRECT);
	// shift angles to add newest obeserved one (do this here because it is at regular intervals and do sum
	for(uint8_t i = MOT_KI_N_ANGLES - 1; i>=1;i--){ //1 offset because it's an array
		lastNAngles[i] = lastNAngles[i-1]; // shift all angles (discard oldest one)
		sumLastNAngles+=lastNAngles[i];
	}
	lastNAngles[0] = destAngle;
	sumLastNAngles+=lastNAngles[0];

	motSpeedDiff = MOT_KP_DIFF*destAngle + MOT_KI_DIFF*sumLastNAngles;

	if(motSpeedDiff > MOT_MAX_DIFF_SPS_FOR_CORRECTION)
		motSpeedDiff = MOT_MAX_DIFF_SPS_FOR_CORRECTION;
	else if( motSpeedDiff < (- MOT_MAX_DIFF_SPS_FOR_CORRECTION))
		motSpeedDiff = (- MOT_MAX_DIFF_SPS_FOR_CORRECTION);

	return motSpeedDiff;
}

/**
 * @brief   Updates the speed for both motors based on distance
 * @return 	The calculated speed for the motors in steps per second, between 0 and MOT_MAX_NEEDED_SPS
*/
int motControllerCalculateSpeed(void){
	int robSpeed = 0; //TODOPING why int and not uint16_t here ?

	// first : filter distances using KP KI controller
	uint16_t sumLastNdestDistanceMM = 0;
	// ---- shift last N distances in array
	for(uint8_t i = STOP_DISTANCE_AVERAGE_N - 1; i>=1;i--){ //1 offset because it's an array
		lastNdestDistanceMM[i] = lastNdestDistanceMM[i-1]; // shift all angles (discard oldest one)
		sumLastNdestDistanceMM+=lastNdestDistanceMM[i];
		}
	lastNdestDistanceMM[0] = destDistanceMM;
	sumLastNdestDistanceMM+=lastNdestDistanceMM[0];

	// -- Calculate filtered destDistanceMM value
	destDistanceMM = MOT_KP_FWD*destDistanceMM + MOT_KI_FWD*sumLastNdestDistanceMM;

	// if in controller bounds, then calculate robSpeed with parameters
	if(STOP_DISTANCE_VALUE_MM <= destDistanceMM && destDistanceMM <= MAX_DISTANCE_VALUE_MM){
		robSpeed = ( MOT_MAX_NEEDED_SPS * (destDistanceMM-STOP_DISTANCE_VALUE_MM) )/(MAX_DISTANCE_VALUE_MM-STOP_DISTANCE_VALUE_MM);
	}
	else if(destDistanceMM > MAX_DISTANCE_VALUE_MM)
		robSpeed = MOT_MAX_NEEDED_SPS;

	return robSpeed;
}

/**
 * @brief   Updates the speeds of the motors based on distance and angle
*/
void motControllerUpdate(void){
	//First : control speed differential based on angle
	int16_t motSpeedDiff = motControllerCalculatetSpeedDiff();

	// Then update speed based on distance
	uint16_t robSpeed = motControllerCalculateSpeed();

	// Then actually update motor speeds
	rightMotSpeed = robSpeed + motSpeedDiff;
	leftMotSpeed = robSpeed - motSpeedDiff;
	//TESTPING not outputting values to motors because for now need to check if it works ok
	chprintf(UART_PORT_STREAM, "New rob speeds are : Left = %d, Right=%d, distanceMM = %d\n\r", leftMotSpeed,rightMotSpeed,destDistanceMM);
//	right_motor_set_speed(rightMotSpeed);
//	left_motor_set_speed(leftMotSpeed);
}

/**
 * @brief   Callback fct given to exterior for when the angle needs updating.
 * @parameter [in] newDestAngle new angle direction between -179° and 180°
*/
void dirAngleCb(int16_t newDestAngle){
	destAngle = newDestAngle;
	//TESTPING
	chprintf(UART_PORT_STREAM, "New dest angle is = %d \n\r", destAngle);
}

/*===========================================================================*/
/* Public functions for setting/getting internal parameters             */
/*===========================================================================*/

travCtrl_dirAngleCb_t travCtrl_init(travCtrl_destReached destReachedCallback){
	// start things, motor, proximity, thread for updating control params etc
	//inits the motors
	motors_init();

	VL53L0X_start();

	destAngle = 0;
	destDistanceMM = 0;

	destReachedFctToCall = destReachedCallback;

	//start of controller thread here
	motCtrlThread = 	chThdCreateStatic(waMotControllerThd, sizeof(waMotControllerThd), NORMALPRIO, MotControllerThd, NULL);

	return &dirAngleCb;
}

/*===========================================================================*/
/* Functions for testing              */
/*===========================================================================*/
/* TESTPING
* before calling this, need to do
* halInit();
* chSysInit();
*/

bool test_destReached = false;

void test_destReachedCB(void){
	test_destReached = true;
	chprintf(UART_PORT_STREAM,"test_destReachedCB was called and waiting 1second\n\r");
	chThdSleepMilliseconds(1000);
}

void travCtrl_testAll(void){
	chprintf(UART_PORT_STREAM,"Beginning of travCtrl_testAll\n\r");

	travCtrl_dirAngleCb_t updateAngle;

	comms_start();
	chprintf(UART_PORT_STREAM,"Starter comms and waiting 1second\n\r");
	chThdSleepMilliseconds(1000);

	updateAngle = travCtrl_init(test_destReachedCB);
	chprintf(UART_PORT_STREAM,"Starter controller, so distance is now active, and waiting 10seconds to test forward\n\r");
	chThdSleepMilliseconds(10000);

	chprintf(UART_PORT_STREAM,"Will now give 90° angle to controller for 5 seconds\n\r");
	updateAngle(90);
	chThdSleepMilliseconds(5000);

	chprintf(UART_PORT_STREAM,"Put angle back to 0 for 5 seconds\n\r");
	updateAngle(0);
	chThdSleepMilliseconds(5000);

	chprintf(UART_PORT_STREAM,"Will now give -90° angle to controller for 5 seconds\n\r");
	updateAngle(-90);
	chThdSleepMilliseconds(5000);

	chprintf(UART_PORT_STREAM,"Put angle back to 0 for 5 seconds\n\r");
	updateAngle(0);
	chThdSleepMilliseconds(5000);

// Now rapid positive angle tests, 4x for simulating rapid updating of sound direction
	chprintf(UART_PORT_STREAM,"Will now give 10°, 20, 50, 90, 150, 100, 0 angle to controller for 500ms each\n\r");
	chprintf(UART_PORT_STREAM,"Will also give each 4x in 10ms intervals to simulate rapid sound direction updates\n\r");
	updateAngle(10);
	chThdSleepMilliseconds(10);
	updateAngle(10);
	chThdSleepMilliseconds(10);
	updateAngle(10);
	chThdSleepMilliseconds(10);
	updateAngle(10);
	chThdSleepMilliseconds(10);
	chThdSleepMilliseconds(500);
	updateAngle(20);
	chThdSleepMilliseconds(10);
	updateAngle(20);
	chThdSleepMilliseconds(10);
	updateAngle(20);
	chThdSleepMilliseconds(10);
	updateAngle(20);
	chThdSleepMilliseconds(10);
	chThdSleepMilliseconds(500);
	updateAngle(50);
	chThdSleepMilliseconds(10);
	updateAngle(50);
	chThdSleepMilliseconds(10);
	updateAngle(50);
	chThdSleepMilliseconds(10);
	updateAngle(50);
	chThdSleepMilliseconds(10);
	chThdSleepMilliseconds(500);
	updateAngle(90);
	chThdSleepMilliseconds(10);
	updateAngle(90);
	chThdSleepMilliseconds(10);
	updateAngle(90);
	chThdSleepMilliseconds(10);
	updateAngle(90);
	chThdSleepMilliseconds(10);
	chThdSleepMilliseconds(500);
	updateAngle(150);
	chThdSleepMilliseconds(10);
	updateAngle(150);
	chThdSleepMilliseconds(10);
	updateAngle(150);
	chThdSleepMilliseconds(10);
	updateAngle(150);
	chThdSleepMilliseconds(500);
	updateAngle(100);
	chThdSleepMilliseconds(10);
	updateAngle(100);
	chThdSleepMilliseconds(10);
	updateAngle(100);
	chThdSleepMilliseconds(10);
	updateAngle(100);
	chThdSleepMilliseconds(10);
	chThdSleepMilliseconds(500);
	updateAngle(0);
	chThdSleepMilliseconds(10);
	updateAngle(0);
	chThdSleepMilliseconds(10);
	updateAngle(0);
	chThdSleepMilliseconds(10);
	updateAngle(0);
	chThdSleepMilliseconds(10);
	chThdSleepMilliseconds(1000);

	// Now rapid tests
	chprintf(UART_PORT_STREAM,"Will now give -10°, -20, -50, -90, -150, -100, 0 angle to controller for 500ms each\n\r");
	chprintf(UART_PORT_STREAM,"Will also give each 4x in 10ms intervals to simulate rapid sound direction updates\n\r");
	updateAngle(-10);
	chThdSleepMilliseconds(10);
	updateAngle(-10);
	chThdSleepMilliseconds(10);
	updateAngle(-10);
	chThdSleepMilliseconds(10);
	updateAngle(-10);
	chThdSleepMilliseconds(10);
	chThdSleepMilliseconds(500);
	updateAngle(-20);
	chThdSleepMilliseconds(10);
	updateAngle(-20);
	chThdSleepMilliseconds(10);
	updateAngle(-20);
	chThdSleepMilliseconds(10);
	updateAngle(-20);
	chThdSleepMilliseconds(10);
	chThdSleepMilliseconds(500);
	updateAngle(-50);
	chThdSleepMilliseconds(10);
	updateAngle(-50);
	chThdSleepMilliseconds(10);
	updateAngle(-50);
	chThdSleepMilliseconds(10);
	updateAngle(-50);
	chThdSleepMilliseconds(10);
	chThdSleepMilliseconds(500);
	updateAngle(-90);
	chThdSleepMilliseconds(10);
	updateAngle(-90);
	chThdSleepMilliseconds(10);
	updateAngle(-90);
	chThdSleepMilliseconds(10);
	updateAngle(-90);
	chThdSleepMilliseconds(10);
	chThdSleepMilliseconds(500);
	updateAngle(-150);
	chThdSleepMilliseconds(10);
	updateAngle(-150);
	chThdSleepMilliseconds(10);
	updateAngle(-150);
	chThdSleepMilliseconds(10);
	updateAngle(-150);
	chThdSleepMilliseconds(500);
	updateAngle(-100);
	chThdSleepMilliseconds(10);
	updateAngle(-100);
	chThdSleepMilliseconds(10);
	updateAngle(-100);
	chThdSleepMilliseconds(10);
	updateAngle(-100);
	chThdSleepMilliseconds(10);
	chThdSleepMilliseconds(500);
	updateAngle(-0);
	chThdSleepMilliseconds(10);
	updateAngle(-0);
	chThdSleepMilliseconds(10);
	updateAngle(-0);
	chThdSleepMilliseconds(10);
	updateAngle(-0);
	chThdSleepMilliseconds(10);
	chThdSleepMilliseconds(1000);


	chprintf(UART_PORT_STREAM,"Put angle back to 0 for 5 seconds\n\r");
	updateAngle(0);
	chThdSleepMilliseconds(5000);

	// Now rapid crazy angles test
	chprintf(UART_PORT_STREAM,"Will now give random angles for 10ms each\n\r");
	updateAngle(1);
	chThdSleepMilliseconds(10);
	updateAngle(-1);
	chThdSleepMilliseconds(10);
	updateAngle(-5);
	chThdSleepMilliseconds(10);
	updateAngle(5);
	chThdSleepMilliseconds(200);
	updateAngle(10);
	chThdSleepMilliseconds(10);
	updateAngle(-20);
	chThdSleepMilliseconds(10);
	updateAngle(20);
	chThdSleepMilliseconds(10);
	updateAngle(-23);
	chThdSleepMilliseconds(200);
	updateAngle(-50);
	chThdSleepMilliseconds(10);
	updateAngle(-100);
	chThdSleepMilliseconds(10);
	updateAngle(-150);
	chThdSleepMilliseconds(10);
	updateAngle(-159);
	chThdSleepMilliseconds(10);
	updateAngle(-90);
	chThdSleepMilliseconds(10);
	updateAngle(-99);
	chThdSleepMilliseconds(10);
	updateAngle(99);
	chThdSleepMilliseconds(10);
	updateAngle(130);
	chThdSleepMilliseconds(20);
	updateAngle(179);
	chThdSleepMilliseconds(10);
	updateAngle(160);
	chThdSleepMilliseconds(10);
	updateAngle(100);
	chThdSleepMilliseconds(10);
	updateAngle(20);
	chThdSleepMilliseconds(20);
	updateAngle(-50);
	chThdSleepMilliseconds(10);
	updateAngle(-100);
	chThdSleepMilliseconds(10);
	updateAngle(-110);
	chThdSleepMilliseconds(10);
	updateAngle(-120);
	chThdSleepMilliseconds(20);
	updateAngle(-150);
	chThdSleepMilliseconds(10);
	updateAngle(-160);
	chThdSleepMilliseconds(10);
	updateAngle(-79);
	chThdSleepMilliseconds(10);
	updateAngle(-179);
	chThdSleepMilliseconds(10);
	updateAngle(-178);
	chThdSleepMilliseconds(30);
	updateAngle(-177);
	chThdSleepMilliseconds(10);
	updateAngle(-160);
	chThdSleepMilliseconds(10);
	updateAngle(-179);
	chThdSleepMilliseconds(1000);
	chprintf(UART_PORT_STREAM,"Angle is back to 0 for 5 seconds\n\r");
	updateAngle(0);
	chThdSleepMilliseconds(5000);

	chprintf(UART_PORT_STREAM,"End of travCtrl_testAll\n\r");
	chThdSleepMilliseconds(1000);

}