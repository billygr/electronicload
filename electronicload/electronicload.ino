/**
 * Electronic Load
 * Original design by Kerry D. Wong
 * http://www.kerrywong.com
 * October 2013
 
 * Code changes by UFN
 * http://theslowdiyer.wordpress.com
 * July/August 2015. Updates August 2019 before publishing.
 * SW version 0.5 to suit PCB version 0.9.
 
 Release notes:
 *** SW version 0.5: First published version. 
 * NOT YET DONE/VERIFIED!!:
 - Change the original SW to use an I2C LCD (started, but not verified)
 - Change pin assignments to suit new PCB connections (started, but not verified)
 - Change the code to only use two sets of MOS-FETs to ensure load calc is correct (ln. 165 and forward)
 - Optional: Set up and use AUX-pins on the board to measure fan speed, heat sink temeperature etc.
 
 *** PCB version 0.9: First published (untested) version.
 
*/

#include <SPI.h>
#include <Encoder.h>
#include <Wire.h> //I2C communication
#include <LiquidCrystal_PCF8574.h> //new library for I2C display with cheap PCF-8574 backpack

#define I2Cadr 0x27 // Defines the I2C address for the LCD - should be default for PCF8574 backpacks
#define LCDrows 2 // Defines the number of display rows available.
#define LCDcols 16 // Defines the number of display columns available.

//Voltage measurement on analog pin 3
const int pinLoadVoltage = A3;

//MCP4921 SPI
const int PIN_CS = 10;

//Control pins
const int BTN_RESET = 7;
const int BTN_RANGE_X2 = 6;
const int BTN_MODE = 5;
const int BTN_ENC = 4;

const int IDX_BTN_RESET = 0;
const int IDX_BTN_RANGE_X2 = 1;
const int IDX_BTN_MODE = 2;
const int IDX_BTN_ENC = 3;
const int LOOP_MAX_COUNT = 2000;

int DACGain = 1; //default gain 1xVref. 

int loadMode = 0; // 0: Constant Current, 1: Constant Power
const float EXT_REF_VOLTAGE = 0.333; // MPC4921 Reference voltage used, adjust it using the trimmer

// Voltage divider on A3 pin
//(calculated for max 60 Volts = > 5V ADC, R1=1K, R2=12K max ADC 4.615)
// Measure the actual values of the resistors you will use in KO
const float RB = 1.004;
const float RA = 11.91;
// RA is the one with the pin on ground, like the R2 in the voltage divider

// Setup of display and encoder
LiquidCrystal_PCF8574 lcd(I2Cadr);  //set up the LCD address
Encoder currentAdjEnc(2, 3); //encoder connected to pins 2 & 3

int buttons[]={BTN_RESET, BTN_RANGE_X2, BTN_MODE, BTN_ENC};
int buttonReadings[3];
int lastButtonStates[]={HIGH, HIGH, HIGH, HIGH};
int currentButtonStates[]={HIGH, HIGH, HIGH, HIGH};
long lastDebounceTime[3];

long oldEncPosition = -999;
long curEncPosition = 0;
int DACSetStep = 1;
int encoderValue = 0;
int loopCounter = 0;
int DACSetValue = 0;

long ADSum = 0;

float vLoad = 0.0;
float setPower = 0.0;
float setCurrent = 0.0;


//***START OF MAIN PROGRAM***START OF MAIN PROGRAM***START OF MAIN PROGRAM***START OF MAIN PROGRAM***START OF MAIN PROGRAM 

void setup()
{
    for (int i = 0 ; i < 4 ; i++) {
        pinMode(buttons[i], INPUT);
        digitalWrite(buttons[i], HIGH);
    }
    
    pinMode(PIN_CS, OUTPUT);
    SPI.begin();  
    SPI.setClockDivider(SPI_CLOCK_DIV2);

    lcd.begin(LCDcols, LCDrows);
    lcd.setBacklight(255); //turn backlight on
}

void getCurrentEncPosition()
{
    curEncPosition = currentAdjEnc.read() / 4;
    
    if (curEncPosition != oldEncPosition) {
        if (curEncPosition > oldEncPosition) {
            encoderValue += DACSetStep;
            
            if (encoderValue > 4095) encoderValue = 4095;
        } else {
            encoderValue -= DACSetStep;
            
            if (encoderValue < 0) encoderValue = 0;
        }
        
        oldEncPosition = curEncPosition;
        
        if (loadMode == 0) //Constant Current mode
        {
            setDACOutput(encoderValue);
        } else { //Constant Power mode
            setDACOutput(DACSetValue);
        }
    }
}

//Buffered DAC output
void setDACOutput(unsigned int val) 
{
    byte lByte = val & 0xff;
    //                      SHDN     GA            BUF 
    byte hByte = val >> 8 | 0x10 | DACGain << 5 | 1 << 6;
    
    PORTB &= 0xfb;
    SPI.transfer(hByte);
    SPI.transfer(lByte);
    PORTB |= 0x4;    
}

void displayStatus()
{   
    lcd.clear();     

    //average load voltage
    vLoad = ADSum * 1.0 / (float) LOOP_MAX_COUNT / 1024.0 * 5.0  * (RA+RB) / RB; //(RA+RB)/RB

    if (loadMode == 0) //Constant Current
    {
        float vSense = 1.0 * encoderValue / 4096.0 * EXT_REF_VOLTAGE;
        float i = 2 * 10 * vSense; // 3 sets of MOSFET in parallel, 0.1 ohm. FIXED: Change it to 2 sets of MOSFET
        if  (DACGain == 0) i *=2; // x2
    
        lcd.print("CI,I Set=");
    
        if (i < 1.0) {
            lcd.print(i * 1000,0);
            lcd.print(" mA");
        } else {
            lcd.print(i ,2);
            lcd.print(" A");
        }
    } else { //Constant Power
        setPower = (float) encoderValue / 20.0; //approximately 0-200W

        lcd.print("CP, P Set=");        
        if (vLoad > 0.5) {//minimum 0.5V
            setCurrent = setPower / (float) vLoad;
            
            //desired sense voltage. Since we have 3 ->> CHANGE TO USE TWO SETS OF MOSFETS!!
            //sets of MOSFETS, the results are devided
            //by 3 and multipled by the value of the sense
            //resistor
            float vSense = setCurrent / 2.0 * 0.1; //FIXED: Change it to 2 sets of MOSFET
          
            DACSetValue = (int) (vSense/EXT_REF_VOLTAGE * 4096.0 + 0.5);

            if (setCurrent < 10.0) {
                DACGain = 1;                
            } else {
                DACGain = 0;
                DACSetValue = (int) ((float) DACSetValue / 2.0 + 0.5);
            }                        
            
            if (setPower >= 100.0) {
                lcd.print(setPower, 1);
            }
            else {
               lcd.print(setPower, 2);     
            }
        } else {
            setPower = 0;
            lcd.print(setPower, 2);
        }
        
        setDACOutput(DACSetValue);
        lcd.print("W");
    } 
    
    lcd.setCursor(0, 1);
    lcd.print("LOAD V=");
    lcd.print(vLoad, 2);
    lcd.print(" V");        
}

//***START OF LOOP SECTION***START OF LOOP SECTION***START OF LOOP SECTION***START OF LOOP SECTION***START OF LOOP SECTION

void loop()
{
    int idx = 0;
    
    for (int i = 0 ; i < 4; i++) {
        buttonReadings[i] = digitalRead(buttons[i]);
        
        if (buttonReadings[i] != lastButtonStates[i]) lastDebounceTime[i] = millis();
        
        if (millis() - lastDebounceTime[i] > 50) { //debouncing the buttons
            if (currentButtonStates[i] != buttonReadings[i]) {
                currentButtonStates[i] = buttonReadings[i];                                
                
                //actions
                if (currentButtonStates[i] == LOW) {
                    switch (i) {
                        case IDX_BTN_RESET:
                        //reset output current to 0
                           encoderValue = 0;
                           DACSetValue = 0;
                        //was missing setting the actual output current 0
                           setDACOutput(0);
                           break; 
                        case IDX_BTN_RANGE_X2:
                        //switch between 100W/200W maximum power mode
                            DACGain = DACGain == 1? 0 : 1;
                            
                            if (loadMode == 0) {
                                setDACOutput(encoderValue);
                            }
                            break;
                        case IDX_BTN_MODE:
                        //switch between constant current and constant power
                            loadMode = loadMode == 1? 0 : 1;
                            break;
                        case IDX_BTN_ENC:
                        //cycle through different encoder steps: 1/10/100
                            DACSetStep *= 10;
                            
                            if (DACSetStep > 100) DACSetStep = 1;
                            break;
                    }
                }
            }
        }
        
        lastButtonStates[i] = buttonReadings[i];
    }        

    getCurrentEncPosition();    
    loopCounter++;
    
    // used to smooth out the analogRead results
    ADSum += analogRead(pinLoadVoltage); 
    
    if (loopCounter == LOOP_MAX_COUNT) {
        displayStatus();
        loopCounter = 0;
        ADSum = 0;
    }
}
