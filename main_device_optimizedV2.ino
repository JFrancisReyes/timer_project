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
#define EEPROM_AM_PM_ADDRESS 0  // EEPROM address for AM/PM storage

int timerDigits[4] = {0, 0, 0, 0};
int clockDigits[4] = {0, 0, 0, 0};
bool clockPM = false;

int cursorPos = 0;

bool settingMode = false;
bool displayClock = false;
bool timerRunning = false;

long remainingSeconds = 0;
unsigned long lastSecond = 0;

bool blinkState = true;
unsigned long blinkTimer = 0;
const int blinkInterval = 600;

// Buzzer sequence variables
bool buzzerActive = false;
unsigned long buzzerStartTime = 0;
int currentBeepIndex = 0;
const int LOW_TONE = 440;
const int HIGH_TONE = 1200;
const int BEEP_DURATION = 250;  // milliseconds
const int BEEP_GAP = 150;       // milliseconds between beeps

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
  loadClockDigits();
}

void loop() {
  readKeypad();
  updateBlink();
  updateTimer();
  updateBuzzerSequence();
  updateLCD();
  sendToSubsystem();
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
      // Position 4 is AM/PM selector - only allow 1 (AM) or 2 (PM)
      if (cursorPos == 4) {
        if (key == '1') {
          clockPM = false;  // 1 = AM
        } else if (key == '2') {
          clockPM = true;   // 2 = PM
        }
        // Don't move cursor for AM/PM selection
      } else if (validClockDigit(cursorPos, value)) {
        clockDigits[cursorPos] = value;
        moveCursorRight();
      }
    } else {
      if (validTimerDigit(cursorPos, value)) {
        timerDigits[cursorPos] = value;
        moveCursorRight();
      }
    }
  }

  if (key == '*' && settingMode) moveCursorLeft();
  if (key == '#' && settingMode) moveCursorRight();

  // Timer control: Start/Pause
  if (key == 'A' && !displayClock) {
    if (!timerRunning) {
      // FIX: Only calculate from digits if starting fresh (remainingSeconds is 0)
      // This prevents losing the seconds component when resuming from pause
      if (remainingSeconds == 0) {
        remainingSeconds = getTimerSeconds();
      }
      lastSecond = millis();  // Reset timing reference for accurate countdown
    }
    timerRunning = !timerRunning;
  }

  // Reset timer
  if (key == 'B') {
    timerRunning = false;
    remainingSeconds = 0;
    buzzerActive = false;
    noTone(BUZZER);
    for (int i = 0; i < 4; i++) timerDigits[i] = 0;
  }

  // Toggle editing mode
  if (key == 'C') {
    if (settingMode && displayClock) saveClock();
    settingMode = !settingMode;

    if (settingMode) {
      cursorPos = 0;
      if (displayClock) loadClockDigits();
    }
  }

  // Switch between clock and timer display
  if (key == 'D') {
    displayClock = !displayClock;
  }
}

void loadClockDigits() {
  DateTime now = rtc.now();
  clockDigits[0] = now.hour() / 10;
  clockDigits[1] = now.hour() % 10;
  clockDigits[2] = now.minute() / 10;
  clockDigits[3] = now.minute() % 10;
  
  // Load AM/PM setting from EEPROM
  EEPROM.begin(512);  // Initialize EEPROM (512 bytes)
  clockPM = EEPROM.read(EEPROM_AM_PM_ADDRESS) != 0;
}

void saveClock() {
  DateTime now = rtc.now();
  int h = clockDigits[0] * 10 + clockDigits[1];
  int m = clockDigits[2] * 10 + clockDigits[3];
  rtc.adjust(DateTime(now.year(), now.month(), now.day(), h, m, 0));
  
  // Save AM/PM setting to EEPROM
  EEPROM.begin(512);  // Initialize EEPROM (512 bytes)
  EEPROM.write(EEPROM_AM_PM_ADDRESS, clockPM ? 1 : 0);
  EEPROM.commit();  // Commit changes to flash
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
  // Position 4 is AM/PM selector - not a regular digit
  if (pos == 4) return false;
  
  if (pos == 0) return value <= 2;
  if (pos == 1) {
    if (clockDigits[0] == 2) return value <= 3;
    return true;
  }
  if (pos == 2) return value <= 5;
  return true;
}

void moveCursorRight() {
  cursorPos++;
  if (cursorPos > 4) cursorPos = 4;  // Max position now 4 (for AM/PM)
}

void moveCursorLeft() {
  cursorPos--;
  if (cursorPos < 0) cursorPos = 0;
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
    } else {
      timerRunning = false;
      startBuzzerSequence();
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
    
    // Always use the saved clockPM value (user's choice)
    isPM = clockPM;

    printDigit(0, h12 / 10);
    printDigit(1, h12 % 10);

    lcd.setCursor(4, 0);
    lcd.print(":");

    printDigit(2, m / 10);
    printDigit(3, m % 10);

    lcd.print(":");

    if (now.second() < 10) lcd.print("0");
    lcd.print(now.second());

    // Display AM/PM at positions 12-13 with blinking support
    lcd.setCursor(12, 0);
    
    if (settingMode && cursorPos == 4 && !blinkState) {
      lcd.print(" ");  // Blinking cursor on AM/PM
    } else {
      lcd.print(isPM ? "P" : "A");
    }
    
    lcd.print("M");  // Static "M"
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
    
    // Clear AM/PM positions (12-13) when in timer mode
    lcd.setCursor(12, 0);
    lcd.print("  ");
  }
}

void sendToSubsystem() {
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

void startBuzzerSequence() {
  buzzerActive = true;
  buzzerStartTime = millis();
  currentBeepIndex = 0;
  playBeep(0);  // Start with the first beep
}

void playBeep(int beepIndex) {
  // Sequence: Low, High, Low, High
  if (beepIndex % 2 == 0) {
    tone(BUZZER, LOW_TONE, BEEP_DURATION);
  } else {
    tone(BUZZER, HIGH_TONE, BEEP_DURATION);
  }
}

void updateBuzzerSequence() {
  if (!buzzerActive) return;

  unsigned long elapsedTime = millis() - buzzerStartTime;
  unsigned long cycleTime = BEEP_DURATION + BEEP_GAP;
  unsigned long totalSequenceTime = cycleTime * 4;  // 4 beeps total

  if (elapsedTime < totalSequenceTime) {
    int currentCycle = elapsedTime / cycleTime;

    // Check if we need to start a new beep
    if (elapsedTime % cycleTime == 0 && (elapsedTime / cycleTime) != currentBeepIndex) {
      currentBeepIndex = elapsedTime / cycleTime;
      playBeep(currentBeepIndex);
    }
  } else {
    // Sequence finished
    buzzerActive = false;
    noTone(BUZZER);
    currentBeepIndex = 0;
  }
}
