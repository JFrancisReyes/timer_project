#define RXD2 15
#define TXD2 4

int D1_A = 13;
int D1_B = 12;
int D1_C = 14;
int D1_D = 27;

int D2_A = 26;
int D2_B = 25;
int D2_C = 33;
int D2_D = 32;

int D3_A = 18;
int D3_B = 19;
int D3_C = 21;
int D3_D = 22;

int D4_A = 23;
int D4_B = 5;
int D4_C = 17;
int D4_D = 16;

int digits[4] = {0, 0, 0, 0};

bool settingMode = false;
int cursorPos = 0;

bool blinkState = true;
unsigned long blinkTimer = 0;
const int blinkInterval = 600;

char buffer[12];
int bufferIndex = 0;

// Array of pin configurations for cleaner code
struct DisplayPins {
  int A, B, C, D;
};

DisplayPins displayPins[4] = {
  {D1_A, D1_B, D1_C, D1_D},
  {D2_A, D2_B, D2_C, D2_D},
  {D3_A, D3_B, D3_C, D3_D},
  {D4_A, D4_B, D4_C, D4_D}
};

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2);

  // Initialize all pins in a loop for cleaner code
  for (int i = 0; i < 4; i++) {
    pinMode(displayPins[i].A, OUTPUT);
    pinMode(displayPins[i].B, OUTPUT);
    pinMode(displayPins[i].C, OUTPUT);
    pinMode(displayPins[i].D, OUTPUT);
  }
}

void loop() {
  readSerial();
  updateBlink();
  updateDisplay();
}

void updateBlink() {
  if (millis() - blinkTimer > blinkInterval) {
    blinkTimer = millis();
    blinkState = !blinkState;
  }
}

void readSerial() {
  while (Serial2.available()) {
    char c = Serial2.read();

    if (c == '\n') {
      buffer[bufferIndex] = 0;

      // Parse incoming data
      digits[0] = buffer[0] - '0';
      digits[1] = buffer[1] - '0';
      digits[2] = buffer[2] - '0';
      digits[3] = buffer[3] - '0';

      settingMode = buffer[4] - '0';
      cursorPos = buffer[5] - '0';

      bufferIndex = 0;
    } else {
      buffer[bufferIndex++] = c;
    }
  }
}

void setBCD(int val, int A, int B, int C, int D) {
  digitalWrite(A, val & 1);
  digitalWrite(B, (val >> 1) & 1);
  digitalWrite(C, (val >> 2) & 1);
  digitalWrite(D, (val >> 3) & 1);
}

void blankSegments(int A, int B, int C, int D) {
  // Turn off all segments by setting all pins to HIGH (for common cathode decoder)
  digitalWrite(A, HIGH);
  digitalWrite(B, HIGH);
  digitalWrite(C, HIGH);
  digitalWrite(D, HIGH);
}

void showDigit(int index, int value, int A, int B, int C, int D) {
  // Blink OFF: blank all segments regardless of digit value
  if (settingMode && index == cursorPos && !blinkState)
    blankSegments(A, B, C, D);
  else
    setBCD(value, A, B, C, D);
}

void updateDisplay() {
  // Update all 4 displays using the pin array
  for (int i = 0; i < 4; i++) {
    showDigit(i, digits[i], displayPins[i].A, displayPins[i].B, displayPins[i].C, displayPins[i].D);
  }
}
