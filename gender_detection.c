/*

Author: Steven Grosz
Collaborator: Sylmarie Dávila
Date: 12/8/2018

Purpose: This program identifies the gender of a speaker.

The code will parse a speech signal into multiple window samples
and perform processing on each individual window and output
either a 0,1,2. 0 indicates that the gender could not be identified,
a 1 indicates a male, and a 2 indicates a female.

The algorithm used requires a training period that provides a
normalization of the sampled speech signal.

The first five windows are used to calculate an offset (an average)
to be subtracted from the signal.

The next five windows are used to establish thresholds in identifying
certain characteristics of speech, such as signal energy (SE) and zero
crossing rate (ZCR). These thresholds are used to distinguish speech from noise
and voiced syllables from un-voiced syllables.

Voiced syllables are characterisitc of high signal energy and low zero
crossing rate. If a possible voiced signal is identified, the code
attempts to detect the period of that speech window. From the period,
the pitch (and thus the gender) of the speaker can be interpreted.

Common frequency cut-offs to distinguish male from female speakers
are the following:
male: 	101 Hz < freq < 175 Hz
female: 190 Hz < freq < 400 Hz

*/

#include <stdlib.h>
#include <stdint.h>
#include <math.h>  //for use of log10
#include <stdio.h>

//define global variables
const int Fs = 10000;  //sampling rate of microphone
const int window_size = 150;  //window_size (data samples per window)
const int length_lp = 15;  //length of lowpass filter coefficient array
const int length_bp = 31;  //length of bandpass filter coefficient array
float offset;  //dc offset level of microphone
int thresholds[2] = {0};  //[0] = SE threshold, [1] = ZCR threshold
int gender;
float window_sample[150];  //array to hold current data window
int data_u[39994];  //data from entire speech recording (for validation puposes)
int SE_avg = 0;  //calculates a running average of SE (used in calc threshold)
int SE_thresh;  //signal energy threshold
int ZCR_thresh = 0;  //zero crossing rate threshold

//define filter arrays
const float lowpassFilter[15] = {
   0.001121705747, 0.003906643018, -0.01608382165,   0.0204258766,   0.0210157074,
    -0.1248775125,   0.2452525347,   0.6984777451,   0.2452525347,  -0.1248775125,
     0.0210157074,   0.0204258766, -0.01608382165, 0.003906643018, 0.001121705747
};

const float bandpassFilter[31] = {
  3.023164969e-19,0.0005073816283,0.0004706283216,-0.0009834705852,-0.005124626681,
   -0.01263490878, -0.02246246487,    -0.03121165, -0.03363485634,  -0.0243132785,
  3.200776042e-17,  0.03834588081,  0.08483161777,   0.1297538131,   0.1623560637,
     0.1742735952,   0.1623560637,   0.1297538131,  0.08483161777,  0.03834588081,
  3.200776042e-17,  -0.0243132785, -0.03363485634,    -0.03121165, -0.02246246487,
   -0.01263490878,-0.005124626681,-0.0009834705852,0.0004706283216,0.0005073816283,
  3.023164969e-19
};

//function declarations
float calcOffset();
void subOffset();
void lowPass();
void bandPass();
void setThresh();
void autoC();
int calcFreq();
int calcGender(int F_speech);
int genDetect();


//main function
int main(void){
	//set up data for testing
	int i;
	int j;
  int x;
  int count = 0;
  char chr;
  int total_num_data_points = 0;
  int N_windows;

  //Opening data file for testing

  FILE *fp;

  //first open file and count number of total data points contained

  if((fp=fopen("/Users/Stevengrosz/Documents/IndStudy/speech_processing/data_sets/amsac_F_1.txt","r"))==NULL)
  {
      printf("cannot open the file");
      exit(1);
  }
  else{

  chr = getc(fp);
  while (chr != EOF)
  {
      //Count whenever new line is encountered
      if (chr == '\n')
      {
          total_num_data_points++;
      }
      //take next character from file.
      chr = getc(fp);
  }

  fclose(fp);

  //now create an array of size == total num of data points

  int data[total_num_data_points];

  //store data from the file in the data array

  if((fp=fopen("/Users/Stevengrosz/Documents/IndStudy/speech_processing/data_sets/amsac_F_1.txt","r"))==NULL)
  {
      printf("cannot open the file");
      exit(1);
  }
  else{
  	for(i=0;i<total_num_data_points;i++){
  		fscanf(fp, "%d", &x);
  		data_u[i] = (int)x;
  	}
  }

	}
	fclose(fp);

	//Divide data into N_windows by dividing by window_size
	N_windows = total_num_data_points/window_size; //calc the total # of window samples

  //begin training phase to establish offset and thresholds

	//calculating offset from first five windows
	int offset_array[5];
	int k;
	for(k=0;k<5;k++){
		for(i=0;i<window_size;i++){
		window_sample[i] = data_u[i];
		}
		offset = offset + calcOffset();
	}

	offset=offset/5;

	//Next 5 windows are used to calc thresholds
	//input to setThresh should be an array of 5 windows
	//output = array of 2 entries: entry 1 = signal energy threshold,
  //entry 2 = Zero crossing rate threshold.
	for(k=0;k<5;k++){
		for(i=0;i<window_size;i++){
			window_sample[i] = data_u[k*window_size+window_size*5+i];
		}
		//calculating thresholds
		setThresh();
		SE_avg = (SE_avg*k+thresholds[0])/(k+1);
		ZCR_thresh = ZCR_thresh+thresholds[1];
	}

	ZCR_thresh = ZCR_thresh/5;
	SE_thresh = SE_avg;

	//Now ready to start gender processing
	//Use function genDetect within a loop to continuously process window samples
	//output stored in variable "gender" is either 1, 2, or 0 (1=male, 2=female, 0=N/A)
	for(i=10;i<N_windows;i++){
		for(j=0;j<window_size;j++){
			window_sample[j] = data_u[i*window_size+j];
		}
		gender = genDetect(window_sample);
		printf("%d\n", gender);
	}

	return 0;
}


//function definitions

//calcOffset
//operates on one window at a time
//output = offset value -> designate as a global variable (named offset)
/* The offset is calculated by taking the average of 5 window samples */
float calcOffset(){
	int i;
	int j;
	float temp_offset = 0;
	for(i=0;i<window_size;i++){
		temp_offset = temp_offset + window_sample[i];
	}
	return temp_offset/window_size;
}

//subOffset
//operates on one window at a time
//output = same array but values subtracted by offset value
/*Purpose of this function is to subtract the offset from every entry of a given window sample */
void subOffset(){
	int i;
	for(i=0;i<window_size;i++){
		window_sample[i] = window_sample[i]-offset;
	}
}

//lowPass
//operates on one window at a time
//output = same array but convolved with low-pass filter, adjusted to have same length
/* purpose of this function is to filter the window sample of speech in an attempt to filter out
high frequency noise. The cut off freq is Fc = 3500Hz*/
void lowPass(){
	float convlp[164] = {0};
	int t;
	int index;
	for(t=0;t<window_size+length_lp-1;t++){
		for(index=0;index<=t;index++){
			if((t-index<length_lp)&&(index<window_size)){
				convlp[t] = convlp[t]+window_sample[index]*lowpassFilter[t-index];
			}
		}
	}
	for(index=0;index<window_size;index++){
		window_sample[index] = convlp[index+length_lp/2];
	}
}

//bandPass
//operates on one window at a time
//output = same array but convolved with band-pass filter, adjusted to have same length
/* The purpose of this bandPass function is to center around the fundamental frequency of the given speech window
The cutoff frequencies are: f1=100Hz and f2=900Hz */
void bandPass(){
	float convbp[180] = {0};
	int t;
	int index;
	for(t=0;t<window_size+length_bp-1;t++){
		for(index=0;index<=t;index++){
			if((t-index<length_bp)&&(index<window_size)){
				convbp[t] = convbp[t]+window_sample[index]*bandpassFilter[t-index];
			}
		}
	}
	for(index=0;index<window_size;index++){
		window_sample[index] = convbp[index+length_bp/2];
	}
}

//setThresh
//operates on one window at a time, but call in a loop for five window samples
//call this function as is called in the main function. A running average of the
//signal energy is calculated over the five window samples. The ZCR threshold is also
//calculated as an average, but calculated after the five samples have been summed.
//output = array of length 2, with entries of SE_thresh and ZCR_thresh
/* The purpose of this function is to set the thresholds for Signal Energy (SE) and Zero Crossing Rate (ZCR)
to be used in determining if the aspect of the speech window being processed is speech vs noise and
if speech, to determine if it is a voiced segment v.s. unvoiced segment of speech. If voiced, then it will
be possible to extract the freq of the speaker since voiced speech is periodic */
void setThresh(){
	int SE_data = 0;
	int ZCR_data = 0;
	int j;
	subOffset();
	lowPass();
	for(j=0;j<window_size;j++){
		SE_data = SE_data + 10*log10(window_sample[j]*window_sample[j]);
		if(j<(window_size-1)){
			if((window_sample[j+1]>=0)&&(window_sample[j]<0)){
				ZCR_data = ZCR_data+1;
			}
			if((window_sample[j+1]<0)&&(window_sample[j]>=0)){
				ZCR_data = ZCR_data+1;
			}
		}
	}
	thresholds[0] = SE_data;
	thresholds[1] = ZCR_data;
}

//autoC
//This function will compute only the second half of autocorrelation,
//since it is symmetrical & we are only concerned with the 2nd half
//Function operates on one window at a time
//output = array of same size corresponding to the second half of the autocorrelated signal
/* this function is used to determine if the segment of speech being processed is periodic by performing
self/auto correlation */
void autoC(){
	float C_ptr[150] = {0};
	int delay;
	int j;
	int k;
	for(delay=0;delay<window_size;delay++){
		j=0;
		while((j+delay)<window_size) {
			C_ptr[delay] = C_ptr[delay] + window_sample[j]*window_sample[j+delay];
			j++;
		}
	}
	for(k=0;k<window_size;k++){
		window_sample[k] = C_ptr[k];
	}
}

//calcFreq
//operates on previosly auto correlated signal, which was saved in the same
//array as the original data "window_sample"
//output = Frequency of speech or return 0 if correlation result is not periodic
//only voiced signals are periodic, so this function will tell us whether the signal is a result of voiced speech
/* this function will extract the period from a periodic speech sample by identifying distance btw local maximums in the
autocorrelated signal */
int calcFreq(){
	int peak2 = 0;
	int loc2 = 1;
	int peak3 = 0;
	int loc3 = 1;
	for(int i=1;i<window_size-1;i++){ //window_size == length of C
		if((window_sample[i]>=window_sample[i-1])&&(window_sample[i]>=window_sample[i+1])){ //local max of C identified
			if(window_sample[i]>peak2){
				peak2=window_sample[i];
				loc2=i;
			}
			if((window_sample[i]>peak3)&&(window_sample[i]<peak2)){
				peak3=window_sample[i];
				loc3=i;
			}
		}
	}
	//check if distance btw peak2 and peak3 is within 5 of the location of peak2
	//If so, then the signal is periodic -> thus is a voiced signal and the freq can be identified
	if(((loc3-loc2)-loc2)<=5&&((loc3-loc2)-loc2)>=-5){
		//T_speech = loc2
		//F_speech = Fs/T_speech
		return Fs/loc2; //Fs is sampling rate of microphone
	}else{
		return 0; //the signal was not periodic -> meaning it was a voiceless syllable
	}
}

//calcGender
//input = Fspeech //which is the result of calcFreq
//output = 1 for male, 2 for female, or zero if N/A
/* this function will output the gender of the speaker */
int calcGender(int F_speech){
	if(F_speech<175 && F_speech>101){
		return 1;
	}
	else if(F_speech>190 && F_speech<400){
		return 2;
	}
	else{
		return 0;
	}
}

//genDetect
//operates on one window at a time
//output = gender of speaker (if able to identify)
/* this function puts together some of the above functions to perform the total signal processing
of the speech signal being analyzed */
int genDetect(){
	subOffset();
	lowPass();
	//float C[150];
	int F_speech;
	//calc SE and ZCR
	int SE = 0;
	int ZCR = 0;
	int i;
	for(i=0;i<window_size;i++){
		SE = SE + 10*log10(window_sample[i]*window_sample[i]);
		if(i<window_size-1){
			if((window_sample[i+1]>=0)&&(window_sample[i]<0)){
				ZCR = ZCR+1;
			}
			if((window_sample[i+1]<0)&&(window_sample[i]>=0)){
				ZCR = ZCR+1;
			}
		}
	}

	//compare against thresholds
	if(SE>=thresholds[0] && ZCR <= thresholds[1]){ //voiced signal identified
		//band-pass to center around fundamental frequency
		bandPass();
		//perform autocorrelation to test periodicity and if periodic -> extract period
		autoC();
		F_speech = calcFreq();
    return calcGender(F_speech);
	}

	return 0;
}
