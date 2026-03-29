# DEVLOG - Timer Project Development Notes

## Project Overview
Multi-device Arduino/ESP32 timer system with:
- **Main Device**: ESP32 with LCD display, RTC, keypad input, buzzer, and subsystem control
- **Subsystem**: ESP32 with 4x 7-segment LED displays showing synchronized time/timer data

---

## Architecture

### Main System (`main_device_optimizedV2.ino`)
**Purpose**: Master controller managing user input, time/timer logic, and system coordination

**Hardware**:
- ESP32 microcontroller
- LCD 16x2 display (I2C address 0x27)
- RTC DS3231 (I2C) for accurate timekeeping
- 4x4 Matrix Keypad for user input
- Buzzer (GPIO 4) for audio alerts

**I2C Bus Configuration**:
- Bus 0 (GPIO 21, 22): LCD display
- Bus 1 (GPIO 18, 19): RTC module (dedicated for isolation)

**Core Modes**:
1. **Clock Display Mode**: Shows current time in 12-hour format (HH:MM:SS)
2. **Timer Mode**: Shows countdown timer (HH:MM:SS)
3. **Setting Mode**: Allows editing of time or timer values

**Key Variables**:
- `timerDigits[4]`: Timer hours/minutes storage
- `clockDigits[4]`: Clock hours/minutes storage
- `remainingSeconds`: Active timer countdown value
- `timerRunning`: Timer state flag
- `displayClock`: Mode selector (true=clock, false=timer)
- `settingMode`: Edit mode flag
- `blinkState`: Cursor blink toggle (600ms interval)

### Subsystem (`subsystem_optimized.ino`)
**Purpose**: Slave display controller receiving formatted data from main system

**Hardware**:
- ESP32 microcontroller
- 4x 7-segment LED displays controlled via BCD pins
- UART2 (115200 baud) for serial communication with main device

**Pin Configuration**:
- Display 1: GPIO 13, 12, 14, 27 (A, B, C, D)
- Display 2: GPIO 26, 25, 33, 32 (A, B, C, D)
- Display 3: GPIO 18, 19, 21, 22 (A, B, C, D)
- Display 4: GPIO 23, 5, 17, 16 (A, B, C, D)

**Display Logic**:
- Uses 4-bit BCD encoding (pins A-D represent bits 0-3)
- Supports cursor blinking in setting mode
- Refreshes all 4 displays continuously in `updateDisplay()`

---

## Communication Protocol

### Main → Subsystem
- **Format**: `d0d1d2d3settingModecursorPos\n` (6 chars + newline)
- **Speed**: 115200 baud via UART2
- **Example**: `123451\n` = Display 1,2,3,4 | settingMode=1 | cursorPos=1
- **Boot Delay**: 3000ms grace period to avoid garbage data during power stabilization

---

## User Interface

### Keypad Layout (4x4 Matrix)
```
1  2  3  A (Start/Pause)
4  5  6  B (Reset)
7  8  9  C (Set Mode)
*  0  #  D (Clock/Timer Toggle)
```

**Key Bindings**:
- **Digits 0-9**: Numeric input in setting mode
- **\***: Move cursor left
- **#**: Move cursor right
- **A**: Toggle timer start/pause
- **B**: Reset timer
- **C**: Toggle setting mode
- **D**: Switch between clock and timer display

### LCD Display (16x2)
**Line 0 (Clock Mode)**: `C HH:MM:SS` (12-hour format)
**Line 0 (Timer Mode)**: `T HH:MM:SS` with status indicator (G=running, P=paused)
**Line 1**: Status messages or "SET MODE"

**Status Messages**:
- `TIMER IS PAUSED` (10s)
- `TIMER IS RUNNING` (10s)
- `TIMER RESET` (10s)
- `CLOCK MODE` (10s)
- `TIMER MODE` (10s)

---

## Buzzer Alert Patterns

### Pattern Types
1. **Startup (ID: 1)**: 3x loud beeps (300ms each, 700ms silence)
2. **Pause (ID: 2)**: Low-High-Low-High sequence (8 notes)
3. **10-Min Alert (ID: 3)**: 5x loud beeps (300ms each, 650ms silence)
4. **1-Min Alert (ID: 4)**: 10x very-loud + 2x loud beeps (intense warning)
5. **Completion (ID: 5)**: Cheerful sequence (6 notes, mixed frequencies)

**Frequencies**:
- Low: 800 Hz
- High: 2800 Hz
- Loud: 3800 Hz
- Very Loud: 4200 Hz (1-min warning)

---

## State Management

### Reset Strategy
Both systems implement `resetAllStates()` to handle power-loss recovery:
- Clears all volatile variables to safe defaults
- Initializes all outputs/displays safely
- Flushes serial buffers
- Prevents corruption from unexpected power interruption

### Debouncing
- **Keypad**: 80ms minimum between valid key presses
- **Blink State**: 600ms toggle interval for cursor visibility

---

## Recent Changes (v0.2.0)

### AM/PM Removal
- **Motivation**: Simplified UI, reduced code complexity, cleaner display
- **Changes**: 
  - Removed `clockPM` variable
  - Removed EEPROM persistence layer
  - Removed AM/PM auto-sync on hour transitions
  - Removed cursor position 4 from clock editing
  - Display now shows pure 12-hour format (HH:MM:SS) without designation
- **Code Impact**: 72 lines removed, 6 lines added (net -66 lines)
- **Testing Notes**: Verify 12-hour conversion still works correctly for all times

---

## Testing Checklist

- [ ] Clock displays correctly in 12-hour format
- [ ] Timer counts down accurately
- [ ] Buzzer patterns play at correct intervals (10-min, 1-min, completion)
- [ ] Keypad input responsive with debouncing
- [ ] Setting mode cursor navigation works (0-3 for clock, 0-3 for timer)
- [ ] Subsystem displays sync with main system
- [ ] Power-on boot cycle stable (settle 3s)
- [ ] Serial communication protocol verified
- [ ] Status messages display and clear correctly
- [ ] Blink state consistent across modes

---

## Known Issues / TODO

- [ ] Verify RTC accuracy over extended periods
- [ ] Test edge cases: midnight transitions, timer expires while in setting mode
- [ ] Consider adding persistence for timer state across power cycles
- [ ] Add diagnostics/debug mode for troubleshooting communication

---

## Development Environment

- **Platform**: Arduino IDE / PlatformIO
- **Target Boards**: ESP32 (WROOM-32)
- **Libraries**:
  - Wire (I2C communication)
  - LiquidCrystal_I2C (LCD driver)
  - Keypad (Matrix keypad)
  - RTClib (RTC DS3231 driver)
  - EEPROM (Persistent storage)

---

*Last Updated: 2026-03-30*
