/* VOICE DETECTION PROJECT 
 *  
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

//con esta configuración va desde 10hz a 1khz exactamente
//#define SAMPLES 512              // Must be a power of 2, max512 creo
//#define SAMPLING_FREQ 2000 // Hz, must be 40000 or less due to ADC conversion time. Determines maximum frequency that can be analysed by the FFT Fmax=sampleF/2.

#define SAMPLES 512         //SAMPLES FFT. Must be a base 2 number. Max 128 for Arduino Uno.
#define SAMPLING_FREQ 8000   //Ts = Based on Nyquist, must be 2 times the highest expected frequency. Determines maximum frequency that can be analysed by the FFT  Fmax = sampleF/2
#define THRESHOLD 20
//#define AMPLITUDE 1000       // Depending on your audio source level, you may need to alter this value. Can be used as a 'sensitivity' control.
#define AMPLITUDE 500            // Depending on your audio source level, you may need to increase this value
#define NOISE 400           // Used as a crude noise filter, values below this are ignored
#define NAverage 12


#define SCREEN_WIDTH 128 // OLED width,  in pixels
#define SCREEN_HEIGHT 64 // OLED height, in pixels

// create an OLED display object connected to I2C
Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

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
                                       LCD
============================================================================================================*/

const int rs = 12, en = 11, d4 = 5, d5 = 4, d6 = 3, d7 = 2; 
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);;

//-------------------------------------------------------------------------------

/*==========================================================================================================
                                       FFT
============================================================================================================*/

// METHOD 1

//Sampling and FFT stuff

unsigned int samplingPeriod;
unsigned long microSeconds;

byte peak[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};              // The length of these arrays must be >= NUM_BANDS

const int number = round(SAMPLING_FREQ/SAMPLES);
 
double vReal[SAMPLES]; //create 2D vector of size SAMPLES to hold real values
double vImag[SAMPLES]; //create 2D vector of size SAMPLES to hold imaginary values
double datasAv[number]; //create vector of size SAMPLING_FREQ/SAMPLES to hold averages
double datas[number][SAMPLES]; 
double dReal[SAMPLES];
double dImag[SAMPLES];

arduinoFFT FFT = arduinoFFT();
//arduinoFFT FFT = arduinoFFT(vReal, vImag, SAMPLES, SAMPLING_FREQ);

//---------------------------------------------------------------------------//


/*==========================================================================================================
                                       Variables
============================================================================================================*/

int analogPin = 35; //Pin that reads the analogpin

char option[4][4]{ //DTMF Options
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};
//

double voice[12];
int j = 0;
int k = 0;
int counter = 0;
const int lim = 4;
double sum = 0;
double sum2 = 0;
double average = 0;
int c = 0;
int f = 0;
int comando=0; 

double peaks[number][SAMPLES/2];
double res[SAMPLES/2];
double command[3][SAMPLES/2];

boolean page = false;
unsigned long newTime, new2Time;

//--------------------------------------------------------------------------------

void setup() {
  //Set up SERIAL
  Serial.begin(9600);
  
  pinMode(18, INPUT_PULLUP);
 // pinMode(27, INPUT_PULLUP);
  pinMode(12, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
  //pinMode(15, INPUT_PULLUP);}//switch para cambiar de practica


  // initialize OLED display with I2C address 0x3C
  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("failed to start SSD1306 OLED"));
    while (1);
  }else
    Serial.println("OLED STARTED");
  delay(2000);         // wait two seconds for initializing

  oled.clearDisplay(); // clear display
  oled.setTextColor(WHITE);    // se tiene que configurar para que funicone
  oled.setTextSize(0.1);         // 1
  
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
  
  
      /*Perform FFT on samples*/
      //Compute FFT
      //FFT.Windowing(f, SAMPLES, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  //    FFT.Windowing(vReal, SAMPLES, FFT_WIN_TYP_RECTANGLE, FFT_FORWARD);
  //    FFT.Compute(vReal, vImag, SAMPLES, FFT_FORWARD);
  //    FFT.ComplexToMagnitude(vReal, vImag, SAMPLES);
  //    FFT.DCRemoval(vReal,SAMPLES);
      //PrintVector(vReal, SAMPLES >> 1, SCL_PLOT);
  
      
      /*FFT.Windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
      FFT.Windowing(vReal, SAMPLES, FFT_WIN_TYP_RECTANGLE, FFT_FORWARD)
      FFT.Compute(FFT_FORWARD);
      FFT.ComplexToMagnitude();
      FFT.DCRemoval(); // If my signal have a DC offset so this line remove that DC offset from the data
      */
      
       // Analyse FFT results
       // Each element represents a frequency bin and the value of each element represents the amplitude of the amount of that frequency
       
    k=0;
    sum=0;
    double peak = 0;
    double sumAverages;
    int temp;
      
    // We remove the noise and only retrieve the n fft values (in this case 4 values) of the n sampling_frequency/samples we need to store in an array //


//    
//    for (int i=0; i < number; i++){
//      for (int j=0; j<(SAMPLES/2); j++){       // Don't use sample 0 and only first SAMPLES/2 are usable. Each array element represents a frequency bin and its value the amplitude.
//          
//  //        Serial.print(i);
//  //        Serial.println(j);
//  //        Serial.print("RealSamples: ");  
//  //        Serial.println(FFT.GetAmplitudeFFT(k));
//          peaks[i][j] = GetFreqFFT(j);
//  //        Serial.print(i);
//  //        Serial.println(j);
//  //        Serial.println(GetFreqFFT(j));
//  //        sum += peak;     
//            
//          k++;
//        }  
//      k=0;
//        //datasAv[i] = sum;
//        //sumAverages += datasAv[i];

    
    // We avaerage each data corresponding to all the sampling_frequency/samples values and thus obtain in a 1D array of the average //
    
    for (int j=0; j < (SAMPLES/2); j++){ 
      for (int i=0; i < number; i++){
      
          // Don't use sample 0 and only first SAMPLES/2 are usable. Each array element represents a frequency bin and its value the amplitude.
         sum += datas[i][j];
          
        }  
        res[j] += sum/number;
        sum = 0;
      }  
  
    
  
    //SAVE DATA
   
    if(f < 3){
      c++;
      if (c == 10){
        for(int b=0; b<(SAMPLES/2); b++) // Guarda el x comando de la suma de los promedios de las SAMPLING_FREQ/SAMPLES FFT
         command[f][b]=res[b]/c;
        for (int j=0; j < (SAMPLES/2); j++) // Resetea el valor donde esta guardado la suma de los promedios de las SAMPLING_FREQ/SAMPLES FFT
          res[j] = 0;
        c = 0;
        f++;
      }
    }
  
     //READING AND CONFIRMATION
          
    
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
  
  
    //  aquí cada que no fueron 4 muestras, checar que sí se haya reiniciado bien la suma
  //  if (k > 0){  //antes era 0, pero más para que sean más de 4 muestras
  //
  //  }
    
  }
 
//  if (digitalRead(27) == LOW){ //RESET BUTTON
//     Serial.println("caca");
//    c = 0;
//    f = 0;
//    page = false;
//  }
  
  if (digitalRead(12) == LOW){ //FIRST PAGE OLED
    page = false;
  }
  
  if (digitalRead(14) == LOW){ //FIRST PAGE OLED
    page = true;
  }
   
}



/* FUNCTIONS  */

// 

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
     
      average = (sum-peakAmplitude)/k-1;//suma-amplitud del pico entre el numero de muestras menos 1
      double voiceAv = sum/k;
      float porcent = (average/peakAmplitude) * 100;
      peakAmplitude=0.0000167588932*peakAmplitude+0.11581;//hasta el final de los calculos para que no afecte
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
      oled.println(k);
      oled.display();              // display on OLED
}
