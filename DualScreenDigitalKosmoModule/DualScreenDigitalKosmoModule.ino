#include <SPI.h>
#include <Wire.h>
#include <Streaming.h>
//OLED Stuff
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
Adafruit_SSD1306 displayA(128, 64, &Wire);
Adafruit_SSD1306 displayB(128, 64, &Wire);
//Encoder Stuff
#include <Encoder.h>
Encoder Enc1(18, 19);
Encoder Enc2(2, 3);
long oldPosition[2] = {0, 0};
bool encoderTurn[4] = {0, 0, 0, 0};
//DAC Stuff
#include "MCP_DAC.h"
MCP4922 MCP;  // HW SPI
//Pins
#define buttonCount 15
#define LEDCount 8
const int rgbPins[6][3] = { {35, 36, 37}, {32, 33, 34}, {29, 30, 31}, {26, 27, 28}, {23, 24, 25}, {16, 17, 22} };
const int ledPins[LEDCount] = {6, 7, 8, 9, 10, 11, 12, 13};

byte actionLayer = 0;
bool drawScreen[2] = {true, true};

const int buttonPins[buttonCount] = {38, 39, 40, 41, 42, 43, 44, 45, 46, 47, A5, A6, A7, A8, A9};
bool buttonState[buttonCount];
bool buttonWasPressed[buttonCount];
bool lastButtonState[buttonCount];
unsigned long lastDebounceTime;
unsigned long debounceDelay = 30;

int totalStepCount[2] = {7, 7};  //total amount of steps are in the sequence before repeating
int currentStep[2] = {0, 0};     //what step is the sequence currently on
boolean currentlyEditingStep[2] = {false, false}; //what value are you changing 0==editing step 2==editing step value 3==gate time 4==gate toggle
int editingStep[2] = {0, 0};     //which step are you editing
int stepVales[128][2];           //the big one. this is where are the values for every step are stored
bool stepGates[128][2];

int clockMode[2] = {1, 1};       //where should the clock come from? 0=Internal 1=OFF 2=External 3=Sync
bool reverseSequance[2] = {false, false}; //shoud the sequence reverse

float internalClockSpeed = 90; //internal Clock Speed in beats per minute
unsigned long lastInternalClock = 0; //last time the internal clock signal ticked
#define leastInternalClockSpeed 30
#define maxInternalClockSpeed 500
#define maxSteps 127
#define minSteps 2
#define ADCMAX 4095

unsigned long lastSerialPrint = 0;
#define serialPrintInterval -1 //set to -1 to disable

const int rangeSelectPins[2] = {15, 14};
bool rangeSelect[2] = {0, 0};

const int gatePins[2] = {5, 4};
bool gateTrigger[2] = {0, 0};
int gateTime[2] = {50, 50};
unsigned long gateTriggerTime[2] = {0, 0};
#define gateMinTime 0
#define gateMaxTime 2000
bool noClockGates = true;

const int clockInputPins[2] = {A0, A1};
bool clockInputState[2];
bool lastClockInputState[2];
unsigned long lastClockInputTime;
unsigned long clockInputDebounceDelay = 3;

unsigned long lastScreenDraw = 0;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  //Setup OLED
  displayA.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  displayB.begin(SSD1306_SWITCHCAPVCC, 0x3D);
  displayA.clearDisplay();
  displayB.clearDisplay();
  displayA.setTextSize(1);
  displayB.setTextSize(1);
  displayA.setTextColor(SSD1306_WHITE);
  displayB.setTextColor(SSD1306_WHITE);
  displayA.setCursor(10, 10);     // Start at top-left corner
  displayB.setCursor(10, 10);     // Start at top-left corner
  displayA.println(F("Left Display"));
  displayB.println(F("Right Display"));
  displayA.display();
  displayB.display();
  //dac
  MCP.begin(53);
  //Set Pins
  for (byte i = 0; i < buttonCount; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
    buttonWasPressed[i] = false;
  }
  for (byte i = 0; i < LEDCount; i++) {
    pinMode(ledPins[i], OUTPUT);
    digitalWrite(ledPins[i], HIGH);
    delay(80);
    digitalWrite(ledPins[i], LOW);
  }
  for (byte i = 0; i < 6; i++) {
    for (byte u = 0; u < 3; u++) {
      pinMode(rgbPins[i][u], OUTPUT);
      digitalWrite(rgbPins[i][u], HIGH);
      delay(80);
      digitalWrite(rgbPins[i][u], LOW);
    }
  }
  for (byte i = 0; i < 128; i++) {
    for (byte u = 0; u < 2; u++) {
      stepGates[i][u] = HIGH;
    }
  }
  for (byte i = 0; i < 2; i++) {
    pinMode(rangeSelectPins[i], OUTPUT);
    digitalWrite(rangeSelectPins[i], rangeSelect[i]);
  }
  for (byte i = 0; i < 2; i++) {
    pinMode(gatePins[i], OUTPUT);
    digitalWrite(gatePins[i], LOW);
  }
  updateRangeSelectLeds();
  updateclockModeLeds();
}


void loop() {
  //read inputs
  readButtons();
  buttonActions();
  readEncoders();
  for (byte i = 0; i < 4; i++) {
    if (encoderTurn[i] == true) {
      encoderActions(i);
      encoderTurn[i] = false;
    }
  }
  wrapCursor();
  //debug serial print
  if ((millis() - lastSerialPrint) > serialPrintInterval) {
    Serial << F("Total:") << totalStepCount[0] + 1 << F(" Editing:") << editingStep[0] + 1 << F(" Value:") << stepVales[editingStep[0]][0] << F(" Current:") << currentStep[0] + 1 << F(" Value:") << stepVales[currentStep[0]][0] << F(" | Total:") << totalStepCount[0] + 1 << F(" Editing:") << editingStep[1] + 1 << F(" Value:") << stepVales[editingStep[1]][1] << F(" Current:") << currentStep[1] + 1 << F(" Value:") << stepVales[currentStep[1]][1] << endl;
    lastSerialPrint = millis();
  }

  for (byte i = 0; i < 2; i++) {
    if (clockMode[i] == 1) {
      currentStep[i] = editingStep[i];
    }
  }
  internalClock();
  externalClock();
  for (byte i = 0; i < 2; i++) {
    if (drawScreen[i] == true) {
      drawScreen[i] = false;
      if (actionLayer == 0 && i == 0) {
        drawSequancerMainPage(0);
      }
      if (actionLayer == 0 && i == 1) {
        drawSequancerMainPage(1);
      }
      else if (actionLayer == 1) {
        drawLeftStepCount();
      }
      else if (actionLayer == 2) {
        drawRightStepCount();
      }
      else if (actionLayer == 3) {
        drawClockSpeed();
      }
      else if (actionLayer == 4) {
        drawClockMultiply();
      }
      else if (actionLayer == 5) {
        drawLeftGateTime();
      }
      else if (actionLayer == 6) {
        drawRightGateTime();
      }
    }
  }
  writeOutputs();
}


void externalClock() {
  for (byte i = 0; i < 2; i++) {
    int raw = analogRead(clockInputPins[i]);
    int reading;
    if (raw >= 300) {
      reading = HIGH;
    }
    else {
      reading = LOW;
    }
    digitalWrite(ledPins[((!i + 1) * 4) - 4], reading);
    if (reading != lastClockInputState[i]) {
      lastClockInputTime = millis();
    }
    if ((millis() - lastClockInputTime) > clockInputDebounceDelay) {
      if (reading != clockInputState[i]) {
        clockInputState[i] = reading;
        if (clockInputState[i] == HIGH) {
          if (clockMode[i] == 2) {
            if (reverseSequance[i] == true) {
              currentStep[i]--;
              triggerGate(i, 1);
            }
            else {
              currentStep[i]++;
              triggerGate(i, 0);
            }
          }
          if (clockMode[1] == 3 && clockMode[0] == 2 && i == 0) {
            if (reverseSequance[1] == true) {
              currentStep[1]--;
              triggerGate(1, 1);
            }
            else {
              currentStep[1]++;
              triggerGate(1, 0);
            }
            if (currentStep[1] > totalStepCount[1]) {
              currentStep[1] = 0;
            }
            else if (currentStep[1] < 0) {
              currentStep[1] = totalStepCount[1];
            }
          }
        }
      }
    }
    lastClockInputState[i] = reading;
  }
}


void writeOutputs() {
  for (byte i = 0; i < 2; i++) {
    if (clockMode[i] == 1 && noClockGates == true) {
      digitalWrite(gatePins[i], HIGH);
      digitalWrite(ledPins[((!i + 1) * 4) - 2], HIGH);
      digitalWrite(rgbPins[((i + 1) * 3) - 2][0], LOW);
      digitalWrite(rgbPins[((i + 1) * 3) - 2][1], LOW);
      digitalWrite(rgbPins[((i + 1) * 3) - 2][2], HIGH);
    }
    else if (gateTrigger[i] == 1) {
      if (millis() - gateTriggerTime[i] > gateTime[i]) {
        digitalWrite(gatePins[i], LOW);
        digitalWrite(ledPins[((!i + 1) * 4) - 2], LOW);
        digitalWrite(rgbPins[((i + 1) * 3) - 2][0], LOW);
        digitalWrite(rgbPins[((i + 1) * 3) - 2][1], LOW);
        digitalWrite(rgbPins[((i + 1) * 3) - 2][2], LOW);
        gateTrigger[i] = 0;
      }
    }
    else {
      digitalWrite(gatePins[i], LOW);
      digitalWrite(ledPins[((!i + 1) * 4) - 2], LOW);
      digitalWrite(rgbPins[((i + 1) * 3) - 2][0], LOW);
      digitalWrite(rgbPins[((i + 1) * 3) - 2][1], LOW);
      digitalWrite(rgbPins[((i + 1) * 3) - 2][2], LOW);
    }
  }
  MCP.fastWriteB(stepVales[currentStep[0]][0]);
  analogWrite(ledPins[7], map(stepVales[currentStep[0]][0], 0, ADCMAX, 0, 255));
  MCP.fastWriteA(stepVales[currentStep[1]][1]);
  analogWrite(ledPins[3], map(stepVales[currentStep[1]][1], 0, ADCMAX, 0, 255));
}


void triggerGate(byte whichGate, byte Direction) {
  if (currentStep[whichGate] > totalStepCount[whichGate]) {
    currentStep[whichGate] = 0;
  }
  else if (currentStep[whichGate] < 0) {
    currentStep[whichGate] = totalStepCount[whichGate];
  }
  writeOutputs();
  gateTrigger[whichGate] = HIGH;
  if (stepGates[currentStep[whichGate]][whichGate] == HIGH) {
    digitalWrite(gatePins[whichGate], HIGH);
    digitalWrite(ledPins[((!whichGate + 1) * 4) - 2], HIGH);
    if (Direction == 1) {
      digitalWrite(rgbPins[((whichGate + 1) * 3) - 2][0], HIGH);
      digitalWrite(rgbPins[((whichGate + 1) * 3) - 2][1], LOW);
      digitalWrite(rgbPins[((whichGate + 1) * 3) - 2][2], LOW);
    }
    else {
      digitalWrite(rgbPins[((whichGate + 1) * 3) - 2][0], LOW);
      digitalWrite(rgbPins[((whichGate + 1) * 3) - 2][1], HIGH);
      digitalWrite(rgbPins[((whichGate + 1) * 3) - 2][2], LOW);
    }
  }
  gateTriggerTime[whichGate] = millis();
  if (actionLayer == 0) {
    drawScreen[whichGate] = true;
  }
}


void encoderActions(byte WhichTurn) {
  drawScreen[0] = true;
  drawScreen[1] = true;
  if (actionLayer == 0) {
    if (buttonState[12] == false || buttonState[13] == false || buttonState[14] == false) {
      if (WhichTurn == 0) {
        stepVales[editingStep[0]][0] = stepVales[editingStep[0]][0] - 100;
      }
      else if (WhichTurn == 1) {
        stepVales[editingStep[0]][0] = stepVales[editingStep[0]][0] + 100;
      }
      else if (WhichTurn == 2) {
        stepVales[editingStep[1]][1] = stepVales[editingStep[1]][1] - 100;
      }
      else if (WhichTurn == 3) {
        stepVales[editingStep[1]][1] = stepVales[editingStep[1]][1] + 100;
      }
    }
    else {
      if (WhichTurn == 0) {
        stepVales[editingStep[0]][0] = stepVales[editingStep[0]][0] - 1;
      }
      else if (WhichTurn == 1) {
        stepVales[editingStep[0]][0] = stepVales[editingStep[0]][0] + 1;
      }
      else if (WhichTurn == 2) {
        stepVales[editingStep[1]][1] = stepVales[editingStep[1]][1] - 1;
      }
      else if (WhichTurn == 3) {
        stepVales[editingStep[1]][1] = stepVales[editingStep[1]][1] + 1;
      }
    }
    if (stepVales[editingStep[0]][0] > ADCMAX) {
      stepVales[editingStep[0]][0] = 0;
    }
    else if (stepVales[editingStep[0]][0] < 0) {
      stepVales[editingStep[0]][0] = ADCMAX;
    }
    if (stepVales[editingStep[1]][1] > ADCMAX) {
      stepVales[editingStep[1]][1] = 0;
    }
    else if (stepVales[editingStep[1]][1] < 0) {
      stepVales[editingStep[1]][1] = ADCMAX;
    }
  }

  if (actionLayer == 1) {
    if (buttonState[12] == false || buttonState[13] == false || buttonState[14] == false) {
      if (WhichTurn == 0) {
        totalStepCount[0] = totalStepCount[0] - 10;
      }
      else if (WhichTurn == 1) {
        totalStepCount[0] = totalStepCount[0] + 10;
      }
    }
    else {
      if (WhichTurn == 0) {
        totalStepCount[0] = totalStepCount[0] - 1;
      }
      else if (WhichTurn == 1) {
        totalStepCount[0] = totalStepCount[0] + 1;
      }
    }
    if (totalStepCount[0] > maxSteps) {
      totalStepCount[0] = minSteps;
    }
    else if (totalStepCount[0] < minSteps) {
      totalStepCount[0] = maxSteps;
    }
  }

  if (actionLayer == 2) {
    if (buttonState[12] == false || buttonState[13] == false || buttonState[14] == false) {
      if (WhichTurn == 2) {
        totalStepCount[1] = totalStepCount[1] - 10;
      }
      else if (WhichTurn == 3) {
        totalStepCount[1] = totalStepCount[1] + 10;
      }
    }
    else {
      if (WhichTurn == 2) {
        totalStepCount[1] = totalStepCount[1] - 1;
      }
      else if (WhichTurn == 3) {
        totalStepCount[1] = totalStepCount[1] + 1;
      }
    }
    if (totalStepCount[1] > maxSteps) {
      totalStepCount[1] = minSteps;
    }
    else if (totalStepCount[1] < minSteps) {
      totalStepCount[1] = maxSteps;
    }
  }

  if (actionLayer == 3) {
    if (buttonState[12] == false || buttonState[13] == false || buttonState[14] == false) {
      if (WhichTurn == 0) {
        internalClockSpeed = internalClockSpeed - 10;
      }
      else if (WhichTurn == 1) {
        internalClockSpeed = internalClockSpeed + 10;
      }
    }
    else {
      if (WhichTurn == 0) {
        internalClockSpeed = internalClockSpeed - 1;
      }
      else if (WhichTurn == 1) {
        internalClockSpeed = internalClockSpeed + 1;
      }
    }
    if (internalClockSpeed < leastInternalClockSpeed) {
      internalClockSpeed = maxInternalClockSpeed;
    }
    if (internalClockSpeed > maxInternalClockSpeed) {
      internalClockSpeed = leastInternalClockSpeed;
    }
  }

  if (actionLayer == 5) {
    if (buttonState[12] == false || buttonState[13] == false || buttonState[14] == false) {
      if (WhichTurn == 0) {
        gateTime[0] = gateTime[0] - 100;
      }
      else if (WhichTurn == 1) {
        gateTime[0] = gateTime[0] + 100;
      }
    }
    else {
      if (WhichTurn == 0) {
        gateTime[0] = gateTime[0] - 1;
      }
      else if (WhichTurn == 1) {
        gateTime[0] = gateTime[0] + 1;
      }
    }
    if (gateTime[0] > gateMaxTime) {
      gateTime[0] = gateMinTime;
    }
    else if (gateTime[0] < gateMinTime) {
      gateTime[0] = gateMaxTime;
    }
  }

  if (actionLayer == 6) {
    if (buttonState[12] == false || buttonState[13] == false || buttonState[14] == false) {
      if (WhichTurn == 2) {
        gateTime[1] = gateTime[1] - 100;
      }
      else if (WhichTurn == 3) {
        gateTime[1] = gateTime[1] + 100;
      }
    }
    else {
      if (WhichTurn == 2) {
        gateTime[1] = gateTime[1] - 1;
      }
      else if (WhichTurn == 3) {
        gateTime[1] = gateTime[1] + 1;
      }
    }
    if (gateTime[1] > gateMaxTime) {
      gateTime[1] = gateMinTime;
    }
    else if (gateTime[1] < gateMinTime) {
      gateTime[1] = gateMaxTime;
    }
  }

}


void buttonActions() {
  for (byte i = 0; i < buttonCount; i++) {
    if (buttonWasPressed[i] == true) {
      Serial << F("actionLayer: ") << actionLayer << F(" Button: ") << i << endl;
      buttonWasPressed[i] = false;
      drawScreen[0] = true;
      drawScreen[1] = true;
      if (actionLayer == 0) {
        switch (i) {
          case 0:
            if (buttonState[2] == LOW) {
              currentStep[0]--;
              if (currentStep[0] < 0) {
                currentStep[0] = totalStepCount[0];
              }
            }
            else if (buttonState[6] == LOW) {
              rangeSelect[0] = LOW;
              digitalWrite(rangeSelectPins[0], rangeSelect[0]);
              updateRangeSelectLeds();
            }
            else {
              if (buttonState[12] == false) {
                editingStep[0] = editingStep[0] - 10;
              }
              else {
                editingStep[0]--;
              }
            }
            break;
          case 1:
            if (buttonState[2] == LOW) {
              currentStep[0]++;
            }
            else if (buttonState[6] == LOW) {
              rangeSelect[0] = HIGH;
              digitalWrite(rangeSelectPins[0], rangeSelect[0]);
              updateRangeSelectLeds();
            }
            else {
              if (buttonState[12] == false) {
                editingStep[0] = editingStep[0] + 10;
              }
              else {
                editingStep[0]++;
              }
            }
            break;
          case 2:
            if (buttonState[11] == LOW) {
              stepGates[editingStep[0]][0] = !stepGates[editingStep[0]][0];
            }
            break;
          case 3:
            if (buttonState[5] == LOW) {
              currentStep[1]--;
              if (currentStep[1] < 0) {
                currentStep[1] = totalStepCount[1];
              }
            }
            else if (buttonState[6] == LOW) {
              rangeSelect[1] = LOW;
              digitalWrite(rangeSelectPins[1], rangeSelect[1]);
              updateRangeSelectLeds();
            }
            else {
              if (buttonState[12] == false) {
                editingStep[1] = editingStep[1] - 10;
              }
              else {
                editingStep[1]--;
              }
            }
            break;
          case 4:
            if (buttonState[5] == LOW) {
              currentStep[1]++;
              if (currentStep[1] > totalStepCount[1]) {
                currentStep[1] = 0;
              }
            }
            else if (buttonState[6] == LOW) {
              rangeSelect[1] = HIGH;
              digitalWrite(rangeSelectPins[1], rangeSelect[1]);
              updateRangeSelectLeds();
            }
            else {
              if (buttonState[12] == false) {
                editingStep[1] = editingStep[1] + 10;
              }
              else {
                editingStep[1]++;
              }
            }
            break;
          case 5:
            if (buttonState[11] == LOW) {
              stepGates[editingStep[1]][1] = !stepGates[editingStep[1]][1];
            }
            break;
          case 6:
            if (buttonState[2] == false) {
              actionLayer = 1;
            }
            else if (buttonState[5] == false) {
              actionLayer = 2;
            }
            break;
          case 7:
            if (buttonState[2] == false) {
              currentStep[0] = 0;
              editingStep[0] = 0;
            }
            else if (buttonState[5] == false) {
              currentStep[1] = 0;
              editingStep[1] = 0;
            }
            else {
              currentStep[0] = 0;
              editingStep[0] = 0;
              currentStep[1] = 0;
              editingStep[1] = 0;
            }
            break;
          case 8:
            if (buttonState[2] == false) {
              clockMode[0]++;
              if (clockMode[0] > 2) {
                clockMode[0] = 0;
              }
            }
            else if (buttonState[5] == false) {
              clockMode[1]++;
              if (clockMode[1] > 3) {
                clockMode[1] = 1;
              }
            }
            else if (buttonState[11] == LOW) {
              noClockGates = !noClockGates;
            }
            else {
              clockMode[0] = 1;
              clockMode[1] = 1;
            }
            updateclockModeLeds();
            break;
          case 9:
            if (buttonState[2] == false) {
              reverseSequance[0] = !reverseSequance[0];
            }
            else if (buttonState[5] == false) {
              reverseSequance[1] = !reverseSequance[1];
            }
            else {
              reverseSequance[0] = !reverseSequance[0];
              reverseSequance[1] = !reverseSequance[1];
            }
            break;
          case 10:
            if (buttonState[2] == false) {
              actionLayer = 3;
            }
            else if (buttonState[5] == false) {
              actionLayer = 4;
            }
            break;
          case 11:
            if (buttonState[2] == false) {
              actionLayer = 5;
            }
            else if (buttonState[5] == false) {
              actionLayer = 6;
            }
            break;
          case 12:

            break;
          case 13:

            break;
          case 14:

            break;
        }
        return;
      }

      else if (actionLayer == 1) {
        switch (i) {
          case 0:
            if (buttonState[12] == false) {
              totalStepCount[0] = totalStepCount[0] - 10;
            }
            else {
              totalStepCount[0]--;
            }
            if (totalStepCount[0] < minSteps) {
              totalStepCount[0] = maxSteps;
            }
            break;
          case 1:
            if (buttonState[12] == false) {
              totalStepCount[0] = totalStepCount[0] + 10;
            }
            else {
              totalStepCount[0]++;
            }
            if (totalStepCount[0] > maxSteps) {
              totalStepCount[0] = minSteps;
            }
            break;
          case 2:
          case 3:
          case 4:
          case 5:
          case 6:
          case 7:
          case 8:
          case 9:
          case 10:
          case 11:
            actionLayer = 0;
            break;
            return;
        }
      }

      else if (actionLayer == 2) {
        switch (i) {
          case 3:
            if (buttonState[12] == false) {
              totalStepCount[1] = totalStepCount[1] - 10;
            }
            else {
              totalStepCount[1]--;
            }
            if (totalStepCount[1] < minSteps) {
              totalStepCount[1] = maxSteps;
            }
            break;
          case 4:
            if (buttonState[12] == false) {
              totalStepCount[1] = totalStepCount[1] + 10;
            }
            else {
              totalStepCount[1]++;
            }
            if (totalStepCount[1] > maxSteps) {
              totalStepCount[1] = minSteps;
            }
            break;
          case 0:
          case 1:
          case 2:
          case 5:
          case 6:
          case 7:
          case 8:
          case 9:
          case 10:
          case 11:
            actionLayer = 0;
            break;
            return;
        }
      }

      else if (actionLayer == 3) {
        switch (i) {
          case 0:
            if (buttonState[12] == false) {
              internalClockSpeed = internalClockSpeed - 10;
            }
            else {
              internalClockSpeed--;
            }
            if (internalClockSpeed < leastInternalClockSpeed) {
              internalClockSpeed = maxInternalClockSpeed;
            }
            break;
          case 1:
            if (buttonState[12] == false) {
              internalClockSpeed = internalClockSpeed + 10;
            }
            else {
              internalClockSpeed++;
            }
            if (internalClockSpeed > maxInternalClockSpeed) {
              internalClockSpeed = leastInternalClockSpeed;
            }
            break;
          case 2:
          case 3:
          case 4:
          case 5:
          case 6:
          case 7:
          case 8:
          case 9:
          case 10:
          case 11:
            actionLayer = 0;
            break;
            return;
        }
      }
      else if (actionLayer == 4) {
        switch (i) {
          case 0:
          case 1:
          case 2:
          case 3:
          case 4:
          case 5:
          case 6:
          case 7:
          case 8:
          case 9:
          case 10:
          case 11:
            actionLayer = 0;
            break;
            return;
        }
      }
      else if (actionLayer == 5) {
        switch (i) {
          case 0:
            if (buttonState[12] == false) {
              gateTime[0] = gateTime[0] - 10;
            }
            else {
              gateTime[0]--;
            }
            if (gateTime[0] < gateMinTime) {
              gateTime[0] = gateMaxTime;
            }
            break;
          case 1:
            if (buttonState[12] == false) {
              gateTime[0] = gateTime[0] + 10;
            }
            else {
              gateTime[0]++;
            }
            if (gateTime[0] > gateMaxTime) {
              gateTime[0] = gateMinTime;
            }
            break;
          case 2:
          case 3:
          case 4:
          case 5:
          case 6:
          case 7:
          case 8:
          case 9:
          case 10:
          case 11:
            actionLayer = 0;
            break;
            return;
        }
      }
      else if (actionLayer == 6) {
        switch (i) {
          case 3:
            if (buttonState[12] == false) {
              gateTime[1] = gateTime[1] - 10;
            }
            else {
              gateTime[1]--;
            }
            if (gateTime[1] < gateMinTime) {
              gateTime[1] = gateMaxTime;
            }
            break;
          case 4:
            if (buttonState[12] == false) {
              gateTime[1] = gateTime[1] + 10;
            }
            else {
              gateTime[1]++;
            }
            if (gateTime[1] > gateMaxTime) {
              gateTime[1] = gateMinTime;
            }
            break;
          case 2:
          case 0:
          case 1:
          case 5:
          case 6:
          case 7:
          case 8:
          case 9:
          case 10:
          case 11:
            actionLayer = 0;
            break;
            return;
        }
      }
    }
  }
}


void drawClockMultiply() {
  displayB.clearDisplay();
  displayB.setTextSize(1);
  displayB.setTextColor(SSD1306_WHITE);
  displayB.setCursor(0, 0);
  displayB.print(F("Press Again to Return"));
  displayB.setTextSize(1);
  displayB.setCursor(0, 12);
  displayB << F("Not Implemented")  << endl;
  displayB.display();
}


void drawClockSpeed() {
  displayA.clearDisplay();
  displayA.setTextSize(1);
  displayA.setTextColor(SSD1306_WHITE);
  displayA.setCursor(0, 0);
  displayA.print(F("Press Again to Return"));
  displayA.setTextSize(2);
  displayA.setCursor(0, 12);
  displayA << F("BPM:") << _FLOAT(internalClockSpeed, 0) << endl;
  displayA.display();
}


void drawLeftStepCount() {
  displayA.clearDisplay();
  displayA.setTextSize(1);
  displayA.setTextColor(SSD1306_WHITE);
  displayA.setCursor(0, 0);
  displayA.print(F("Press Again to Return"));
  displayA.setTextSize(2);
  displayA.setCursor(0, 12);
  displayA << F("Steps:") << totalStepCount[0] + 1 << endl;
  displayA.display();
}


void drawRightStepCount() {
  displayB.clearDisplay();
  displayB.setTextSize(1);
  displayB.setTextColor(SSD1306_WHITE);
  displayB.setCursor(0, 0);
  displayB.print(F("Press Again to Return"));
  displayB.setTextSize(2);
  displayB.setCursor(0, 12);
  displayB << F("Steps:") << totalStepCount[1] + 1 << endl;
  displayB.display();
}


void drawLeftGateTime() {
  displayA.clearDisplay();
  displayA.setTextSize(1);
  displayA.setTextColor(SSD1306_WHITE);
  displayA.setCursor(0, 0);
  displayA.print(F("Press Again to Return"));
  displayA.setTextSize(2);
  displayA.setCursor(0, 12);
  displayA << F("Time:") << gateTime[0] << F("MS") << endl;
  displayA.display();
}


void drawRightGateTime() {
  displayB.clearDisplay();
  displayB.setTextSize(1);
  displayB.setTextColor(SSD1306_WHITE);
  displayB.setCursor(0, 0);
  displayB.print(F("Press Again to Return"));
  displayB.setTextSize(2);
  displayB.setCursor(0, 12);
  displayB << F("Time:") << gateTime[1] << F("MS") << endl;
  displayB.display();
}


void drawSequancerMainPage(byte whichScreen) {
  if (whichScreen == 0) {
    displayA.clearDisplay();
    displayA.setTextSize(1);
    displayA.setTextColor(SSD1306_WHITE);
    displayA.setCursor(0, 0);
    displayA.print(F("Clock:"));
    if (clockMode[0] == 0) {
      displayA << F("Internal:") << _FLOAT(internalClockSpeed, 0) << F("BPM") << endl;
    }
    else if (clockMode[0] == 1) {
      displayA.print(F("Off"));
    }
    else if (clockMode[0] == 2) {
      displayA.print(F("External"));
    }
    displayA.setCursor(3, 15);
    displayA << _WIDTHZ((currentStep[0] + 1), 3) << endl;
    displayA.drawRect(28, 14, 70, 9, SSD1306_WHITE);
    displayA.setCursor(30, 15);
    float temp0a = currentStep[0];
    float temp0b = totalStepCount[0];
    float bar0 = (temp0a / temp0b) * 11;
    for (int i = 0; i < bar0; i++) {
      displayA.print("=");
    }
    displayA.setCursor(104, 15);
    displayA << _WIDTHZ((totalStepCount[0] + 1), 3) << endl;
    displayA.setCursor(51, 28);
    displayA << _WIDTHZ((stepVales[currentStep[0]][0]), 4) << endl;
    if (reverseSequance[0] == true) {
      displayA.setCursor(15, 28);
      displayA << F("REV") << endl;
    }
    displayA.setCursor(88, 28);
    displayA << F("Gate:") << ((stepGates[editingStep[0]][0])) << endl;
    displayA.setTextSize(2);
    displayA.setCursor(10, 45);
    displayA << _WIDTHZ((editingStep[0] + 1), 3) << endl;
    displayA.setCursor(68, 45);
    displayA << _WIDTHZ((stepVales[editingStep[0]][0]), 4) << endl;
    displayA.drawRect(66, 42, 50, 20, SSD1306_WHITE);
    displayA.drawRect(8, 42, 39, 20, SSD1306_WHITE);
    displayA.display();
  }
  if (whichScreen == 1) {
    displayB.clearDisplay();
    displayB.setTextSize(1);
    displayB.setTextColor(SSD1306_WHITE);
    displayB.setCursor(0, 0);
    displayB.print(F("Clock:"));
    if (clockMode[1] == 1) {
      displayB.print(F("Off"));
    }
    else if (clockMode[1] == 2) {
      displayB.print(F("External"));
    }
    else if (clockMode[1] == 3) {
      displayB.print(F("Sync"));
    }
    displayB.setCursor(3, 15);
    displayB << _WIDTHZ((currentStep[1] + 1), 3) << endl;
    displayB.drawRect(28, 14, 70, 9, SSD1306_WHITE);
    displayB.setCursor(30, 15);
    float temp1a = currentStep[1];
    float temp1b = totalStepCount[1];
    float bar1 = (temp1a / temp1b) * 11;
    for (int i = 0; i < bar1; i++) {
      displayB.print("=");
    }
    displayB.setCursor(104, 15);
    displayB << _WIDTHZ((totalStepCount[1] + 1), 3) << endl;
    displayB.setCursor(51, 28);
    displayB << _WIDTHZ((stepVales[currentStep[1]][1]), 4) << endl;
    if (reverseSequance[1] == true) {
      displayB.setCursor(15, 28);
      displayB << F("REV") << endl;
    }
    displayB.setCursor(88, 28);
    displayB << F("Gate:") << ((stepGates[editingStep[1]][1])) << endl;
    displayB.setTextSize(2);
    displayB.setCursor(10, 45);
    displayB << _WIDTHZ((editingStep[1] + 1), 3) << endl;
    displayB.setCursor(68, 45);
    displayB << _WIDTHZ((stepVales[editingStep[1]][1]), 4) << endl;
    displayB.drawRect(66, 42, 50, 20, SSD1306_WHITE);
    displayB.drawRect(8, 42, 39, 20, SSD1306_WHITE);
    displayB.display();
  }
}


void readEncoders() {
  for (byte i = 0; i < 2; i++) {
    long newPosition;
    if (i == 0) {
      newPosition = Enc1.read();
    }
    else {
      newPosition = Enc2.read();
    }
    newPosition = newPosition / 4;
    if (newPosition != oldPosition[i]) {
      if (newPosition > oldPosition[i]) {
        encoderTurn[((i + 1) * 2) - 1] = true;
      }
      if (newPosition < oldPosition[i]) {
        encoderTurn[((i + 1) * 2) - 2] = true;
      }
      oldPosition[i] = newPosition;
    }
  }
}


void readButtons() {
  for (byte i = 0; i < buttonCount; i++) {
    int reading = digitalRead(buttonPins[i]);
    if (reading != lastButtonState[i]) {
      lastDebounceTime = millis();
    }
    if ((millis() - lastDebounceTime) > debounceDelay) {
      if (reading != buttonState[i]) {
        buttonState[i] = reading;
        if (buttonState[i] == LOW) {
          buttonWasPressed[i] = true;
        }
      }
    }
    lastButtonState[i] = reading;
  }
}


void internalClock() {
  if (clockMode[0] == 0 && (millis() - lastInternalClock) > ((60 / internalClockSpeed) * 1000)) {
    lastInternalClock = millis();
    if (reverseSequance[0] == true) {
      currentStep[0]--;
      triggerGate(0, 1);
    }
    else {
      currentStep[0]++;
      triggerGate(0, 0);
    }
    if (clockMode[1] == 3) {
      if (reverseSequance[1] == true) {
        currentStep[1]--;
        triggerGate(1, 1);
      }
      else {
        currentStep[1]++;
        triggerGate(1, 0);
      }
    }
  }
}


void updateRangeSelectLeds() {
  if (rangeSelect[0] == LOW) {
    digitalWrite(rgbPins[2][2], HIGH);
    digitalWrite(rgbPins[2][1], LOW);
  }
  else if (rangeSelect[0] == HIGH) {
    digitalWrite(rgbPins[2][2], LOW);
    digitalWrite(rgbPins[2][1], HIGH);
  }
  if (rangeSelect[1] == LOW) {
    digitalWrite(rgbPins[5][2], HIGH);
    digitalWrite(rgbPins[5][1], LOW);
  }
  else if (rangeSelect[1] == HIGH) {
    digitalWrite(rgbPins[5][2], LOW);
    digitalWrite(rgbPins[5][1], HIGH);
  }
}


void updateclockModeLeds() {
  if (clockMode[0] == 0) {
    digitalWrite(rgbPins[0][0], LOW);
    digitalWrite(rgbPins[0][1], LOW);
    digitalWrite(rgbPins[0][2], HIGH);
  }
  else if (clockMode[0] == 1) {
    digitalWrite(rgbPins[0][0], HIGH);
    digitalWrite(rgbPins[0][1], LOW);
    digitalWrite(rgbPins[0][2], LOW);
  }
  else if (clockMode[0] == 2) {
    digitalWrite(rgbPins[0][0], LOW);
    digitalWrite(rgbPins[0][1], HIGH);
    digitalWrite(rgbPins[0][2], LOW);
  }
  if (clockMode[1] == 3) {
    digitalWrite(rgbPins[3][0], LOW);
    digitalWrite(rgbPins[3][1], LOW);
    digitalWrite(rgbPins[3][2], HIGH);
  }
  else if (clockMode[1] == 1) {
    digitalWrite(rgbPins[3][0], HIGH);
    digitalWrite(rgbPins[3][1], LOW);
    digitalWrite(rgbPins[3][2], LOW);
  }
  else if (clockMode[1] == 2) {
    digitalWrite(rgbPins[3][0], LOW);
    digitalWrite(rgbPins[3][1], HIGH);
    digitalWrite(rgbPins[3][2], LOW);
  }
}


void wrapCursor() {
  for (byte i = 0; i < 2; i++) {
    if (editingStep[i] > totalStepCount[i]) {
      editingStep[i] = 0;
    }
    if (editingStep[i] < 0) {
      editingStep[i] = totalStepCount[i];
    }
  }
}
