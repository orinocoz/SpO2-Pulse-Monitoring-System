/* Imports */
#include<Arduino.h>
#include<Wire.h> // For I2C communication
#include "MAX30105.h" // Core functions of the 
#include "spo2_algorithm.h" // Header file for SpO2 calculation
#include "heartRate.h" // Header file for Heart rate calculation
#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h> // Header for Blynk

/* MACROS */
#define LED D3

/* Objects & Variables */
MAX30105 pox; // Pulse Oximeter Objects
String lcdMessage;
// Variables for SpO2
uint32_t irBuffer[100]; //infrared LED sensor data
uint32_t redBuffer[100];  //red LED sensor data
const int32_t bufferLength = 100; // buffer length of 100 stores 4 seconds of samples running at 25sps
int32_t spo2; //SPO2 value
int8_t validSPO2; //indicator to show if the SPO2 calculation is valid
int32_t heartRate; //heart rate value
int8_t validHeartRate; //indicator to show if the heart rate calculation is valid
// Variables for Heart Rate
int setSize = 50;
const byte RATE_SIZE = 4; //Increase this for more averaging. 4 is good.
byte rates[RATE_SIZE]; //Array of heart rates
byte rateSpot = 0;
long lastBeat = 0; //Time at which the last beat occurred
float beatsPerMinute;
int beatAvg;
bool notifyBPM = false;
// You should get Auth Token in the Blynk App.
// Go to the Project Settings (nut icon).
// char blynkAuth[] = "NaZgjhHT4oxT5rT3tFoE4WkeRxegj8RQ"; // Rahul Bera's project auth token
char blynkAuth[] = "X45G8bdHeq8kE4eggJeKm77OCe6NqFlu"; // Prakrit Raj's project auth token
BlynkTimer sendTimer;
WidgetLCD lcd(V5); // Configure Virtual Pin V5 of Blynk for LCD
// Variables for ESP8266
// Set password to "" for open networks.
char ssid[] = "iBall-Baton";
char pass[] = "ARcHEs1952";

/* Custom Functions */
void getHeartRate(){
  for (int i = 0; i < setSize; i++){  
    long irValue = pox.getIR();
    if (checkForBeat(irValue)) {
      //We sensed a beat!
      long delta = millis() - lastBeat;
      lastBeat = millis();

      beatsPerMinute = 60 / (delta / 1000.0);

      if (beatsPerMinute < 255 && beatsPerMinute > 20) {
        rates[rateSpot++] = (byte)beatsPerMinute; //Store this reading in the array
        rateSpot %= RATE_SIZE; //Wrap variable

        //Take average of readings
        beatAvg = 0;
        for (byte x = 0 ; x < RATE_SIZE ; x++)
          beatAvg += rates[x];
        beatAvg /= RATE_SIZE;
      }
      /* Debugging */
      Serial.print("IR=");
      Serial.print(irValue);
      Serial.print("\tBPM=");
      Serial.print(beatsPerMinute);
      Serial.print("\tAvg BPM=");
      Serial.println(beatAvg); /* */
      lcdMessage = "Scanning...";
    }
    if (irValue < 50000){
      Serial.println("No finger?");
      lcdMessage = "No finger?";
    }
  }
}

void getSpO2(){
  //dumping the first 25 sets of samples in the memory and shift the last 75 sets of samples to the top
  for (byte i = 25; i < 100; i++){
    redBuffer[i - 25] = redBuffer[i];
    irBuffer[i - 25] = irBuffer[i];
  }

  //take 25 sets of samples before calculating the heart rate.
  for (byte i = 75; i < 100; i++){
    while (pox.available() == false) //do we have new data?
      pox.check(); //Check the sensor for new data

    redBuffer[i] = pox.getRed();
    irBuffer[i] = pox.getIR();
    pox.nextSample(); //We're finished with this sample so move to next sample
  }
  //After gathering 25 new samples recalculate HR and SP02
  maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, 
  &spo2, &validSPO2, &heartRate, &validHeartRate);

  //send samples and calculation result to terminal program through UART
  Serial.print(F("HR="));
  Serial.print(heartRate, DEC);

  Serial.print(F("\tHRvalid="));
  Serial.print(validHeartRate, DEC);

  Serial.print(F("\tSPO2="));
  Serial.print(spo2, DEC);

  Serial.print(F("\tSPO2Valid="));
  Serial.println(validSPO2, DEC);
}

void sendToBlynkServer(){
  notifyBPM = notifyBPM | (beatAvg > 50);
  Blynk.virtualWrite(V0, beatsPerMinute);
  Blynk.virtualWrite(V1, beatAvg);
  if(validSPO2==1)
    Blynk.virtualWrite(V2, spo2);
  lcd.clear();
  lcd.print(0, 0, lcdMessage);
  if(notifyBPM){
    if(beatAvg < 40 || beatAvg > 100)
      Blynk.notify("Your patient's BPM is "+(String)beatAvg+"!");
    if(validSPO2 == 1 && spo2 < 80)
      Blynk.notify("Your patient's Blood Oxygen Level is "+(String)spo2+"!");
  }
}

void blinkLED(int count = 3) {
  int ledStatus = HIGH;
  for(int i = 0; i < 2*count; i++, ledStatus = !ledStatus ){
    digitalWrite(LED, ledStatus);
    delay(500);
  }
}

/* Driver Code */
void setup(){
  pinMode(LED, OUTPUT); // Pinmode for LED
  Serial.begin(115200); // Initialize Serial
  blinkLED(5); // Blink LED for 5 seconds to signal start of initialization
  Serial.println("Initializing...");
  // Initialize sensor
  if (!pox.begin(Wire, I2C_SPEED_FAST)) { //Use default I2C port, 400kHz speed
    Serial.println("MAX30102 Sensor was not found. Please check wiring/power.\nTry pressing the Reset key");
  } else {
    /* Configure the sensor */
    byte ledBrightness = 60; //Options: 0=Off to 255=50mA
    byte sampleAverage = 4; //Options: 1, 2, 4, 8, 16, 32
    byte ledMode = 2; //Options: 1 = Red only, 2 = Red + IR, 3 = Red + IR + Green
    byte sampleRate = 100; //Options: 50, 100, 200, 400, 800, 1000, 1600, 3200
    int pulseWidth = 411; //Options: 69, 118, 215, 411
    int adcRange = 4096; //Options: 2048, 4096, 8192, 16384
    pox.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);

    Serial.println("Setup initialized! Enter any key to continue...");
    blinkLED(3); // Blink LED for 3 seconds to mark initialization
    while (Serial.available() == 0) ; //wait until user presses a key
    Serial.read();
  }

  Blynk.begin(blynkAuth, ssid, pass);
  // Blynk cloud server doesn't handle inline delays well
  // Hence, timer is used
  // Reason: http://help.blynk.cc/en/articles/2091699-keep-your-void-loop-clean
  sendTimer.setInterval(250L, sendToBlynkServer);

  /* Initial Collection of Data */
  Serial.println("Collecting Data...");
  //read the first 100 samples, and determine the signal range
  for (byte i = 0 ; i < bufferLength ; i++){
    while (pox.available() == false) //do we have new data?
      pox.check(); //Check the sensor for new data

    redBuffer[i] = pox.getRed();
    irBuffer[i] = pox.getIR();
    pox.nextSample(); //We're finished with this sample so move to next sample
    // Serial.print(F("red="));
    // Serial.print(redBuffer[i], DEC);
    // Serial.print(F(", ir="));
    // Serial.println(irBuffer[i], DEC);
  }
  Serial.println("Data Collected!");
}

void loop(){
  Blynk.run();
  sendTimer.run();
  getHeartRate();
  getSpO2();
}