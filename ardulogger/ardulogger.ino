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
//   Standard "Amateur Programmer" disclaimer applies.  This code is guaranteed
//   to be sub-optimal, inconsistent and imperfect.  I will cheerfully refund
//   your money if you find the code quality to be perfect.


#define DEBUG true // Enable serial debug output.

// include the SD library:
#include <SPI.h>
#include <SD.h>

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
//  in the form: TxTemp = TxA * (TxValue * (5.0 / 1024.0)) + TxB
#define T1A 1.0
#define T1B 0.0
#define T2A 1.0
#define T2B 0.0
#define T3A 1.0
#define T3B 0.0

// Filename for logfile
#define LogFile "datalog.csv"

// Specify constructor for PCF8574 Backpack
LiquidCrystal_I2C lcd(I2C_ADDR,En_pin,Rw_pin,Rs_pin,D4_pin,D5_pin,D6_pin,D7_pin, Bl_pin, POSITIVE);

bool backlightStatus = true;

// Date and time functions using a DS1307 RTC connected via I2C and Wire lib
DS1307 rtc;

uint8_t lastSecond = 0;
uint8_t thisSecond = 0;

//SD Card chip select I/O pin
const int SDchipSelect = 10;

int T1minusPin = A0;  // Select the differential low pin for T1
int T1plusPin = A1;   // Select the differential high pin for T1
int T2Pin = A2;       // Select ground referenced analogue T2 pin
int T3Pin = A3;       // Select ground referenced analogue T3 pin
int PumpPin = 2;      // Digital input pin for AC mains detection module for circulation pump
int HeatPin = 3;      // Digital input pin for AC mains detection module for heater element

// Raw temperature values from A/D converter
int T1Value = 0;
int T2Value = 0;
int T3Value = 0;

// Pump and heater voltage presence flags
bool PumpFlag = false;
bool HeatFlag = false;

// Normalised temperature values after linear function adjustment
float T1Temp = 0.0;
float T2Temp = 0.0;
float T3Temp = 0.0;

// Previous values of all parameters for comparison
bool LastPumpFlag = false;
bool LastHeatFlag = false;

float LastT1Temp = 0.0;
float LastT2Temp = 0.0;
float LastT3Temp = 0.0;

// Minimum delta values for temperatures before triggering a loggable event
#define DeltaT1Temp 0.02
#define DeltaT2Temp 0.02
#define DeltaT3Temp 0.02

bool DeltaFlag = false;  // Flag to indicate an event crossed the logging threshold


char buf[24]; // buffer for print format string
char TimeStamp[24]; // buffer for current timestamp string

String LogEntry = ""; // Log entry string for serial and SD output


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
    Serial.println("?RTC is NOT running!");
#endif

    // sets the RTC to the date & time this sketch was compiled
    rtc.adjust(DateTime(__DATE__, __TIME__));
  }

  // Set input pins for pump and heater sensors.
  // If not using external pullup resistors, then use "INPUT_PULLUP" instead
  pinMode(PumpPin, INPUT);
  pinMode(HeatPin, INPUT);
  
  // Initialise LCD display area and set backlight
  lcd.begin(16,2);
  lcd.setBacklight(backlightStatus);

  // Intialise SD card.  See if the card is present and can be initialized:
  if (!SD.begin(SDchipSelect)) {
#ifdef DEBUG
    Serial.println("?Card failed or not present");
#endif
    // don't do anything more:
    return;
  }
#ifdef DEBUG
  Serial.println("Card initialized");
#endif

  // Write startup datetime stamp to SD card
  DateTime now = rtc.now();
  strncpy(buf,"YYYY-MM-DD hh:mm:ss\0",24);
  strncpy(TimeStamp, now.format(buf), 24);
  LogEntry = "//Init ";
  LogEntry += TimeStamp;
  writeLogEntry(LogFile, LogEntry);
    
} //End setup


void loop() {
    DateTime now = rtc.now();
    thisSecond = now.second();

    if (!(lastSecond == thisSecond)) { // Per second input scan loop
      lastSecond = thisSecond;

      // Format current timestamp into loggable CSV string
      strncpy(buf,",YYYY-MM-DD hh:mm:ss,\0", 24);
      strncpy(TimeStamp, now.format(buf), 24);

      // read the sensor values:
      T1Value = analogRead(T1plusPin) - analogRead(T1minusPin);
      T2Value = analogRead(T2Pin);
      T3Value = analogRead(T3Pin);
      PumpFlag = !digitalRead(PumpPin);
      HeatFlag = !digitalRead(HeatPin);

      // Convert analog input sensor value to temperature using linear equation constants:
      T1Temp = (T1A * (T1Value * (5.0 / 1024.0 ))) + T1B;
      T2Temp = (T2A * (T2Value * (5.0 / 1024.0 ))) + T2B;
      T3Temp = (T3A * (T3Value * (5.0 / 1024.0 ))) + T3B;

      // Pack log entry into string
      LogEntry = TimeStamp + String(T1Temp) + "," + String(T2Temp) + "," + String(T3Temp)
                + "," + String(PumpFlag) + "," + String(HeatFlag);           

      // Format data for LCD and display it
      lcd.setCursor ( 0, 0 );        
      strncpy(buf,"hh:mm:ss\0",24);
      lcd.print(now.format(buf));
      if (PumpFlag) {
        lcd.print(" P1");
        } else lcd.print(" P0");
      if (HeatFlag) {
        lcd.print(" H1  ");
        } else lcd.print(" H0  ");
      lcd.setCursor( 0, 1 );
      dtostrf(T1Temp, 6, 2, buf);
      lcd.print(buf);
      dtostrf(T2Temp, 5, 2, buf);
      lcd.print(buf);
      dtostrf(T3Temp, 5, 2, buf);
      lcd.print(buf);

      DeltaFlag = false;  // Start wih the assumption that there are no loggable events since last poll
      

      if (PumpFlag | LastPumpFlag) {
        DeltaFlag = true;
        LastPumpFlag = PumpFlag;
      }

      if (HeatFlag | LastHeatFlag) {
        DeltaFlag = true;
        LastHeatFlag = HeatFlag;
      }

      if (abs(T1Temp - LastT1Temp) >= DeltaT1Temp) {
        DeltaFlag = true;
        LastT1Temp = T1Temp;
      }
      
      if (abs(T2Temp - LastT2Temp) >= DeltaT2Temp) {
        DeltaFlag = true;
        LastT1Temp = T1Temp;
      }

      if (abs(T3Temp - LastT3Temp) >= DeltaT3Temp) {
        DeltaFlag = true;
        LastT1Temp = T1Temp;
      }

      if (DeltaFlag) {
        lcd.setCursor ( 15, 0 );
        lcd.print("*"); // Show loggable event flag on LCD
        writeLogEntry(LogFile, LogEntry);
      }
      
      
    } // Per second input scan loop end

    

    
  delay(50); // Delay 50 ms before smashing the RTC for another read cycle
  

} //End loop


// Appends string passed as stringBuffer to fileName.  Requires SD library
//  and expects SD card to be initialised with SD.begin(x).
//
//  Presence of #define DEBUG will write stringBuffer
//  to serial out and requires Serial.begin to be called before
//  invocation.
//
void writeLogEntry(String fileName, String printBuffer) {

    File dataFile = SD.open(fileName, FILE_WRITE);

  // if the file is available, write to it:
  if (dataFile) {
    dataFile.println(printBuffer);
    dataFile.close();
  }
  else {
#ifdef DEBUG    
    // if the file open failed, write error to debug console:
    Serial.println("?Error opening " + fileName);
#endif
  }
  
#ifdef DEBUG
    Serial.println(printBuffer);
#endif    

  
} // End writeLogEntry

