#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <RTClib.h>
#include <EEPROM.h>

// Create a second I2C bus for RTC
TwoWire I2C_RTC = TwoWire(1);

LiquidCrystal_I2C lcd(0x27, 16, 2);
RTC_DS3231 rtc;

HardwareSerial SubSerial(2);

#define TX_SUB 17
#define RX_SUB 16
#define BUZZER 4

int timerDigits[4] = {0, 0, 0, 0};
int clockDigits[4] = {0, 0, 0, 0};

int clockCursorPos = 0;  // Cursor position for clock mode (0-3)
int timerCursorPos = 0;  // Cursor position for timer mode (0-3)
int cursorPos = 0;       // Active cursor position (mirror of clock or timer)

bool settingMode = false;
bool displayClock = false;
bool timerRunning = false;

long remainingSeconds = 0;
unsigned long lastSecond = 0;

// Boot startup timing
unsigned long bootStartTime = 0;
const unsigned long BOOT_SETTLE_TIME = 3000;  // Don't send serial data for 3 seconds after boot

bool blinkState = true;
unsigned long blinkTimer = 0;
const int blinkInterval = 600;

// Status message variables (for temporary button feedback)
int timerStatusMessage = 0;  // 0=none, 1=paused, 2=running, 3=reset, 4=clock mode, 5=timer mode
unsigned long statusMessageTime = 0;
const unsigned long MESSAGE_DURATION = 10000;  // 10 seconds

// Buzzer sequence variables
bool buzzerActive = false;
unsigned long buzzerStartTime = 0;
int buzzerPatternType = 0;  // 0=none, 1=startup, 2=pause, 3=10min, 4=1min, 5=completion

const int LOW_TONE = 800;
const int HIGH_TONE = 2800;
const int LOUD_TONE = 3800;
const int VERY_LOUD_TONE = 4200;  // For 1-min alert (loudest)

// Buzzer pattern: array of (frequency, duration in ms) pairs, -1 frequency = silence
struct BuzzerNote {
  int frequency;  // -1 for silence
  unsigned long duration;
};

// Define all buzzer patterns
BuzzerNote startupPattern[] = {
  {LOUD_TONE, 300}, {-1, 700},
  {LOUD_TONE, 300}, {-1, 700},
  {LOUD_TONE, 300}, {-1, 700}
};
const int startupPatternLen = 6;

BuzzerNote pausePattern[] = {
  {LOW_TONE, 300}, {-1, 400},
  {HIGH_TONE, 300}, {-1, 400},
  {LOW_TONE, 300}, {-1, 400},
  {HIGH_TONE, 300}, {-1, 700},
  {LOW_TONE, 300}, {-1, 400},
  {HIGH_TONE, 300}, {-1, 400},
  {LOW_TONE, 300}, {-1, 400},
  {HIGH_TONE, 300}, {-1, 400}
};
const int pausePatternLen = 16;

BuzzerNote alert10MinPattern[] = {
  {LOUD_TONE, 300}, {-1, 650},
  {LOUD_TONE, 300}, {-1, 650},
  {LOUD_TONE, 300}, {-1, 650},
  {LOUD_TONE, 300}, {-1, 650},
  {LOUD_TONE, 300}, {-1, 650}
};
const int alert10MinPatternLen = 10;

BuzzerNote alert1MinPattern[] = {
  {VERY_LOUD_TONE, 250}, {-1, 350},
  {VERY_LOUD_TONE, 250}, {-1, 350},
  {VERY_LOUD_TONE, 250}, {-1, 350},
  {VERY_LOUD_TONE, 250}, {-1, 350},
  {VERY_LOUD_TONE, 250}, {-1, 350},
  {VERY_LOUD_TONE, 250}, {-1, 350},
  {VERY_LOUD_TONE, 250}, {-1, 350},
  {VERY_LOUD_TONE, 250}, {-1, 350},
  {VERY_LOUD_TONE, 250}, {-1, 350},
  {VERY_LOUD_TONE, 250}, {-1, 350},
  {VERY_LOUD_TONE, 300}, {-1, 450},
  {VERY_LOUD_TONE, 300}, {-1, 450}
};
const int alert1MinPatternLen = 24;

BuzzerNote completionPattern[] = {
  {LOUD_TONE, 300}, {-1, 400},
  {LOUD_TONE, 300}, {-1, 500},
  {LOW_TONE, 300}, {-1, 500},
  {LOUD_TONE, 300}, {-1, 400},
  {LOW_TONE, 300}, {-1, 400},
  {LOUD_TONE, 300}, {-1, 500}
};
const int completionPatternLen = 12;

int currentPatternIndex = 0;
unsigned long patternNoteStart = 0;

// Alert tracking
bool alert10MinTriggered = false;
bool alert1MinTriggered = false;

const byte ROWS = 4;
const byte COLS = 4;

char keys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

byte rowPins[ROWS] = {13, 12, 14, 27};
byte colPins[COLS] = {26, 25, 33, 32};

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

void setup() {
  Serial.begin(115200);
  
  // Initialize I2C Bus 0 for LCD on GPIO 21,22
  Wire.begin(21, 22);
  
  // Initialize I2C Bus 1 for RTC on GPIO 18,19
  I2C_RTC.begin(18, 19, 100000);
  
  lcd.init();
  lcd.backlight();
  
  // Initialize RTC with second I2C bus
  rtc.begin(&I2C_RTC);
  
  if (!rtc.begin(&I2C_RTC)) {
    Serial.println("RTC DS3231 not found!");
  } else {
    Serial.println("RTC DS3231 initialized successfully!");
  }
  
  SubSerial.begin(115200, SERIAL_8N1, RX_SUB, TX_SUB);
  pinMode(BUZZER, OUTPUT);
  
  // HARD RESET: Clear all volatile state variables to prevent corruption after power loss
  resetAllStates();
  
  // Initialize boot timing
  bootStartTime = millis();
  
  loadClockDigits();
  

}

void loop() {
  // SAFETY CHECK: Ensure cursor positions never go out of bounds
  if (displayClock) {
    if (clockCursorPos < 0 || clockCursorPos > 3) {
      Serial.println("WARNING: clockCursorPos out of bounds! Resetting to 0");
      clockCursorPos = 0;
    }
    cursorPos = clockCursorPos;
  } else {
    if (timerCursorPos < 0 || timerCursorPos > 3) {
      Serial.println("WARNING: timerCursorPos out of bounds! Resetting to 0");
      timerCursorPos = 0;
    }
    cursorPos = timerCursorPos;
  }
  
  readKeypad();
  updateBlink();
  updateTimer();
  updateBuzzerSequence();
  updateLCD();
  sendToSubsystem();
}

// HARD RESET: Initialize all volatile state variables to safe defaults
// This prevents corruption bugs after sudden power loss
void resetAllStates() {
  // Timer state
  for (int i = 0; i < 4; i++) {
    timerDigits[i] = 0;
  }
  remainingSeconds = 0;
  timerRunning = false;
  timerCursorPos = 0;
  
  // Clock state
  for (int i = 0; i < 4; i++) {
    clockDigits[i] = 0;
  }
  clockCursorPos = 0;
  
  // Display/UI state
  displayClock = false;
  settingMode = false;
  cursorPos = 0;
  blinkState = true;
  blinkTimer = millis();
  
  // Status messages
  timerStatusMessage = 0;
  statusMessageTime = 0;
  
  // Buzzer state
  buzzerActive = false;
  buzzerStartTime = 0;
  buzzerPatternType = 0;
  currentPatternIndex = 0;
  patternNoteStart = 0;
  noTone(BUZZER);  // Ensure buzzer is off
  
  // Alert tracking
  alert10MinTriggered = false;
  alert1MinTriggered = false;
  
  // Timing references
  lastSecond = millis();
  lastClockEditTime = 0;
  
  // Clear keypad buffer by consuming all pending keys
  char dummyKey;
  while ((dummyKey = keypad.getKey())) {
    // Just clear the buffer
  }
  
  // AM/PM (load from EEPROM - not reset)
  // clockPM is preserved from EEPROM
  
  Serial.println("All states reset to safe defaults!");
}

void updateBlink() {
  if (millis() - blinkTimer > blinkInterval) {
    blinkTimer = millis();
    blinkState = !blinkState;
  }
}

void readKeypad() {
  char key = keypad.getKey();
  if (!key) return;

  // Numeric input: 0-9 OR AM/PM selection (1=AM, 2=PM)
  if (key >= '0' && key <= '9' && settingMode) {
    int value = key - '0';

    if (displayClock) {
      if (validClockDigit(clockCursorPos, value)) {
        clockDigits[clockCursorPos] = value;
        moveCursorRight();
      }
    } else {
      // Timer mode - strictly positions 0-3 only
      if (timerCursorPos >= 0 && timerCursorPos <= 3 && validTimerDigit(timerCursorPos, value)) {
        timerDigits[timerCursorPos] = value;
        moveCursorRight();
      }
    }
  }

  if (key == '*' && settingMode) moveCursorLeft();
  if (key == '#' && settingMode) moveCursorRight();
  if (key == 'A' && !displayClock) {
    // Auto-exit setting mode when button A is pressed
    if (settingMode) {
      settingMode = false;
    } else {
      if (!timerRunning) {
        // FIX: Only calculate from digits if starting fresh (remainingSeconds is 0)
        // This prevents losing the seconds component when resuming from pause
        if (remainingSeconds == 0) {
          remainingSeconds = getTimerSeconds();
          // Startup beep: 3 loud beeps
          buzzerPatternType = 1;
          buzzerActive = true;
          buzzerStartTime = millis();
          patternNoteStart = millis();
          currentPatternIndex = 0;
          alert10MinTriggered = false;
          alert1MinTriggered = false;
        }
        lastSecond = millis();  // Reset timing reference for accurate countdown
      } else {
        // Pause beep: Low-High-Low-High sequence
        buzzerPatternType = 2;
        buzzerActive = true;
        buzzerStartTime = millis();
        patternNoteStart = millis();
        currentPatternIndex = 0;
      }
      timerRunning = !timerRunning;
      
      // Set status message
      timerStatusMessage = timerRunning ? 2 : 1;  // 2=running, 1=paused
      statusMessageTime = millis();
    }
  }

  // Reset timer
  if (key == 'B') {
    // Auto-exit setting mode when button B is pressed
    if (settingMode) {
      settingMode = false;
    } else {
      timerRunning = false;
      remainingSeconds = 0;
      buzzerActive = false;
      noTone(BUZZER);
      for (int i = 0; i < 4; i++) timerDigits[i] = 0;
      
      // Set status message
      timerStatusMessage = 3;  // 3=reset
      statusMessageTime = millis();
    }
  }

  // Toggle editing mode
  if (key == 'C') {
    if (settingMode && displayClock) saveClock();
    settingMode = !settingMode;

    if (settingMode) {
      lastClockEditTime = millis();  // Start grace period when entering edit mode
      // Pause timer when entering set mode (only in timer mode)
      if (!displayClock && timerRunning) {
        timerRunning = false;
        timerStatusMessage = 1;  // Show "TIMER IS PAUSED"
        statusMessageTime = millis();
      }
      
      // Update cursor position based on mode
      if (displayClock) {
        clockCursorPos = 0;
        cursorPos = clockCursorPos;
        loadClockDigits();
      } else {
        timerCursorPos = 0;
        cursorPos = timerCursorPos;
      }
    }
  }

  // Switch between clock and timer display
  if (key == 'D') {
    // Auto-exit setting mode when button D is pressed
    if (settingMode) {
      settingMode = false;
    } else {
      displayClock = !displayClock;
      // Swap cursor position based on new mode
      if (displayClock) {
        cursorPos = clockCursorPos;
        loadClockDigits();
        // Set status message
        timerStatusMessage = 4;  // 4=clock mode
      } else {
        cursorPos = timerCursorPos;
        // Set status message
        timerStatusMessage = 5;  // 5=timer mode
      }
      statusMessageTime = millis();
    }
  }
}

void loadClockDigits() {
  DateTime now = rtc.now();
  clockDigits[0] = now.hour() / 10;
  clockDigits[1] = now.hour() % 10;
  clockDigits[2] = now.minute() / 10;
  clockDigits[3] = now.minute() % 10;
}

void saveClock() {
  DateTime now = rtc.now();
  int h = clockDigits[0] * 10 + clockDigits[1];
  int m = clockDigits[2] * 10 + clockDigits[3];
  rtc.adjust(DateTime(now.year(), now.month(), now.day(), h, m, 0));
}

bool validTimerDigit(int pos, int value) {
  if (pos == 0) return value <= 2;
  if (pos == 1) {
    if (timerDigits[0] == 2) return value <= 3;
    return true;
  }
  if (pos == 2) return value <= 5;
  return true;
}

bool validClockDigit(int pos, int value) {
  if (pos == 0) return value <= 2;
  if (pos == 1) {
    if (clockDigits[0] == 2) return value <= 3;
    return true;
  }
  if (pos == 2) return value <= 5;
  return true;
}

void moveCursorRight() {
  if (displayClock) {
    clockCursorPos++;
    if (clockCursorPos > 3) clockCursorPos = 3;
    cursorPos = clockCursorPos;
  } else {
    timerCursorPos++;
    if (timerCursorPos > 3) timerCursorPos = 3;
    cursorPos = timerCursorPos;
  }
}

void moveCursorLeft() {
  if (displayClock) {
    clockCursorPos--;
    if (clockCursorPos < 0) clockCursorPos = 0;
    cursorPos = clockCursorPos;
  } else {
    timerCursorPos--;
    if (timerCursorPos < 0) timerCursorPos = 0;
    cursorPos = timerCursorPos;
  }
}

long getTimerSeconds() {
  int h = timerDigits[0] * 10 + timerDigits[1];
  int m = timerDigits[2] * 10 + timerDigits[3];
  return h * 3600L + m * 60L;  // Using long literals for safety
}

// Convert 24-hour format to 12-hour format
void convert24to12(int hour24, int &hour12, bool &isPM) {
  isPM = (hour24 >= 12);
  if (hour24 == 0) {
    hour12 = 12;  // Midnight is 12:00 AM
  } else if (hour24 <= 12) {
    hour12 = hour24;
  } else {
    hour12 = hour24 - 12;  // 13:00 becomes 01:00 PM
  }
}

void updateTimer() {
  if (!timerRunning) return;

  if (millis() - lastSecond >= 1000) {
    lastSecond += 1000;

    if (remainingSeconds > 0) {
      remainingSeconds--;

      // Update display digits based on remaining time
      int h = remainingSeconds / 3600;
      int m = (remainingSeconds % 3600) / 60;

      timerDigits[0] = h / 10;
      timerDigits[1] = h % 10;
      timerDigits[2] = m / 10;
      timerDigits[3] = m % 10;
      
      // Check for 10-minute threshold (600 seconds)
      if (remainingSeconds == 600 && !alert10MinTriggered) {
        alert10MinTriggered = true;
        buzzerPatternType = 3;  // 10-min alert: 5 high beeps
        buzzerActive = true;
        buzzerStartTime = millis();
        patternNoteStart = millis();
        currentPatternIndex = 0;
      }
      
      // Check for 1-minute threshold (60 seconds)
      if (remainingSeconds == 60 && !alert1MinTriggered) {
        alert1MinTriggered = true;
        buzzerPatternType = 4;  // 1-min alert: 10 high + 2 low beeps
        buzzerActive = true;
        buzzerStartTime = millis();
        patternNoteStart = millis();
        currentPatternIndex = 0;
      }
    } else {
      timerRunning = false;
      buzzerPatternType = 5;  // Completion: cheerful sequence
      buzzerActive = true;
      buzzerStartTime = millis();
      patternNoteStart = millis();
      currentPatternIndex = 0;
    }
  }
}

void printDigit(int pos, int value) {
  int lcdPos[4] = {2, 3, 5, 6};
  lcd.setCursor(lcdPos[pos], 0);

  if (settingMode && pos == cursorPos && !blinkState)
    lcd.print(" ");
  else
    lcd.print(value);
}

void updateLCD() {
  lcd.setCursor(0, 0);

  if (displayClock) {
    DateTime now = rtc.now();

    lcd.print("C ");

    int h = settingMode ? clockDigits[0] * 10 + clockDigits[1] : now.hour();
    int m = settingMode ? clockDigits[2] * 10 + clockDigits[3] : now.minute();

    // Convert to 12-hour format
    int h12;
    bool isPM;
    convert24to12(h, h12, isPM);

    printDigit(0, h12 / 10);
    printDigit(1, h12 % 10);

    lcd.setCursor(4, 0);
    lcd.print(":");

    printDigit(2, m / 10);
    printDigit(3, m % 10);

    lcd.print(":");

    if (now.second() < 10) lcd.print("0");
    lcd.print(now.second());
  } else {
    lcd.print("T ");

    printDigit(0, timerDigits[0]);
    printDigit(1, timerDigits[1]);

    lcd.setCursor(4, 0);
    lcd.print(":");

    printDigit(2, timerDigits[2]);
    printDigit(3, timerDigits[3]);

    lcd.print(":");

    int sec = remainingSeconds % 60;

    if (sec < 10) lcd.print("0");
    lcd.print(sec);

    lcd.setCursor(15, 0);

    if (timerRunning) lcd.print("G");
    else lcd.print("P");
  }
  
  // Display bottom row: SET MODE or Status Message or blank
  lcd.setCursor(0, 1);
  
  // Check if status message has expired
  if (timerStatusMessage != 0 && (millis() - statusMessageTime > MESSAGE_DURATION)) {
    timerStatusMessage = 0;  // Clear expired message
  }
  
  // Display based on priority: SET MODE > Status Message > Blank
  if (settingMode) {
    lcd.print("SET MODE        ");
  } else if (timerStatusMessage == 1) {
    lcd.print("TIMER IS PAUSED ");
  } else if (timerStatusMessage == 2) {
    lcd.print("TIMER IS RUNNING");
  } else if (timerStatusMessage == 3) {
    lcd.print("TIMER RESET     ");
  } else if (timerStatusMessage == 4) {
    lcd.print("CLOCK MODE      ");
  } else if (timerStatusMessage == 5) {
    lcd.print("TIMER MODE      ");
  } else {
    lcd.print("                ");  // Clear bottom row
  }
}

void sendToSubsystem() {
  // Don't send anything during boot settle period
  if (millis() - bootStartTime < BOOT_SETTLE_TIME) {
    return;
  }

  char buffer[10];
  int d0, d1, d2, d3;

  if (displayClock) {
    if (settingMode) {
      d0 = clockDigits[0];
      d1 = clockDigits[1];
      d2 = clockDigits[2];
      d3 = clockDigits[3];
    } else {
      DateTime now = rtc.now();
      // Convert to 12-hour format for subsystem display
      int h12;
      bool isPM;
      convert24to12(now.hour(), h12, isPM);
      d0 = h12 / 10;
      d1 = h12 % 10;
      d2 = now.minute() / 10;
      d3 = now.minute() % 10;
    }
  } else {
    d0 = timerDigits[0];
    d1 = timerDigits[1];
    d2 = timerDigits[2];
    d3 = timerDigits[3];
  }

  sprintf(buffer, "%d%d%d%d%d%d\n", d0, d1, d2, d3, settingMode, cursorPos);
  SubSerial.print(buffer);
}

void updateBuzzerSequence() {
  if (!buzzerActive) return;

  BuzzerNote* pattern = nullptr;
  int patternLen = 0;
  
  // Select pattern
  switch(buzzerPatternType) {
    case 1:
      pattern = startupPattern;
      patternLen = startupPatternLen;
      break;
    case 2:
      pattern = pausePattern;
      patternLen = pausePatternLen;
      break;
    case 3:
      pattern = alert10MinPattern;
      patternLen = alert10MinPatternLen;
      break;
    case 4:
      pattern = alert1MinPattern;
      patternLen = alert1MinPatternLen;
      break;
    case 5:
      pattern = completionPattern;
      patternLen = completionPatternLen;
      break;
    default:
      buzzerActive = false;
      return;
  }
  
  // Check if we've finished all notes
  if (currentPatternIndex >= patternLen) {
    noTone(BUZZER);
    buzzerActive = false;
    currentPatternIndex = 0;
    return;
  }
  
  // Get current note
  BuzzerNote currentNote = pattern[currentPatternIndex];
  unsigned long noteDuration = currentNote.duration;
  unsigned long noteElapsed = millis() - patternNoteStart;
  
  // Play or silence the current frequency
  if (noteElapsed < noteDuration) {
    if (currentNote.frequency > 0) {
      tone(BUZZER, currentNote.frequency);
    } else {
      noTone(BUZZER);
    }
  } else {
    // Move to next note
    currentPatternIndex++;
    patternNoteStart = millis();
    noTone(BUZZER);
  }
}
