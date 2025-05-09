#include <Arduino.h>
#include <U8g2lib.h>
#include <OneWireSTM.h>

// Display
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);
#define DISPLAY_WIDTH 128
#define TEMP_RIGHT_POS 124  // Right-most position for temperature display

// Pins
#define ENC_A PB4
#define ENC_B PB3
#define ENC_SW PB5
#define OneWirePin PA5
#define SOLIDSTATE_1 PA3
#define SOLIDSTATE_2 PA2

// Timing constants
#define TEMP_UPDATE_INTERVAL 1000
#define ACTIVE_DISPLAY_INTERVAL 50
#define IDLE_DISPLAY_INTERVAL 1000
#define IDLE_TIMEOUT 3000

// Function prototypes
void setup();
void loop();
void updateEncoder();
void readTemperatures();
void updateOutputs();
void handleEncoderButton();
void handleMenuNavigation();
void drawMainMenu();
void drawSettingsMenu();
void drawOutputSetting(float setPoint, float delta);
void updateDisplay();
void handleStateMachine();

// Global variables
OneWire ds(OneWirePin);
byte addr1[8], addr2[8];
bool sensorsFound = false;

volatile int encoderPos = 0;
volatile int lastEncoded = 0;
volatile bool buttonPressed = false;
volatile bool lastButtonState = false;

float temp1 = 0.0, temp2 = 0.0;
float setPoint1 = 25.0, setPoint2 = 25.0;
float delta1 = 0.5, delta2 = 0.5;
bool output1State = false, output2State = false;

unsigned long lastTempUpdate = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastInteractionTime = 0;
bool displayNeedsUpdate = true;
bool isActiveMode = false;

enum AppState { MAIN, SETTINGS_MENU, SET_OUTPUT1, SET_OUTPUT2 };
AppState currentState = MAIN;
int menuPos = 0;
bool settingActive = false;
float* currentSetting = nullptr;



void setup() {
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);
  pinMode(SOLIDSTATE_1, OUTPUT);
  pinMode(SOLIDSTATE_2, OUTPUT);
  
  attachInterrupt(digitalPinToInterrupt(ENC_A), updateEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B), updateEncoder, CHANGE);
  
  u8g2.begin();
  u8g2.setFont(u8g2_font_5x7_tr);
  
  sensorsFound = ds.search(addr1) && ds.search(addr2);
  if (sensorsFound) {
    ds.reset();
    ds.select(addr1);
    ds.write(0x4E);
    ds.write(0);
    ds.write(0);
    ds.write(0x7F);
    
    ds.reset();
    ds.select(addr2);
    ds.write(0x4E);
    ds.write(0);
    ds.write(0);
    ds.write(0x7F);
  }
}

void loop() {
  handleEncoderButton();
  handleMenuNavigation();
  handleStateMachine();
  
  unsigned long currentMillis = millis();
  
  if (isActiveMode && (currentMillis - lastInteractionTime > IDLE_TIMEOUT)) {
    isActiveMode = false;
    displayNeedsUpdate = true;
  }
  
  if (currentMillis - lastTempUpdate >= TEMP_UPDATE_INTERVAL) {
    readTemperatures();
    updateOutputs();
    lastTempUpdate = currentMillis;
  }
  
  unsigned long displayInterval = isActiveMode ? ACTIVE_DISPLAY_INTERVAL : IDLE_DISPLAY_INTERVAL;
  if (displayNeedsUpdate || (currentMillis - lastDisplayUpdate >= displayInterval)) {
    updateDisplay();
    lastDisplayUpdate = currentMillis;
    displayNeedsUpdate = false;
  }
}

// Interrupt Service Routine for encoder
void updateEncoder() {
  int MSB = digitalRead(ENC_A);
  int LSB = digitalRead(ENC_B);
  int encoded = (MSB << 1) | LSB;
  int sum = (lastEncoded << 2) | encoded;

  if (sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011) {
    encoderPos++;
  } else if (sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000) {
    encoderPos--;
  }
  lastEncoded = encoded;
  lastInteractionTime = millis();
  isActiveMode = true;
}

void readTemperatures() {
  if (!sensorsFound) return;
  
  static bool conversionStarted = false;
  static unsigned long conversionStartTime = 0;
  
  if (!conversionStarted) {
    ds.reset();
    ds.skip();
    ds.write(0x44, 1);
    conversionStartTime = millis();
    conversionStarted = true;
  } else if (millis() - conversionStartTime >= 750) {
    byte data[12];
    
    // Read sensor 1
    ds.reset();
    ds.select(addr1);
    ds.write(0xBE);
    for (int i = 0; i < 9; i++) data[i] = ds.read();
    int16_t raw1 = (data[1] << 8) | data[0];
    temp1 = (float)raw1 / 16.0;  // This will properly handle negative values
    
    // Read sensor 2
    ds.reset();
    ds.select(addr2);
    ds.write(0xBE);
    for (int i = 0; i < 9; i++) data[i] = ds.read();
    int16_t raw2 = (data[1] << 8) | data[0];
    temp2 = (float)raw2 / 16.0;  // This will properly handle negative values
    
    conversionStarted = false;
    displayNeedsUpdate = true;
  }
}

void updateOutputs() {
  bool stateChanged = false;
  
  // Check Output 1
  if (temp1 <= (setPoint1 - delta1) && !output1State) {
    output1State = true;
    stateChanged = true;
  } else if (temp1 >= setPoint1 && output1State) {
    output1State = false;
    stateChanged = true;
  }
  
  // Check Output 2
  if (temp2 <= (setPoint2 - delta2) && !output2State) {
    output2State = true;
    stateChanged = true;
  } else if (temp2 >= setPoint2 && output2State) {
    output2State = false;
    stateChanged = true;
  }
  
  // Only update physical outputs if state changed
  if (stateChanged) {
    digitalWrite(SOLIDSTATE_1, output1State);
    digitalWrite(SOLIDSTATE_2, output2State);
    displayNeedsUpdate = true; // Force display update
  }
}

void handleEncoderButton() {
  bool currentButtonState = digitalRead(ENC_SW);
  if (currentButtonState != lastButtonState) {
    if (currentButtonState == LOW) {
      lastInteractionTime = millis();
      isActiveMode = true;
      buttonPressed = true;
      displayNeedsUpdate = true;
    }
    lastButtonState = currentButtonState;
  }
}

void handleMenuNavigation() {
  static int lastEncoderPos = 0;
  if (encoderPos != lastEncoderPos) {
    int diff = encoderPos - lastEncoderPos;
    lastEncoderPos = encoderPos;
    
    if (settingActive && currentSetting) {
      *currentSetting = max(0.0, *currentSetting + (0.5 * diff));
    } else {
      if (currentState == MAIN) {
        menuPos = (menuPos + diff + 3) % 3;
      } else if (currentState == SETTINGS_MENU) {
        menuPos = (menuPos + diff + 3) % 3;
      }
    }
    displayNeedsUpdate = true;
  }
}

void drawMainMenu() {
  u8g2.clearBuffer();
  u8g2.drawStr(0, 10, "Temperature controller");
  
  // Sensor 1 line
  char line1[30];
  snprintf(line1, sizeof(line1), "%sSensor 1 %s", 
           menuPos == 0 ? ">" : " ", 
           output1State ? "ON " : "OFF");
  u8g2.drawStr(0, 25, line1);
  
  // Right-aligned temperature
  char temp1Str[10];
  snprintf(temp1Str, sizeof(temp1Str), "(%.2fC)", temp1);
  u8g2.drawStr(TEMP_RIGHT_POS - u8g2.getStrWidth(temp1Str), 25, temp1Str);
  
  // Sensor 2 line
  char line2[30];
  snprintf(line2, sizeof(line2), "%sSensor 2 %s", 
           menuPos == 1 ? ">" : " ", 
           output2State ? "ON " : "OFF");
  u8g2.drawStr(0, 40, line2);
  
  // Right-aligned temperature
  char temp2Str[10];
  snprintf(temp2Str, sizeof(temp2Str), "(%.2fC)", temp2);
  u8g2.drawStr(TEMP_RIGHT_POS - u8g2.getStrWidth(temp2Str), 40, temp2Str);
  
  // Settings line
  u8g2.drawStr(0, 55, menuPos == 2 ? ">Settings" : " Settings");
  
  u8g2.sendBuffer();
}

void drawSettingsMenu() {
  u8g2.clearBuffer();
  u8g2.drawStr(0, 10, "Settings menu");
  
  u8g2.drawStr(0, 25, menuPos == 0 ? "> Set output 1" : "  Set output 1");
  u8g2.drawStr(0, 40, menuPos == 1 ? "> Set output 2" : "  Set output 2");
  u8g2.drawStr(0, 55, menuPos == 2 ? "> Back" : "  Back");
  
  u8g2.sendBuffer();
}

void drawOutputSetting(float setPoint, float delta) {
  u8g2.clearBuffer();
  u8g2.drawStr(0, 10, "Output Settings");
  
  // Setpoint line
  char setPointStr[10];
  snprintf(setPointStr, sizeof(setPointStr), "(%.1fC)", setPoint);
  u8g2.drawStr(0, 25, "On at:");
  u8g2.drawStr(TEMP_RIGHT_POS - u8g2.getStrWidth(setPointStr), 25, setPointStr);
  
  // Delta line
  char deltaStr[10];
  snprintf(deltaStr, sizeof(deltaStr), "(%.1fC)", delta);
  u8g2.drawStr(0, 40, "Off delta:");
  u8g2.drawStr(TEMP_RIGHT_POS - u8g2.getStrWidth(deltaStr), 40, deltaStr);
  
  u8g2.drawStr(0, 55, "Press to confirm");
  u8g2.sendBuffer();
}

void updateDisplay() {
  switch(currentState) {
    case MAIN: drawMainMenu(); break;
    case SETTINGS_MENU: drawSettingsMenu(); break;
    case SET_OUTPUT1: drawOutputSetting(setPoint1, delta1); break;
    case SET_OUTPUT2: drawOutputSetting(setPoint2, delta2); break;
  }
}

void handleStateMachine() {
  if (buttonPressed) {
    buttonPressed = false;
    
    switch(currentState) {
      case MAIN:
        if (menuPos == 2) {
          currentState = SETTINGS_MENU;
          menuPos = 0;
        }
        break;
        
      case SETTINGS_MENU:
        if (menuPos == 2) {
          currentState = MAIN;
          menuPos = 2;
        } else if (menuPos == 0) {
          currentState = SET_OUTPUT1;
          settingActive = true;
          currentSetting = &setPoint1;
        } else if (menuPos == 1) {
          currentState = SET_OUTPUT2;
          settingActive = true;
          currentSetting = &setPoint2;
        }
        break;
        
      case SET_OUTPUT1:
        if (settingActive) {
          if (currentSetting == &setPoint1) {
            currentSetting = &delta1;
          } else {
            settingActive = false;
            currentState = SETTINGS_MENU;
          }
        }
        break;
        
      case SET_OUTPUT2:
        if (settingActive) {
          if (currentSetting == &setPoint2) {
            currentSetting = &delta2;
          } else {
            settingActive = false;
            currentState = SETTINGS_MENU;
          }
        }
        break;
    }
    displayNeedsUpdate = true;
  }
}