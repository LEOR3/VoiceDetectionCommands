/* VOICE DETECTION PROJECT 
 * Author: LEOR
 * Voice Detection With 3 Saved Commands

   HARDWARE
   - ESP32 Microcontroller
   - Microphone (KY-038)
   - OLED 128X64  Interface: I2C
   - Buttons (If you need the 2 pages that are included in this project).

   SOFTWARE
   - Arduino FFT Library by Enrique Condes (kosme), you can download it directly in arduino IDE library manager, if you are being use this IDE.
      https://github.com/kosme/arduinoFFT.git
    
*/

#include <LiquidCrystal.h>
#include <ESP32TimerInterrupt.h>
#include <ESP32_ISR_Timer.h>
#include <math.h>
#include "arduinoFFT.h"

#include <splash.h>
#include <Wire.h>
#include <Adafruit_GFX.h>    // Core graphics library
#include <Fonts/FreeMonoBoldOblique12pt7b.h>
#include <Fonts/FreeSerif9pt7b.h>
#include <Adafruit_SSD1306.h>

// With this configuration it goes from 10hz to 1khz exactly
//#define SAMPLES 512              // Must be a power of 2, max512 creo
//#define SAMPLING_FREQ 2000 // Hz, must be 40000 or less due to ADC conversion time. Determines maximum frequency that can be analysed by the FFT Fmax=sampleF/2.

#define SAMPLES 512         //SAMPLES FFT. Must be a base 2 number. Max 128 for Arduino Uno.
#define SAMPLING_FREQ 8000   //Ts = Based on Nyquist, must be 2 times the highest expected frequency. Determines maximum frequency that can be analysed by the FFT  Fmax = sampleF/2
#define THRESHOLD 20
#define NOISE 400           // Used as a crude noise filter, values below this are ignored
#define NAverage 12


#define SCREEN_WIDTH 128 // OLED width,  in pixels
#define SCREEN_HEIGHT 64 // OLED height, in pixels

// create an OLED display object connected to I2C
Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

/*==========================================================================================================
                                FFT INFORMATION
============================================================================================================*/


// The maximum frequency we can sample is half the sampling rate
// Frequency bins are the samples quantity
/*
          |
          |
          |     . ------------------------------------------------> MaxAmplitude     
vReal[i]  |   . |  .
          |     |   .
          | .   |       .
          |     |
          |     |
          ------------------------------------------------
                |          Frequency Bin Number
             Max Frequency

We can only use the first half of the data so from SAMPLES/2 becuase the rest of the values are negative and are meaningless
for looking at power

The first sample is also unusable as this is swapped by DC 
(Ignore bin 0, this mainly contains DC and mains hum)

Frequency width of each bin is Sample Rate / Number of Samples 
So everything between Zero and Frequency Width is locked into bin zero, from Frequency Width to 2*Frequency Width is in bin one
*/


/*==========================================================================================================
                                  DECLARATION PART
============================================================================================================*/


/*==========================================================================================================
                                       FFT
============================================================================================================*/

// METHOD 1

//Sampling and FFT stuff

unsigned int samplingPeriod;
unsigned long microSeconds;

const int number = round(SAMPLING_FREQ/SAMPLES);
 
double vReal[SAMPLES]; //create 1D vector of size SAMPLES to hold real values
double vImag[SAMPLES]; //create 1D vector of size SAMPLES to hold imaginary values
double datasAv[number]; //create vector of size SAMPLING_FREQ/SAMPLES to hold averages
double datas[number][SAMPLES]; //create 2D vector of size SAMPLES to hold real values of n FFT

arduinoFFT FFT = arduinoFFT();
//arduinoFFT FFT = arduinoFFT(vReal, vImag, SAMPLES, SAMPLING_FREQ);

//---------------------------------------------------------------------------//


/*==========================================================================================================
                                       Variables
============================================================================================================*/

int analogPin = 35; //Pin that reads the analogpin

//

int j = 0;
int k = 0;
double sum = 0;
double sumAv = 0;
double average = 0;
int c = 0;
int f = 0;
int comando=0; 

double res[SAMPLES/2];
double command[3][SAMPLES/2];

boolean page = false;
unsigned long newTime, new2Time;


//--------------------------------------------------------------------------------

void setup() {
  //Set up SERIAL
  Serial.begin(9600);
  
  //Set up INPUTS
  pinMode(18, INPUT_PULLUP);
  pinMode(12, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);

  //Set up OLED
  // Initialize OLED display with I2C address 0x3C
  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("failed to start SSD1306 OLED"));
    while (1);
  }else
    Serial.println("OLED STARTED");
  delay(2000);         // Wait two seconds for initializing

  oled.clearDisplay(); // Clear display
  oled.setTextColor(WHITE);    // You have to configure it to work
  oled.setTextSize(0.1);         // Set the letter size
  
  //Sampling Period FFT
  //How many microseconds there are between each sampling event
  samplingPeriod = round(1000000*(1.0/SAMPLING_FREQ)); //Period in microseconds. This convert into microseconds
  
}

void loop() {

// ========================================================================================================
// METHOD 1
// =========================================================================================================


// Serial.println("Say the correct phrase");
// delay(2000);
// Serial.println("GO");
// delay(1000);

  if(page == false){
    page1();
  }else{
    page2();
  }

  if (digitalRead(18) == LOW){ // Only do this when you push the button
  
    Serial.println("GO");
    oled.clearDisplay(); // clear display
    oled.setCursor(80, 0);
    oled.println("GO");  
    oled.display(); 
  
   //    READING PART    //
   // Reading samples in 1 sec, for this is necessary to do SAMPLING_FREQ/SAMPLES
  
   newTime = micros();
   
   for (int i= 0; i<number; i++){ // Rows
    for(int j=0; j<SAMPLES; j++) //Columns
      {
          microSeconds = micros();    //Returns the number of microseconds since the microcontroller board began running the current script. 
  
          //A conversion takes about 9.7us on an ESP32
          vReal[j] = analogRead(analogPin); //Reads the value from analog pin, quantize it and save it as a real term.
          vImag[j] = 0; //Makes imaginary term 0 always
  
          /*Remaining wait time between samples if necessary*/
          while(micros() < (microSeconds + samplingPeriod))
          {
            //do nothing
          }
      }
      /*Perform FFT on samples*/
      //Compute FFT
      FFT.DCRemoval(vReal,SAMPLES);
      FFT.Windowing(vReal, SAMPLES, FFT_WIN_TYP_RECTANGLE, FFT_FORWARD); /* Weigh data */
      FFT.Compute(vReal, vImag, SAMPLES, FFT_FORWARD); /* Compute FFT */
      FFT.ComplexToMagnitude(vReal, vImag, SAMPLES); /* Compute magnitudes */
      
      //Retrieve the amplitude of each position    
      for(int l=0;l<SAMPLES;l++){
        datas[i][l] = vReal[l];
        //Serial.println(datas[i][l]);
      }
  }
  
  
    float conversionTime = (micros() - newTime) / 1000000.0;
    Serial.print("Conversion time: ");
    Serial.print(conversionTime);
    Serial.println(" SEG");
      
    // Analyse FFT results
    // Each element represents a frequency bin and the value of each element represents the amplitude of the amount of that frequency
       
    k=0;
    sum=0;
    double peak = 0;
    double sumAverages;
    int temp;
     
    
    // PROCESSING //
     
    // We avaerage each data corresponding to all the sampling_frequency/samples values and thus obtain in a 1D array of the average //
    
    for (int j=0; j < (SAMPLES/2); j++){ 
      for (int i=0; i < number; i++){
      
          // Don't use sample 0 and only first SAMPLES/2 are usable. Each array element represents a frequency bin and its value the amplitude.
         sum += datas[i][j];
         sumAv = sum;
          
        }  
        res[j] += sum/number;
        sum = 0;
      }  
  
    // SAVE DATA //
   
    if(f < 3){
      c++;
      if (c == 10){
        for(int b=0; b<(SAMPLES/2); b++) // Saves the x command of the sum of the averages of the SAMPLING_FREQ/SAMPLES FFT
         command[f][b]=res[b]/c;
        for (int j=0; j < (SAMPLES/2); j++) // Resets the value where the sum of the averages of the SAMPLING_FREQ/SAMPLES FFT is stored
          res[j] = 0;
        c = 0;
        f++;
      }
    }
  
     
    // READING AND CONFIRMATION //
         
    int deviation[3]; // Deviation from mean

     
    if (f == 3){
      for(int c=0; c<3 ;c++)
          deviation[c] = 0; // Deviation sums
          
 
      for(int c=0; c<3 ;c++){
        for(int b=0; b<(SAMPLES/2); b++){
          deviation[c]+=abs(command[c][b]-res[b]); // Deviation sums
//           Serial.print("res: ");
//           Serial.println(res[b]);
//           Serial.print("command: ");
//           Serial.println(command[c][b]);
//           Serial.print("deviation: ");
//           Serial.println(deviation[c]); 
//           Serial.println("---------------------");     
        }  
      }

      Serial.print("MaxAmplitude: ");
      Serial.println(FFT.MajorPeakAmplitude());    
      
      if(deviation[0]<deviation[1] && deviation[0]<deviation[2])
        comando=1;
      else if(deviation[1]<deviation[0] && deviation[1]<deviation[2])
        comando=2;
       else
        comando=3;
    
       Serial.print("deviation1: ");
       Serial.println(deviation[0]);
       Serial.print("deviation2: ");
       Serial.println(deviation[1]);
       Serial.print("deviation3: ");
       Serial.println(deviation[2]);

       for (int j=0; j < (SAMPLES/2); j++)
          res[j] = 0;
          
       for(int c=0; c<3 ;c++)
          deviation[c] = 0; // Deviation sums
    }
    
  }
  
  if (digitalRead(12) == LOW){ //FIRST PAGE OLED
    page = false;
  }
  
  if (digitalRead(14) == LOW){ //FIRST PAGE OLED
    page = true;
  }
   
}


/* FUNCTIONS  */


double GetFreqFFT(int i)
{  
  double freq = i * SAMPLING_FREQ / SAMPLES;
  return(freq);
}

void page1(){
    //Serial.println("STAY");
    oled.clearDisplay(); // clear display
    oled.setCursor(0,10);
    oled.println(comando); 
    oled.setCursor(80, 0);
    oled.println("STAY");  
    oled.setCursor(0, 28);      
    oled.print("CommmandSample:");
    oled.println(c);
    oled.setCursor(0, 48);      
    oled.print("Commmands Saved:");
    oled.println(f);
    oled.display();  
}

void page2(){
      double peak = FFT.MajorPeak(vReal, SAMPLES, SAMPLING_FREQ);
      double peakAmplitude = FFT.MajorPeakAmplitude();
      //peakAmplitude=map(round(peakAmplitude),250,190000,0.12,3.3); 
     
      average = (sumAv-peakAmplitude)/(SAMPLES/2)-1;// sum-amplitude of the peak between the number of samples minus 1
      double voiceAv = sumAv/(SAMPLES/2);
      float porcent = (average/peakAmplitude) * 100;
      peakAmplitude=0.0000167588932*peakAmplitude+0.11581; // until the end of the calculations so that it does not affect
      Serial.print("SNR: ");
      Serial.print(porcent);
      Serial.println("%");
      //Find peak frequency and print peak
      Serial.print("Peak Frequency:                        "); 
      Serial.print(peak);     //Print out the most dominant frequency.
       Serial.println("Hz"); 
      Serial.print("Maximum Amplitude :                        "); 
      Serial.println(peakAmplitude);     //Print out the most dominant frequency.
      Serial.print("Average (noise): ");
      Serial.println(average);
      Serial.print("Average Samples Vreal FFT: ");
      Serial.println(voiceAv);
      Serial.print("num of times sumed: ");
      Serial.println(k);
      Serial.println("__________");


      //OLED
      oled.clearDisplay(); // clear display
      oled.setCursor(40, 0);
      oled.println("FFT");      
      oled.setCursor(0, 8);      
      oled.print("Peak F:"); 
      oled.print(peak);     //Print out the most dominant frequency.
      oled.println("Hz"); 
      oled.setCursor(0, 18);
      oled.print("Max Amp:"); 
      oled.print(peakAmplitude);     //Print out the most dominant frequency.
      oled.println("V"); 
      oled.setCursor(0, 28);      
      oled.print("AverageN:");
      oled.println(average);
      oled.setCursor(0, 38);      
      oled.print("AverageS:");
      oled.println(voiceAv);
      oled.setCursor(0, 48);      
      oled.print("num sums:");
      oled.println(SAMPLES/2);
      oled.display();              // display on OLED
}
