/**
 * Minimum code to send data from terabee 64 pixel sensor from arduino (Teensy 3.6) to a computer
 * ignore the CRC 
 */
#include <SPI.h>
//#include <SD.h>
#include <math.h>

// for the audio
#include <Audio.h>
#include <Wire.h>
//#include <SPI.h>
#include <SD.h>
#include <SerialFlash.h>


#include <EloquentTinyML.h>
#include "wine_nn.h"

#define NUMBER_OF_INPUTS 64
#define NUMBER_OF_OUTPUTS 4
#define TENSOR_ARENA_SIZE 16*1024

Eloquent::TinyML::TfLite<NUMBER_OF_INPUTS, NUMBER_OF_OUTPUTS, TENSOR_ARENA_SIZE> nn;

#define MAXROWS 5
#define CLASSES 4

uint8_t dataFrame [256];
uint16_t distance [128];   
//const int chipSelect = BUILTIN_SDCARD;
char dataStr[512] = "";
char buffer[7];
int monitor = 0;
int dataBuffer[MAXROWS][64];
float postData[MAXROWS][64];
float threshold = 0.85;
int finalOutput = 0;
float distRatio = 1000.0;
float voiceThres = 3;
//int lastResult = -1;


// for audio
AudioPlaySdWav           playWav1;
// Use one of these 3 output types: Digital I2S, Digital S/PDIF, or Analog DAC
AudioOutputI2S           audioOutput;
//AudioOutputSPDIF       audioOutput;
//AudioOutputAnalog      audioOutput;
//On Teensy LC, use this for the Teensy Audio Shield:
//AudioOutputI2Sslave    audioOutput;

AudioConnection          patchCord1(playWav1, 0, audioOutput, 0);
AudioConnection          patchCord2(playWav1, 1, audioOutput, 1);
AudioControlSGTL5000     sgtl5000_1;

// Use these with the Teensy 3.5 & 3.6 SD card
#define SDCARD_CS_PIN    BUILTIN_SDCARD
#define SDCARD_MOSI_PIN  11  // not actually used
#define SDCARD_SCK_PIN   13  // not actually used



void setup() {
    Serial.begin(0);             // Open serial port for USB
  Serial1.begin(3000000);      // Open serial port 1 
  while(!Serial){};
  //cardinit();
  nn.begin(wine_model);


//  for audio
//  Serial.begin(9600);

  // Audio connections require memory to work.  For more
  // detailed information, see the MemoryAndCpuUsage example
  AudioMemory(8);

  // Comment these out if not using the audio adaptor board.
  // This may wait forever if the SDA & SCL pins lack
  // pullup resistors
  sgtl5000_1.enable();
  sgtl5000_1.volume(0.9);

  SPI.setMOSI(SDCARD_MOSI_PIN);
  SPI.setSCK(SDCARD_SCK_PIN);
  if (!(SD.begin(SDCARD_CS_PIN))) {
    // stop here, but print a message repetitively
    while (1) {
      Serial.println("Unable to access the SD card");
      delay(500);
    }
  }
}


//void setup() {
//  Serial.begin(0);             // Open serial port for USB
//  Serial1.begin(3000000);      // Open serial port 1 
//  while(!Serial){};
//  //cardinit();
//  nn.begin(wine_model);
//}

//void cardinit(){
//  Serial.print("Initializing SD card...");
//  if(!SD.begin(BUILTIN_SDCARD)){
//    Serial.println("ititialization failed");
//    while(1);
//  }
//Serial.println("Card Initialized Successfully");
//Serial.println("-----------------------------\n");
//}

void playFile(const char *filename)
{
  Serial.print("Playing file: ");
  Serial.println(filename);

  // Start playing the file.  This sketch continues to
  // run while the file plays.
  playWav1.play(filename);

  // A brief delay for the library read WAV info
  delay(25);

  // Simply wait for the file to finish playing.
  while (playWav1.isPlaying()) {
    // uncomment these lines if you audio shield
    // has the optional volume pot soldered
    //float vol = analogRead(15);
    //vol = vol / 1024;
    // sgtl5000_1.volume(vol);
  }
}



// The main loop starts here
void loop() {
  if (Serial1.available() > 0) {                          // Send data only when you receive data  
    uint8_t inChar = Serial1.read();                        // Read the incoming byte and save it in the variable "inChar"
    if (inChar ==0x11) { 
       Serial1.readBytes(dataFrame,256);
       for(int i=0; i<64; i++) {
          dataFrame[i*2] &= ~(1UL << 7);
          dataFrame[(i*2)+1] &= ~(1UL << 7);
          distance[i] = (dataFrame[i*2]<<8) + dataFrame[(i*2)+1]; 
          dataBuffer[monitor][i] = distance[i];
          Serial.print(distance[i]);
       }
       Serial.print("\n");

       float mean[MAXROWS] = {0};
       float var[MAXROWS] = {0};
       if (monitor == 4) {
          // preprocessing
          for (int i = 0; i < MAXROWS; i++) {
            for (int j = 0; j < 64; j++) {
              mean[i] += dataBuffer[i][j];
              var[i] += dataBuffer[i][j] * dataBuffer[i][j];
            }
            mean[i] /= 64;
            var[i] /= 64;
            var[i] = var[i] - mean[i] * mean[i];
            var[i] = sqrt(var[i]);

            for (int j = 0; j < 64; j++) {
              postData[i][j] = (dataBuffer[i][j] - mean[i]) / var[i];
            }
            
          }
          // input to model

          float output[MAXROWS][CLASSES];
          int stat[CLASSES] = {0};
          for (int i = 0; i < MAXROWS; i++) {
             nn.predict((float *)postData[i], output[i]);
             int predictClass = 0;
             // 找最大
             for (int j = 1; j < CLASSES; j++) {
                if (output[i][j] > output[i][predictClass]) {
                  predictClass = j;
                }
             }
             // 通过阈值定分类结果
             if (output[i][predictClass] >= threshold) {
               stat[predictClass] += 1;
             }
          }

          for (int i = 0; i < MAXROWS; i++) {
              for (int j = 0; j < CLASSES; j++) {
                Serial.print(output[i][j]);
                Serial.print(" ");
              }
              //Serial.print(predictClass[i]);
              Serial.print("\n");
            }

          /*投票最多结果小于3，记为失败*/
//
//          stat[0] = 3;
//          stat[1] = 1;
//          stat[2] = 0;
//          stat[3] = 0;
          int final = 0;
          for (int i = 1; i < CLASSES; i++) {
              if (stat[i] >= stat[final]) {
                final = i;
              }
          }
          float finalDist = 0;
          if (stat[final] >= 3) {
            // 语音返回finalOuput类
            finalOutput = final;
            Serial.println("Final: ");
            Serial.print(finalOutput);
            Serial.print("\n");
            for (int i = 0; i < MAXROWS; i++) {
              finalDist += mean[i];
            }
            finalDist /= MAXROWS;
            finalDist /= distRatio;

            
            

           // finalOutput = 1;

          if (finalOutput == 0 && finalDist >= voiceThres) 
            {
              playFile("DOOR_FAR.WAV");
            }
            else if(finalOutput == 0 && finalDist < voiceThres){
              playFile("DOOR_CLOSE.WAV");
              }
            else if(finalOutput == 1 && finalDist >= voiceThres){
              playFile("BLOCK_FAR.WAV");
              }
              else if(finalOutput == 1 && finalDist < voiceThres){
              playFile("BLOCK_CLOSE.WAV");
              }
              else if(finalOutput == 2 && finalDist >= voiceThres){
              playFile("TRASH_FAR.WAV");
              }
              else if(finalOutput == 2 && finalDist < voiceThres){
              playFile("TRASH_CLOSE.WAV");
              }
            else if(finalOutput == 3 && finalDist >= voiceThres){
            playFile("STARI_FAR.WAV");
            }
            else if (finalOutput == 3  && finalDist < voiceThres){
              playFile("STAIR_CLOSE.WAV");
             }
            delay(4000);
          } else {
            //Serial.print("E");
            
            delay(500);
            // 不返回
          }
          
        
        
       }

       //send range data (64 integers) and ambient data (64 integers)
       //Serial.print("E");
       //Serial.print(",");
       //for(int i=0; i<64; i++) { 
       //   Serial.print(distance[i]);
       //   Serial.print(",");
       //}
       
       //Serial.println("");  
       monitor = (monitor + 1) % MAXROWS;         
    }//end if char 0x11
  }
}
