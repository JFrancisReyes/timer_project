#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>
#include <RTClib.h>
#include <EEPROM.h>
#include <WiFi.h>       // WiFi capability
#include <time.h>       // Time functions for NTP

// ==================== WiFi Configuration ====================
// IMPORTANT: Replace with YOUR credentials
const char* ssid = "YOUR_SSID";           // Your WiFi network name
const char* password = "YOUR_PASSWORD";   // Your WiFi password

// NTP Server Configuration
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;             // UTC offset in seconds (0 for UTC)
const int daylightOffset_sec = 0;         // Daylight saving offset in seconds

// Timezone Examples:
// UTC: 0
// EST: -18000 (UTC-5)
// CST: -21600 (UTC-6)
// PST: -28800 (UTC-8)
// CET: 3600 (UTC+1)
// IST: 19800 (UTC+5:30)
// PHT (Philippine Time): 28800 (UTC+8) <-- Philippine Standard Time


// WiFi Connection Timeout
const int WIFI_TIMEOUT_MS = 30000;        // 30 seconds timeout for WiFi connection
unsigned long wifiStartTime = 0;
bool wifiConnected = false;

// ==================== Hardware Definitions ====================
// Create a second I2C bus for RTC
TwoWire I2C_RTC = TwoWire(1);

LiquidCrystal_I2C lcd(0x27, 16, 2);
RTC_DS3231 rtc;

HardwareSerial SubSerial(2);

#define TX_SUB 17
#define RX_SUB 16
#define BUZZER 4

// ==================== State Variables ====================
int timerDigits[4] = {0, 0, 0, 0};
int clockDigits[4] = {0, 0, 0, 0};

int clockCursorPos = 0;  // Cursor position for clock mode (0-3)
int timerCursorPos = 0;  // Cursor position for timer mode (0-3)
int cursorPos = 0;       // Active cursor position (mirror of clock or timer)

bool settingMode = false;
int displayMode = 0;  // 0=clock, 1=timer, 2=MAC address
bool timerRunning = false;

long remainingSeconds = 0;
unsigned long lastSecond = 0;
unsigned long lastClockEditTime = 0;  // Grace period for clock editing

bool blinkState = true;
unsigned long blinkTimer = 0;
const int blinkInterval = 600;

// Status message variables (for temporary button feedback)
int timerStatusMessage = 0;  // 0=none, 1=paused, 2=running, 3=reset, 4=clock mode, 5=timer mode, 6=MAC address mode
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

// 60-minute alert: 3 loud buzzes with 2-second delays
BuzzerNote alert60MinPattern[] = {
  {LOUD_TONE, 300}, {-1, 2000},
  {LOUD_TONE, 300}, {-1, 2000},
  {LOUD_TONE, 300}, {-1, 2000}
};
const int alert60MinPatternLen = 6;

// 45-minute alert: 2 loud buzzes with 2-second delays
BuzzerNote alert45MinPattern[] = {
  {LOUD_TONE, 300}, {-1, 2000},
  {LOUD_TONE, 300}, {-1, 2000}
};
const int alert45MinPatternLen = 4;

// 30-minute alert: 4 loud buzzes with 2-second delays
BuzzerNote alert30MinPattern[] = {
  {LOUD_TONE, 300}, {-1, 2000},
  {LOUD_TONE, 300}, {-1, 2000},
  {LOUD_TONE, 300}, {-1, 2000},
  {LOUD_TONE, 300}, {-1, 2000}
};
const int alert30MinPatternLen = 8;

// 15-minute alert: 3 loud beeps + 2 soft beeps with 2-second delays
BuzzerNote alert15MinPattern[] = {
  {LOUD_TONE, 300}, {-1, 2000},
  {LOUD_TONE, 300}, {-1, 2000},
  {LOUD_TONE, 300}, {-1, 2000},
  {LOW_TONE, 300}, {-1, 2000},
  {LOW_TONE, 300}, {-1, 2000}
};
const int alert15MinPatternLen = 10;

// 10-minute alert: 5 loud beeps with 2-second delays
BuzzerNote alert10MinPattern[] = {
  {LOUD_TONE, 300}, {-1, 2000},
  {LOUD_TONE, 300}, {-1, 2000},
  {LOUD_TONE, 300}, {-1, 2000},
  {LOUD_TONE, 300}, {-1, 2000},
  {LOUD_TONE, 300}, {-1, 2000}
};
const int alert10MinPatternLen = 10;

// 5-minute alert: 6 loud beeps with 2-second delays
BuzzerNote alert5MinPattern[] = {
  {LOUD_TONE, 300}, {-1, 2000},
  {LOUD_TONE, 300}, {-1, 2000},
  {LOUD_TONE, 300}, {-1, 2000},
  {LOUD_TONE, 300}, {-1, 2000},
  {LOUD_TONE, 300}, {-1, 2000},
  {LOUD_TONE, 300}, {-1, 2000}
};
const int alert5MinPatternLen = 12;

// 1-minute alert: 7 very loud beeps with 2-second delays
BuzzerNote alert1MinPattern[] = {
  {VERY_LOUD_TONE, 300}, {-1, 2000},
  {VERY_LOUD_TONE, 300}, {-1, 2000},
  {VERY_LOUD_TONE, 300}, {-1, 2000},
  {VERY_LOUD_TONE, 300}, {-1, 2000},
  {VERY_LOUD_TONE, 300}, {-1, 2000},
  {VERY_LOUD_TONE, 300}, {-1, 2000},
  {VERY_LOUD_TONE, 300}, {-1, 2000}
};
const int alert1MinPatternLen = 14;

// Completion alert: 5 very loud beeps, pause, then 3 very loud beeps
BuzzerNote completionPattern[] = {
  {VERY_LOUD_TONE, 300}, {-1, 2000},
  {VERY_LOUD_TONE, 300}, {-1, 2000},
  {VERY_LOUD_TONE, 300}, {-1, 2000},
  {VERY_LOUD_TONE, 300}, {-1, 2000},
  {VERY_LOUD_TONE, 300}, {-1, 3000},
  {VERY_LOUD_TONE, 300}, {-1, 2000},
  {VERY_LOUD_TONE, 300}, {-1, 2000},
  {VERY_LOUD_TONE, 300}, {-1, 2000}
};
const int completionPatternLen = 16;

int currentPatternIndex = 0;
unsigned long patternNoteStart = 0;

// Alert tracking
bool alert60MinTriggered = false;
bool alert45MinTriggered = false;
bool alert30MinTriggered = false;
bool alert15MinTriggered = false;
bool alert10MinTriggered = false;
bool alert5MinTriggered = false;
bool alert1MinTriggered = false;

// ==================== Keypad Configuration ====================
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

// ==================== Setup ====================
void setup() {
  Serial.begin(115200);
  delay(1000);  // Wait for serial to stabilize
  
  Serial.println("\n\n========== TIMER SYSTEM V3 (WiFi+NTP) STARTING ==========");
  
  // Initialize I2C Bus 0 for LCD on GPIO 21,22
  Wire.begin(21, 22);
  
  // Initialize I2C Bus 1 for RTC on GPIO 18,19
  I2C_RTC.begin(18, 19, 100000);
  
  lcd.init();
  lcd.backlight();
  lcd.print("Initializing...");
  
  // Initialize RTC with second I2C bus
  rtc.begin(&I2C_RTC);
  
  if (!rtc.begin(&I2C_RTC)) {
    Serial.println("ERROR: RTC DS3231 not found!");
    lcd.clear();
    lcd.print("RTC ERROR");
  } else {
    Serial.println("RTC DS3231 initialized successfully!");
  }
  
  SubSerial.begin(115200, SERIAL_8N1, RX_SUB, TX_SUB);
  pinMode(BUZZER, OUTPUT);
  
  // ==================== WiFi + NTP Initialization ====================
  Serial.println("\n--- WiFi & NTP Initialization ---");
  lcd.clear();
  lcd.print("WiFi: Connecting");
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  wifiStartTime = millis();
  
  // Wait for WiFi connection with timeout
  int wifiAttempts = 0;
  while (WiFi.status() != WL_CONNECTED && (millis() - wifiStartTime) < WIFI_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");
    wifiAttempts++;
    if (wifiAttempts % 4 == 0) {
      lcd.clear();
      lcd.print("Connecting.");
      lcd.print(wifiAttempts / 2);
    }
  }
  
  // Check if connected
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("\nWiFi connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    
    // Configure time with NTP
    Serial.println("Syncing time from NTP server...");
    lcd.clear();
    lcd.print("NTP: Syncing");
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer, "time.nist.gov");
    
    // Wait for time to be set (NTP sync)
    time_t now = time(nullptr);
    int ntpAttempts = 0;
    while (now < 24 * 3600 && ntpAttempts < 20) {
      delay(500);
      Serial.print("*");
      now = time(nullptr);
      ntpAttempts++;
    }
    Serial.println();
    
    if (now > 24 * 3600) {
      Serial.println("Time synchronized successfully!");
      Serial.print("Current time: ");
      Serial.println(ctime(&now));
      
      // Update DS3231 RTC with NTP time
      struct tm timeinfo;
      localtime_r(&now, &timeinfo);
      rtc.adjust(DateTime(
        timeinfo.tm_year + 1900,
        timeinfo.tm_mon + 1,
        timeinfo.tm_mday,
        timeinfo.tm_hour,
        timeinfo.tm_min,
        timeinfo.tm_sec
      ));
      
      Serial.println("DS3231 RTC updated with NTP time!");
      lcd.clear();
      lcd.print("NTP: SUCCESS");
      delay(2000);
    } else {
      Serial.println("ERROR: NTP sync timeout!");
      lcd.clear();
      lcd.print("NTP: TIMEOUT");
      delay(2000);
    }
  } else {
    wifiConnected = false;
    Serial.println("\nWiFi connection failed!");
    Serial.println("Using DS3231 time as fallback...");
    lcd.clear();
    lcd.print("WiFi: FAILED");
    delay(2000);
  }
  
  // Disconnect WiFi when done to save power
  WiFi.disconnect(true);  // true = turn off WiFi radio
  Serial.println("WiFi disconnected (radio off for power saving)");
  
  loadClockDigits();
  
  Serial.println("========== SETUP COMPLETE ==========\n");
  lcd.clear();
}

// ==================== Main Loop ====================
void loop() {
  // SAFETY CHECK: Ensure cursor positions never go out of bounds
  if (displayMode == 0) {
    if (clockCursorPos < 0 || clockCursorPos > 3) {
      Serial.println("WARNING: clockCursorPos out of bounds! Resetting to 0");
      clockCursorPos = 0;
    }
    cursorPos = clockCursorPos;
  } else if (displayMode == 1) {
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

// ==================== Core Functions (Same as V2) ====================

void updateBlink() {
  if (millis() - blinkTimer > blinkInterval) {
    blinkTimer = millis();
    blinkState = !blinkState;
  }
}

void readKeypad() {
  char key = keypad.getKey();
  if (!key) return;

  // Numeric input: 0-9
  if (key >= '0' && key <= '9' && settingMode) {
    int value = key - '0';

    if (displayMode == 0) {
      if (validClockDigit(clockCursorPos, value)) {
        clockDigits[clockCursorPos] = value;
        moveCursorRight();
      }
    } else if (displayMode == 1) {
      if (validTimerDigit(timerCursorPos, value)) {
        timerDigits[timerCursorPos] = value;
        moveCursorRight();
      }
    }
  }

  if (key == '*' && settingMode) moveCursorLeft();
  if (key == '#' && settingMode) moveCursorRight();

  // Timer control: Start/Pause
  if (key == 'A' && displayMode == 1) {
    if (!timerRunning) {
      if (remainingSeconds == 0) {
        remainingSeconds = getTimerSeconds();
        buzzerPatternType = 1;
        buzzerActive = true;
        buzzerStartTime = millis();
        patternNoteStart = millis();
        currentPatternIndex = 0;
        alert10MinTriggered = false;
        alert1MinTriggered = false;
      }
      lastSecond = millis();
    } else {
      buzzerPatternType = 2;
      buzzerActive = true;
      buzzerStartTime = millis();
      patternNoteStart = millis();
      currentPatternIndex = 0;
    }
    timerRunning = !timerRunning;
    timerStatusMessage = timerRunning ? 2 : 1;
    statusMessageTime = millis();
  }

  // Reset timer
  if (key == 'B') {
    if (settingMode) {
      settingMode = false;
    } else {
      timerRunning = false;
      remainingSeconds = 0;
      buzzerActive = false;
      noTone(BUZZER);
      for (int i = 0; i < 4; i++) timerDigits[i] = 0;
      timerStatusMessage = 3;
      statusMessageTime = millis();
    }
  }

  // Toggle editing mode
  if (key == 'C') {
    if (settingMode && displayMode == 0) saveClock();
    settingMode = !settingMode;

    if (settingMode) {
      if (displayMode == 1 && timerRunning) {
        timerRunning = false;
        timerStatusMessage = 1;
        statusMessageTime = millis();
      }
      if (displayMode == 0) {
        clockCursorPos = 0;
        cursorPos = clockCursorPos;
        loadClockDigits();
      } else if (displayMode == 1) {
        timerCursorPos = 0;
        cursorPos = timerCursorPos;
      }
    }
  }

  // Cycle through display modes: Clock -> Timer -> MAC Address -> Clock
  if (key == 'D') {
    if (settingMode) {
      settingMode = false;
    } else {
      displayMode = (displayMode + 1) % 3;  // Cycle through 0, 1, 2
      
      if (displayMode == 0) {
        cursorPos = clockCursorPos;
        loadClockDigits();
        timerStatusMessage = 4;  // "CLOCK MODE"
      } else if (displayMode == 1) {
        cursorPos = timerCursorPos;
        timerStatusMessage = 5;  // "TIMER MODE"
      } else if (displayMode == 2) {
        timerStatusMessage = 6;  // "MAC ADDRESS"
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
  if (displayMode == 0) {
    clockCursorPos++;
    if (clockCursorPos > 3) clockCursorPos = 3;
    cursorPos = clockCursorPos;
  } else if (displayMode == 1) {
    timerCursorPos++;
    if (timerCursorPos > 3) timerCursorPos = 3;
    cursorPos = timerCursorPos;
  }
}

void moveCursorLeft() {
  if (displayMode == 0) {
    clockCursorPos--;
    if (clockCursorPos < 0) clockCursorPos = 0;
    cursorPos = clockCursorPos;
  } else if (displayMode == 1) {
    timerCursorPos--;
    if (timerCursorPos < 0) timerCursorPos = 0;
    cursorPos = timerCursorPos;
  }
}

long getTimerSeconds() {
  int h = timerDigits[0] * 10 + timerDigits[1];
  int m = timerDigits[2] * 10 + timerDigits[3];
  return h * 3600L + m * 60L;
}

void convert24to12(int hour24, int &hour12, bool &isPM) {
  isPM = (hour24 >= 12);
  if (hour24 == 0) {
    hour12 = 12;
  } else if (hour24 <= 12) {
    hour12 = hour24;
  } else {
    hour12 = hour24 - 12;
  }
}

void updateTimer() {
  if (!timerRunning) return;

  if (millis() - lastSecond >= 1000) {
    lastSecond += 1000;

    if (remainingSeconds > 0) {
      remainingSeconds--;

      int h = remainingSeconds / 3600;
      int m = (remainingSeconds % 3600) / 60;

      timerDigits[0] = h / 10;
      timerDigits[1] = h % 10;
      timerDigits[2] = m / 10;
      timerDigits[3] = m % 10;

      // Check for 60-minute threshold (3600 seconds)
      if (remainingSeconds == 3600 && !alert60MinTriggered) {
        alert60MinTriggered = true;
        buzzerPatternType = 3;  // 60-min alert
        buzzerActive = true;
        buzzerStartTime = millis();
        patternNoteStart = millis();
        currentPatternIndex = 0;
      }
      
      // Check for 45-minute threshold (2700 seconds)
      if (remainingSeconds == 2700 && !alert45MinTriggered) {
        alert45MinTriggered = true;
        buzzerPatternType = 4;  // 45-min alert
        buzzerActive = true;
        buzzerStartTime = millis();
        patternNoteStart = millis();
        currentPatternIndex = 0;
      }
      
      // Check for 30-minute threshold (1800 seconds)
      if (remainingSeconds == 1800 && !alert30MinTriggered) {
        alert30MinTriggered = true;
        buzzerPatternType = 5;  // 30-min alert
        buzzerActive = true;
        buzzerStartTime = millis();
        patternNoteStart = millis();
        currentPatternIndex = 0;
      }
      
      // Check for 15-minute threshold (900 seconds)
      if (remainingSeconds == 900 && !alert15MinTriggered) {
        alert15MinTriggered = true;
        buzzerPatternType = 6;  // 15-min alert
        buzzerActive = true;
        buzzerStartTime = millis();
        patternNoteStart = millis();
        currentPatternIndex = 0;
      }
      
      // Check for 10-minute threshold (600 seconds)
      if (remainingSeconds == 600 && !alert10MinTriggered) {
        alert10MinTriggered = true;
        buzzerPatternType = 7;  // 10-min alert
        buzzerActive = true;
        buzzerStartTime = millis();
        patternNoteStart = millis();
        currentPatternIndex = 0;
      }
      
      // Check for 5-minute threshold (300 seconds)
      if (remainingSeconds == 300 && !alert5MinTriggered) {
        alert5MinTriggered = true;
        buzzerPatternType = 8;  // 5-min alert
        buzzerActive = true;
        buzzerStartTime = millis();
        patternNoteStart = millis();
        currentPatternIndex = 0;
      }
      
      // Check for 1-minute threshold (60 seconds)
      if (remainingSeconds == 60 && !alert1MinTriggered) {
        alert1MinTriggered = true;
        buzzerPatternType = 9;  // 1-min alert
        buzzerActive = true;
        buzzerStartTime = millis();
        patternNoteStart = millis();
        currentPatternIndex = 0;
      }
    } else {
      timerRunning = false;
      buzzerPatternType = 10;  // Completion sequence
      buzzerActive = true;
      buzzerStartTime = millis();
      patternNoteStart = millis();
      currentPatternIndex = 0;
    }
  }
}

String getMacAddress() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  String macStr = "";
  for (int i = 0; i < 6; i++) {
    if (mac[i] < 16) macStr += "0";
    macStr += String(mac[i], HEX);
    if (i < 5) macStr += ":";
  }
  return macStr;
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

  if (displayMode == 0) {
    // Clock Mode
    DateTime now = rtc.now();

    lcd.print("C ");

    int h = settingMode ? clockDigits[0] * 10 + clockDigits[1] : now.hour();
    int m = settingMode ? clockDigits[2] * 10 + clockDigits[3] : now.minute();

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
  } else if (displayMode == 1) {
    // Timer Mode
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
  } else if (displayMode == 2) {
    // MAC Address Display Mode
    lcd.print("MAC:");
    String macAddr = getMacAddress();
    // Display first 12 characters (enough for MAC address)
    lcd.print(macAddr.substring(0, 12));
  }

  // Display bottom row: SET MODE or Status Message or blank
  lcd.setCursor(0, 1);

  if (timerStatusMessage != 0 && (millis() - statusMessageTime > MESSAGE_DURATION)) {
    timerStatusMessage = 0;
  }

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
  } else if (timerStatusMessage == 6) {
    lcd.print("MAC ADDRESS     ");
  } else {
    lcd.print("                ");
  }
}

void sendToSubsystem() {
  int d0, d1, d2, d3;

  if (displayMode == 0) {
    if (settingMode) {
      d0 = clockDigits[0];
      d1 = clockDigits[1];
      d2 = clockDigits[2];
      d3 = clockDigits[3];
    } else {
      DateTime now = rtc.now();
      int h12;
      bool isPM;
      convert24to12(now.hour(), h12, isPM);
      d0 = h12 / 10;
      d1 = h12 % 10;
      d2 = now.minute() / 10;
      d3 = now.minute() % 10;
    }
  } else if (displayMode == 1) {
    d0 = timerDigits[0];
    d1 = timerDigits[1];
    d2 = timerDigits[2];
    d3 = timerDigits[3];
  } else {
    // MAC address mode - send zeros to subsystem
    d0 = 0;
    d1 = 0;
    d2 = 0;
    d3 = 0;
  }

  SubSerial.print(d0);
  SubSerial.print(d1);
  SubSerial.print(d2);
  SubSerial.print(d3);
  SubSerial.print(settingMode);
  SubSerial.print(cursorPos);
  SubSerial.print('\n');
}

void updateBuzzerSequence() {
  if (!buzzerActive) return;

  BuzzerNote* pattern = nullptr;
  int patternLen = 0;

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
      pattern = alert60MinPattern;
      patternLen = alert60MinPatternLen;
      break;
    case 4:
      pattern = alert45MinPattern;
      patternLen = alert45MinPatternLen;
      break;
    case 5:
      pattern = alert30MinPattern;
      patternLen = alert30MinPatternLen;
      break;
    case 6:
      pattern = alert15MinPattern;
      patternLen = alert15MinPatternLen;
      break;
    case 7:
      pattern = alert10MinPattern;
      patternLen = alert10MinPatternLen;
      break;
    case 8:
      pattern = alert5MinPattern;
      patternLen = alert5MinPatternLen;
      break;
    case 9:
      pattern = alert1MinPattern;
      patternLen = alert1MinPatternLen;
      break;
    case 10:
      pattern = completionPattern;
      patternLen = completionPatternLen;
      break;
    default:
      buzzerActive = false;
      return;
  }

  if (currentPatternIndex >= patternLen) {
    noTone(BUZZER);
    buzzerActive = false;
    currentPatternIndex = 0;
    return;
  }

  BuzzerNote currentNote = pattern[currentPatternIndex];
  unsigned long noteDuration = currentNote.duration;
  unsigned long noteElapsed = millis() - patternNoteStart;

  if (noteElapsed < noteDuration) {
    if (currentNote.frequency > 0) {
      tone(BUZZER, currentNote.frequency);
    } else {
      noTone(BUZZER);
    }
  } else {
    currentPatternIndex++;
    patternNoteStart = millis();
    noTone(BUZZER);
  }
}
