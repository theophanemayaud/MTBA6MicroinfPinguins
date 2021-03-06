/*
 * comms.h
 *
 *  Created on: Apr 1, 2020
 *  Authors: Nicolaj Schmid & Théophane Mayaud
 * 	Project: EPFL MT BA6 penguins epuck2 project
 *
 * Introduction: Provides necessary interaction with computer for the project
 */
#ifndef COMMS_H_
#define COMMS_H_

/*===========================================================================*/
/* Public functions definitions            									 */
/*===========================================================================*/

/**
 * @brief   starts all communication processes
 */
void comms_start(void);

/**
 * @brief   use same function as chprintf() from chibios for sending information : System formatted output function.
 * @details This function implements a minimal @p printf() like functionality
 *          with output on a @p BaseSequentialStream.
 *          The general parameters format is: %[-][width|*][.precision|*][l|L]p.
 *          The following parameter types (p) are supported:
 *          - <b>x</b> hexadecimal integer.
 *          - <b>X</b> hexadecimal long.
 *          - <b>o</b> octal integer.
 *          - <b>O</b> octal long.
 *          - <b>d</b> decimal signed integer.
 *          - <b>D</b> decimal signed long.
 *          - <b>u</b> decimal unsigned integer.
 *          - <b>U</b> decimal unsigned long.
 *          - <b>c</b> character.
 *          - <b>s</b> string.
 *
 * @param[in] fmt       formatting string
 *
 *@return              The number of bytes that would have been
 *                      written to @p chp if no stream error occurs
 */
int comms_printf(const char *fmt, ...);

/**
 * @brief  	read from  USB_PORT or UART_PORT and store in char array
 * @note 	this function will print back what the user enters as he enters
 * 				so that he sees what he inputs
 * @warning	this function is blocking for the calling thread until either
 * 				the end char is met or the user presses enter
 *
 * @param[out] readText				pointer to char array where characters should be stored
 * 										\0 will be put at the end of table after reading
 * @param[in] arraySize				dictates max number of chars to be read including last \0 end character
 *
 *@return	Number of chars read and stored in array (not counting \0)
 */
uint16_t comms_readf(char *readText, uint16_t arraySize);


#endif /* COMMS_H_ */
