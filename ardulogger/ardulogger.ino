// ardulogger
//
//   Copyright 2017 by Phillip Yialeloglou
//
//   Licensed under the Apache License, Version 2.0 (the "License");
//   you may not use this file except in compliance with the License.
//   You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the License is distributed on an "AS IS" BASIS,
//   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//   See the License for the specific language governing permissions and
//   limitations under the License.
//


#define DEBUG true // Enable serial debug output.

// include the SD library:
#include <SPI.h>
#include <SD.h>

// set up variables using the SD utility library functions:
Sd2Card card;
SdVolume volume;
SdFile root;

// include the RTC, LCD and wire libraries
#include <Wire.h>
#include <RTClib.h>
#include <LiquidCrystal_I2C.h>

#define I2C_ADDR    0x27 //I2C address for QAPASS PCF8574 Backpack.
#define Rs_pin  0 // Register Select Pin
#define Rw_pin  1 // Read/Write Pin
#define En_pin  2 // Enable Pin
#define Bl_pin  3 // Backlight Pin

// LCD Data pins
#define D4_pin  4
#define D5_pin  5
#define D6_pin  6
#define D7_pin  7

// Calibration constants for converting volts to thermometer reading assuming linear equation
// in the form: TxTemp = TxA * (TxValue * (5.0 / 1024.0)) + TxB
#define T1A 1.0
#define T1B 0.0
#define T2A 1.0
#define T2B 0.0
#define T3A 1.0
#define T3B 0.0


// The number of seconds between input data readings
// Also defines file recording and LCD refresh rate
#define SampleIntervalSeconds 1

// Specify constructor for PCF8574 Backpack
LiquidCrystal_I2C lcd(I2C_ADDR,En_pin,Rw_pin,Rs_pin,D4_pin,D5_pin,D6_pin,D7_pin, Bl_pin, POSITIVE);

bool backlightStatus = true;

// Date and time functions using a DS1307 RTC connected via I2C and Wire lib
DS1307 rtc;

uint8_t lastSecond = 0;
uint8_t thisSecond = 0;

//SD Card chip select I/O pin
const int SDchipSelect = 10;

int T1minusPin = A0;    // select the differential low pin for T1
int T1plusPin = A1;     // select the differential high pin for T1
int T2Pin = A2;         // select ground referenced analogue T2 pin
int T3Pin = A3;         // select ground referenced analogue T3 pin
int PumpPin = 2;        // Digital input pin for AC mains detection module for circulation pump
int HeatPin = 3;      // Digital input pin for AC mains detection module for heater element

int T1Value = 0;
int T2Value = 0;
int T3Value = 0;
bool PumpFlag = false;
bool HeatFlag = false;

float T1Temp = 0.0;
float T2Temp = 0.0;
float T3Temp = 0.0;


char buf[50]; // buffer for print format string


void setup() {

#ifdef DEBUG
// Open serial communications and wait for port to open:
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }
#endif  
  
#ifdef AVR
  Wire.begin();
#else
  Wire1.begin(); // Shield I2C pins connect to alt I2C bus on Arduino Due
#endif
  rtc.begin();

  //Set RTC if not already running.
  //  For this horrible kludge to work, you need to first invalidate the RTC
  //  by removing the battery and then re-inserting it.  Compile and immediately
  //   upload the script.  The __DATE__ and __TIME__ parameters (set to the script
  //   compilation time) will then be used to re-set the RTC.
  //
  // Success will be signalled in debug mode by the serial console proclaiming
  //  "RTC is NOT running!", followed by the almost correct time being displayed
  //  on the LCD.
  
  if (! rtc.isrunning()) {
    
#ifdef DEBUG
    Serial.println("RTC is NOT running!");
#endif

    // sets the RTC to the date & time this sketch was compiled
    rtc.adjust(DateTime(__DATE__, __TIME__));
  }

  // Set input pins for pump and heater sensors.
  // If not using external pullup resistors, then use "INPUT_PULLUP" instead
  pinMode(PumpPin, INPUT);
  pinMode(HeatPin, INPUT);
  

  lcd.begin(16,2);
  // Temporary code to draw relay and temp state for layout testing           
  lcd.setBacklight(backlightStatus);
//  lcd.setCursor (8,0);   
//  lcd.print(" P:0 H:0");

  // we'll use the initialization code from the utility libraries
  // since we're just testing if the card is working!
  if (!card.init(SPI_HALF_SPEED, SDchipSelect)) {
#ifdef DEBUG
    Serial.println("?SD card initialization failed");
#endif
return;
  } else {
#ifdef DEBUG
    Serial.println("SD card initialised");
#endif
  }

  // Check SD card for valid partition
  if (!volume.init(card)) {
#ifdef DEBUG
    Serial.println("?SD card FAT partition not found");
#endif
  return;
  }
    
}


void loop(void) {
    DateTime now = rtc.now();
    thisSecond = now.second();

    if (!(lastSecond == thisSecond)) { // Scan inputs and update display once a second
      lastSecond = thisSecond;
      // read the sensor values:
      T1Value = analogRead(T1plusPin) - analogRead(T1minusPin);
      T2Value = analogRead(T2Pin);
      T3Value = analogRead(T3Pin);
      PumpFlag = digitalRead(PumpPin);
      HeatFlag = digitalRead(HeatPin);

      // Convert analog read sensor values to temperatures:
      T1Temp = (T1A * (T1Value * (5.0 / 1024.0 ))) + T1B;
      T2Temp = (T2A * (T2Value * (5.0 / 1024.0 ))) + T2B;
      T3Temp = (T3A * (T3Value * (5.0 / 1024.0 ))) + T3B;
      
#ifdef DEBUG
      strncpy(buf,"YYYYMMDD hhmmss \0",50);
      Serial.print(now.format(buf));
      Serial.print(T1Temp); Serial.print(" ");
      Serial.print(T2Temp); Serial.print(" ");
      Serial.print(T3Temp); Serial.print(" ");
      Serial.print(PumpFlag); Serial.print(" ");
      Serial.println(HeatFlag);
      
#endif

      lcd.setCursor ( 0, 0 );        
      strncpy(buf,"hh:mm:ss\0",50);
      lcd.print(now.format(buf));
      if (PumpFlag) {
        lcd.print(" P:0");
        } else lcd.print(" P:1");
      if (HeatFlag) {
        lcd.print(" H:0");
        } else lcd.print(" H:1");
      lcd.setCursor( 0, 1 );
      dtostrf(T1Temp, 6, 2, buf);
      lcd.print(buf);
      dtostrf(T2Temp, 5, 2, buf);
      lcd.print(buf);
      dtostrf(T3Temp, 5, 2, buf);
      lcd.print(buf);
    }
 

    delay(50); // 50 ms delay before smashing RTC for another read cycle
  

}
