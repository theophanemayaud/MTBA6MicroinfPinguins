#ifndef AUDIO_PROCESSING_H
#define AUDIO_PROCESSING_H

/*Enable for Debugging audio_processing*/
#define DEBUG_AUDIO

#define FFT_SIZE 						1024
#define ERROR_AUDIO						9999						//Error number
#define SUCCESS_AUDIO					1
#define ERROR_AUDIO_SOURCE_NOT_FOUND		8888
#define NB_SOURCES_MAX					5						//Max 255 sources!
#define UNINITIALIZED_FREQ				0
#define UNINITIALIZED_INDEX				255

typedef enum {
	//2 times FFT_SIZE because these arrays contain complex numbers (real + imaginary)
	LEFT_CMPLX_INPUT = 0,
	RIGHT_CMPLX_INPUT,
	FRONT_CMPLX_INPUT,
	BACK_CMPLX_INPUT,
	//Arrays containing the computed magnitude of the complex numbers
	LEFT_OUTPUT,
	RIGHT_OUTPUT,
	FRONT_OUTPUT,
	BACK_OUTPUT
} BUFFER_NAME_t;

/*
 * Structure for each source
 * Freq is not in Hz!
 */
typedef struct Sources {
	uint16_t freq;
	float ampli;
} Source;

/*
 * Structure for destination source
 * Freq is not in Hz!
 */
typedef struct Destinations {
	uint8_t index;
	uint16_t freq;
	int16_t arg;
} Destination;

/*
*	Callback called when the demodulation of the four microphones is done.
*
*	Sampling freq of mic: 16kHz
*	Every 10ms we get 160 samples per mic
*	Fill the samples buffers to reach 1024 samples to calculate the FFTs
*
*	Parameters :
*	int16_t *data			Buffer containing 4 times 160 samples. the samples are sorted by mic:
*							 [micRight1, micLeft1, micBack1, micFront1, micRight2, etc...]
*	uint16_t num_samples		Tells how many data we get in total (should always be 640)
*/
void processAudioData(int16_t *data, uint16_t num_samples);

/*
 * Calculates FFT and its amplitude of the for mic
 * FFT is saved in mic_data and amplitude in mic_ampli
 */
void audioCalculateFFT(float *mic_data_left, float *mic_data_right, float *mic_data_back, float *mic_data_front,
						float *mic_ampli_left, float *mic_ampli_right, float *mic_ampli_back, float *mic_ampli_front);

/*
 * Returns freq of source: source[source_index].freq
 * Rturns ERROR_AUDIO if source[source_index].freq=ERROR_AUDIO or source[source_index].freq=ZERO
 */
uint16_t audioGetSourceFreq(uint8_t source_index);

/*
 * Determines the direction of the sound
 */
int16_t audioDetermineAngle(float *mic_data_left, float *mic_data_right, float *mic_data_back, float *mic_data_front, uint8_t source_index);

/*
 * Determines the phase shift
 */
int16_t audioDeterminePhase(float *mic_data1, float *mic_data2, uint8_t source_index);

/*
 * Calculates NB_SOURCES peak values and sorts them after freq
 * Returns peak freq of source_index; source_index=ZERO: lowest_freq, source_index=NB_SOURCES-ONE: highest_freq
 */
uint16_t audioPeak(float *mic_ampli_left, Destination *destination);

/*
 * Changing source array if necessary, array is sorted by ampli: source[0].=smallest_ampli, source[nb_sources].=biggest_ampli
 */
int16_t audioPeakScan(Source *source_init, uint8_t *nb_sources_init, float *mic_ampli);

/*
 * Changes source array corresponding to peak_mode and source_exchange
 *  peak_mode=PEAK_MODE_EXCHANGE: new ampli is bigger than min one of source array
 *  		source_exchange: indicates new position
 *  peak_mode=PEAK_MODE_SMALLER: new ampli is smaller than all of source array
 *  peak_mode=PEAK_MODE_REPLACE: freq_difference<FREQ_THD and one value of source array has to be replaced
 *  		source_exchange: indicates position of replacement
 */
int16_t audioPeakChange(int8_t source_exchange, uint16_t freq_counter, float *mic_ampli, uint8_t peak_mode, Source *source_init, uint8_t *nb_sources_init);

/*
 * Only fct that writes into source_init array !
 */
int16_t audioPeakWriteInit(uint8_t source_counter, uint8_t writing_mode, uint16_t freq_counter, float *mic_ampli, Source *source_init);

/*
 * Only fct that writes into global source array !
 */
int16_t audioPeakWriteSource(uint8_t source_counter, uint8_t writing_mode,  Source *source_init);

/*
 * Compares source_init and source_change
 */
uint8_t audioPeakCompareSource(Source *source_change, Source *source_init, uint8_t nb_sources_init);

/*
 * Bubblesort: max_freq[ZERO]=smallest_freq, max_freq[NB_SOURCES]=highest_freq
 */
int16_t audioPeakBubblesort(Source *source_init, uint8_t nb_sources_init, float *mic_ampli);

/*
 * Convert angle from radian into degree
 */
uint16_t audioConvertRad(float rad);

/*
 * Converts the FFT value into a real frequency
 */
uint16_t audioConvertFreq(uint16_t freq);

/*
 * Converts phase shift into angle
 * freq is in Hz!
 */
uint16_t audioConvertPhase(int16_t arg, uint16_t freq);

/*
*	Put the invoking thread into sleep until it can process the audio data
*/
void wait_send_to_computer(void);

/*
*	Returns the pointer to the BUFFER_NAME_t buffer asked
*/
float* get_audio_buffer_ptr(BUFFER_NAME_t name);

#endif /* AUDIO_PROCESSING_H */
