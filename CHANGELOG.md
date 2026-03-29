# Changelog

All notable changes to this project will be documented in this file.

## [0.2.0] - 2026-03-30

### Removed
- **AM/PM Functionality**: Completely removed AM/PM time format support from both display and editing
  - Removed `clockPM` variable from main system
  - Removed EEPROM storage for AM/PM preference (address 0)
  - Removed AM/PM auto-sync logic on hour transitions
  - Removed AM/PM cursor position (position 4) from clock editing
  - Removed "P"/"A" display indicator from LCD
  - Removed "M" suffix from time display

### Changed
- **Clock Display Format**: Now displays pure 12-hour format (HH:MM:SS) without AM/PM designation
- **Clock Cursor Positions**: Reduced from 5 positions (0-4) to 4 positions (0-3) for editing HH:MM only
- **Validation Functions**: Updated `validClockDigit()` to only validate positions 0-3

### Technical Details
- Commit: `ad3bc1c` - "Removal of AM and PM"
- Files Modified: `main_device_optimizedV2.ino`
- Lines Changed: 6 insertions(+), 72 deletions(-)

---

## [0.1.0] - 2026-03-29

### Initial Version
- 12-hour clock display with AM/PM support
- Timer with duration input (HH:MM)
- Multiple buzzer alert patterns (startup, pause, 10-min, 1-min, completion)
- LCD 16x2 display with status messages
- 4x4 Keypad control
- 4-digit 7-segment subsystem display
- Dual I2C bus configuration for LCD and RTC
- EEPROM-backed AM/PM preference storage
