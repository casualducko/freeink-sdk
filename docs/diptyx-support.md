# Diptyx dual-screen e-reader support

`-DFREEINK_DEVICE_DIPTYX=1` (`Board::Diptyx`, profile name `diptyx`). **Left panel only** so far.

ESP32-S3 (16 MB quad flash, 8 MB octal PSRAM), two 648x480 B/W e-paper panels (Waveshare 5.83" B V2
class, UC8179-family command set), SDMMC 1-bit SD, seven buttons. Hardware: <https://diptyx.dev>.
Stock firmware (MIT): <https://github.com/MartijndenHoed/Diptyx>.

## Pin evidence

Taken from the stock firmware sources (`epdif`, `epd5in83b_V2`, `device`, `main`) and checked on a unit
with a test app.

| Function | Pins |
|---|---|
| EPD SPI (shared by both panels) | SCLK 11, MOSI 12, no MISO, mode 0, 7 MHz |
| Left panel | CS 10, DC 9, RST 8, BUSY 7 (BUSY high = idle) |
| Right panel (not driven yet) | CS 21, DC 18, RST 17, BUSY 14. CS 21 is held high via `DiptyxDriver::coCs()` |
| SD (SDMMC 1-bit, internal pull-ups) | CLK 41, CMD 40, D0 39 |
| Buttons (active-low, pull-ups), tested | page-left 5, arrow-left 1, joystick press 0 (boot pin), up 2, down 3, arrow-right 4, page-right 6 |
| Power latch | GPIO38 HIGH early in boot (stock also gpio_hold_en) |
| Power button / USB | power button GPIO42 (active-HIGH, pulldown), USB VBUS detect GPIO16 (HIGH = USB present, also TinyUSB `vbus_monitor_io`), USB wake trigger GPIO15 (inverted, unused here). Roles derived from stock `main.cpp`: hold 42 for 3 s with 16 low shuts down; 16+!15 = USB booted |
| Other | status LED 48, rumble 47, battery sense gate 43 (HIGH to sample), battery ADC = ADC2 ch2 = GPIO13 with a 2x divider |

Button mapping in the profile: back = page-left (5), confirm = joystick press (0), right/Next = page-right (6),
left/Prev = joystick-left (1), up/down = joystick up/down (2/3), power = 42 (active-high), USB detect = 16. Joystick-right (4) is
unmapped. The bottom hint bar shows three hints (Back / Select / Next) over the three bottom buttons; the Prev hint is dropped.

## Battery

`BatteryMonitor` reads ADC2 ch2 (GPIO13) with a 2x divider. The divider is only connected while GPIO43 is HIGH, so each read
releases the pad hold, drives GPIO43 high for 20 ms, averages 16 samples, then drops it low and re-holds it (stock
`Device::getBatteryVoltage()`). ADC2 is unreadable while Wi-Fi runs; a failed read keeps the last good value instead of showing 0%.
The percentage uses the SDK's generic Li-ion table, not the stock 6-point table.

## Sleep

Like stock, "sleep" is deep sleep with the GPIO38 latch held (the board stays powered). `PowerManager` wakes it from any of the seven
buttons (EXT1 any-low, RTC pull-ups, GPIO 0-6 are all RTC pins) and waits up to 5 s for a held button to be released first. The power
button (GPIO42) is not an RTC pin and cannot wake deep sleep; USB (GPIO15) is not a wake source yet. A hard power-off (CrossPoint: hold the power button) releases the GPIO38 latch and drives it low, as stock `Device::shutdown()` does; the physical
power button then cold-boots the board, and it has to be held for ~3 s because the rail is only latched once firmware has booted. With USB attached
the rail stays up and the board behaves like standby.

## Panel

`DiptyxDriver` replays the stock sequence: reset, POWER SETTING (0x01) / VCOM 0x82 / BTST 0x06 / PLL 0x30 / 0x52 /
PWS 0xE3 / 0x41, POWER ON, PSR 0x3F,0x09 (register-LUT mode), TRES 648x480, 0x15, CDI 0x18,0x07, TCON 0x22, then the five
42-byte LUT registers (full or partial), new-frame data (0x13), refresh (0x12), POWER OFF. It does this before every
refresh. FULL and HALF requests use the full LUT, FAST uses the partial LUT, and the host schedules the full refreshes
(a safety cap forces one after 30 partials; stock used 5). This differs from `Uc8179Driver`, which uses the OTP waveforms. VCOM is the stock default 23; the stock firmware keeps a
per-unit value in NVS that is not read here.

Verified on hardware: a 1 bit in the data plane is BLACK (framebuffer is inverted on output), and the left panel is
mounted rotated 180 degrees (stock flips it). The glass sits portrait in the case, so the reader's own
portrait orientation handles the 90 degree rotation.

## Not done

Right panel; USB wake; LED/rumble; grayscale (B/W only); VCOM from NVS.
