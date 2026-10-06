#include "DiptyxDriver.h"

#include <BoardConfig.h>
#include <nvs.h>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

#if FREEINK_DRIVER_DIPTYX

namespace freeink {
namespace {
constexpr uint8_t CMD_PANEL_SETTING = 0x00;
constexpr uint8_t CMD_POWER_SETTING = 0x01;
constexpr uint8_t CMD_POWER_OFF = 0x02;
constexpr uint8_t CMD_POWER_ON = 0x04;
constexpr uint8_t CMD_BOOSTER_SOFT_START = 0x06;
constexpr uint8_t CMD_DEEP_SLEEP = 0x07;
constexpr uint8_t CMD_DTM2 = 0x13;  // new image data
constexpr uint8_t CMD_DISPLAY_REFRESH = 0x12;
constexpr uint8_t CMD_UNKNOWN_15 = 0x15;  // stock sends 0x00
constexpr uint8_t CMD_PLL_CONTROL = 0x30;
constexpr uint8_t CMD_TEMP_OFFSET = 0x41;
constexpr uint8_t CMD_VCOM_DATA_INTERVAL = 0x50;
constexpr uint8_t CMD_TCON = 0x60;
constexpr uint8_t CMD_RESOLUTION = 0x61;
constexpr uint8_t CMD_VCOM_DC = 0x82;
constexpr uint8_t CMD_LUT_BASE = 0x20;  // 0x20 VCOM, 0x21 WW, 0x22 KW, 0x23 WK, 0x24 KK
constexpr uint8_t CMD_0x52 = 0x52;
constexpr uint8_t CMD_PWS = 0xE3;

constexpr uint16_t PANEL_W = 648;
constexpr uint16_t PANEL_H = 480;
constexpr uint16_t ROW_BYTES = PANEL_W / 8;  // 81
constexpr uint8_t VCOM_DEFAULT = 23;         // stock default; each unit stores its own value (see readStockVcom)
constexpr uint8_t LUT_LEN = 42;

// Stock waveforms: first 6 bytes of each 42-byte register, remainder zero.
const uint8_t LUT_FULL[5][6] = {{0x00, 0x1E, 0x1E, 0x1E, 0x01, 0x01}, {0x60, 0x1E, 0x1E, 0x1E, 0x01, 0x01},
                                {0x60, 0x1E, 0x1E, 0x1E, 0x01, 0x01}, {0x64, 0x1E, 0x1E, 0x1E, 0x01, 0x01},
                                {0x24, 0x1E, 0x1E, 0x1E, 0x01, 0x01}};
const uint8_t LUT_PARTIAL[5][6] = {{0x00, 0x14, 0x01, 0x00, 0x00, 0x01}, {0x00, 0x14, 0x01, 0x00, 0x00, 0x01},
                                   {0x80, 0x14, 0x01, 0x00, 0x00, 0x01}, {0x40, 0x14, 0x01, 0x00, 0x00, 0x01},
                                   {0x00, 0x14, 0x01, 0x00, 0x00, 0x01}};

// Each Diptyx stores its own panel voltage (VCOM) in the stock firmware's settings: NVS namespace "device", key
// "settings", a JSON blob containing "vcomLeft" / "vcomRight". Read-only; falls back to the stock default when the
// settings are missing (a unit that never ran the stock firmware) or out of range. Needs nvs_flash_init() to have run,
// which the Arduino core does before setup().
uint8_t readStockVcom(DiptyxDriver::Side side) {
  const char* key = side == DiptyxDriver::Side::Left ? "\"vcomLeft\"" : "\"vcomRight\"";
  uint8_t value = VCOM_DEFAULT;
  nvs_handle_t handle;
  if (nvs_open("device", NVS_READONLY, &handle) != ESP_OK) return value;
  size_t len = 0;
  if (nvs_get_blob(handle, "settings", nullptr, &len) == ESP_OK && len > 0 && len <= 4096) {
    std::unique_ptr<char[]> json(new (std::nothrow) char[len + 1]);
    if (json && nvs_get_blob(handle, "settings", json.get(), &len) == ESP_OK) {
      json[len] = '\0';
      if (const char* p = strstr(json.get(), key)) {
        p = strchr(p + strlen(key), ':');
        const long v = p ? strtol(p + 1, nullptr, 10) : 0;
        if (v >= 1 && v <= 127) value = static_cast<uint8_t>(v);
      }
    }
  }
  nvs_close(handle);
  return value;
}

uint8_t reverseBits(uint8_t b) {
  b = static_cast<uint8_t>((b & 0xF0) >> 4 | (b & 0x0F) << 4);
  b = static_cast<uint8_t>((b & 0xCC) >> 2 | (b & 0x33) << 2);
  b = static_cast<uint8_t>((b & 0xAA) >> 1 | (b & 0x55) << 1);
  return b;
}
}  // namespace

EpdPins DiptyxDriver::pins(Side side) {
  // SCLK MOSI CS DC RST BUSY
  return side == Side::Left ? EpdPins{11, 12, 10, 9, 8, 7, -1} : EpdPins{11, 12, 21, 18, 17, 14, -1};
}

int8_t DiptyxDriver::coCs() const { return _side == Side::Left ? 21 : 10; }

uint32_t DiptyxDriver::spiHz() const {
  return BoardConfig::ACTIVE.displaySpiHz != 0 ? BoardConfig::ACTIVE.displaySpiHz : 7000000;
}

PanelGeometry DiptyxDriver::geometry() const {
  return {PANEL_W, PANEL_H, ROW_BYTES, static_cast<uint32_t>(ROW_BYTES) * PANEL_H};
}

void DiptyxDriver::sendLuts(EpdBus& bus, bool full) {
  const uint8_t(*lut)[6] = full ? LUT_FULL : LUT_PARTIAL;
  uint8_t buf[LUT_LEN];
  for (uint8_t r = 0; r < 5; r++) {
    for (uint8_t i = 0; i < LUT_LEN; i++) buf[i] = i < 6 ? lut[r][i] : 0x00;
    bus.cmd(CMD_LUT_BASE + r);  // stock sends each byte under its own CS pulse
    for (uint8_t i = 0; i < LUT_LEN; i++) bus.data(buf[i]);
  }
}

// Stock initPhase1 + initPhase2 + LUTs, for one panel.
void DiptyxDriver::initController(EpdBus& bus, bool full) {
  bus.reset(10);
  bus.cmd(CMD_POWER_SETTING);
  bus.data(0x03);
  bus.data(0x17);  // VGH=20V, VGL=-20V
  bus.data(0x3F);  // VDH=15V
  bus.data(0x3F);  // VDL=-15V
  bus.data(0x03);
  bus.cmd(CMD_VCOM_DC);
  bus.data(_vcom);
  bus.cmd(CMD_BOOSTER_SOFT_START);
  bus.data(0x17);
  bus.data(0x17);
  bus.data(0x3D);
  bus.data(0x3C);
  bus.cmd(CMD_PLL_CONTROL);
  bus.data(0x07);
  bus.cmd(CMD_0x52);
  bus.data(0x02);
  bus.cmd(CMD_PWS);
  bus.data(0x88);
  bus.cmd(CMD_TEMP_OFFSET);  // keeps the waveform from slowing down in the cold
  bus.data(0x07);
  bus.cmd(CMD_POWER_ON);
  bus.waitBusy(" diptyx_PON");
  _isScreenOn = true;

  bus.cmd(CMD_PANEL_SETTING);  // register-LUT mode
  bus.data(0x3F);
  bus.data(0x09);
  bus.cmd(CMD_RESOLUTION);
  bus.data(static_cast<uint8_t>(PANEL_W >> 8));
  bus.data(static_cast<uint8_t>(PANEL_W & 0xFF));
  bus.data(static_cast<uint8_t>(PANEL_H >> 8));
  bus.data(static_cast<uint8_t>(PANEL_H & 0xFF));
  bus.cmd(CMD_UNKNOWN_15);
  bus.data(0x00);
  bus.cmd(CMD_VCOM_DATA_INTERVAL);
  bus.data(0x18);
  bus.data(0x07);
  bus.cmd(CMD_TCON);
  bus.data(0x22);
  sendLuts(bus, full);
}

// Left panel mounted rotated 180 degrees: reverse row order, byte order and bit order. The right panel is sent
// as is. Both are inverted (1 = black on this panel). Sent as two CS bursts, like stock.
void DiptyxDriver::writePlane(EpdBus& bus, const uint8_t* fb) {
  bus.cmd(CMD_DTM2);
  uint8_t row[ROW_BYTES];
  const bool flip = _side == Side::Left;
  bus.beginTxn();
  for (uint16_t j = 0; j < PANEL_H; j++) {
    if (j == PANEL_H / 2) {
      bus.endTxn();
      bus.beginTxn();
    }
    if (flip) {
      const uint8_t* src = fb + static_cast<uint32_t>(PANEL_H - 1 - j) * ROW_BYTES;
      for (uint16_t i = 0; i < ROW_BYTES; i++) row[i] = static_cast<uint8_t>(~reverseBits(src[ROW_BYTES - 1 - i]));
    } else {
      const uint8_t* src = fb + static_cast<uint32_t>(j) * ROW_BYTES;
      for (uint16_t i = 0; i < ROW_BYTES; i++) row[i] = static_cast<uint8_t>(~src[i]);
    }
    bus.rawWriteBytes(row, ROW_BYTES);
  }
  bus.endTxn();
}

void DiptyxDriver::begin(EpdBus& bus) {
  (void)bus;
  _partialsRemaining = 0;  // first refresh is a full one
  _isScreenOn = false;
  _vcom = readStockVcom(_side);
  if (Serial) {
    Serial.printf("[%lu] [DIPTYX] %s panel VCOM %u\n", millis(), _side == Side::Left ? "left" : "right", _vcom);
  }
}

void DiptyxDriver::display(EpdBus& bus, const uint8_t* fb, const uint8_t* prev, RefreshMode mode, bool turnOff) {
  (void)prev;
  // turnOff carries the Sunlight Fading Fix: with it on, every refresh uses the full waveform (darker blacks, with a
  // flash), whatever the refresh mode asked for.
  const bool full = (mode != RefreshMode::Fast) || _partialsRemaining == 0 || turnOff;
  if (full) {
    _partialsRemaining = kMaxPartialsInRow;
  } else {
    _partialsRemaining--;
  }
  initController(bus, full);
  writePlane(bus, fb);
  bus.cmd(CMD_DISPLAY_REFRESH);
  bus.waitBusy(" diptyx_DRF");
  // The stock firmware powers the panel rail off after every refresh; the next one re-inits anyway.
  bus.cmd(CMD_POWER_OFF);
  bus.waitBusy(" diptyx_POF");
  _isScreenOn = false;
}

void DiptyxDriver::deepSleep(EpdBus& bus) {
  if (_isScreenOn) {
    bus.cmd(CMD_POWER_OFF);
    bus.waitBusy(" diptyx power-down");
    _isScreenOn = false;
  }
  bus.cmd(CMD_DEEP_SLEEP);
  bus.data(0xA5);
}

PanelDriver& diptyxDriver(DiptyxDriver::Side side) {
  static DiptyxDriver left(DiptyxDriver::Side::Left);
  static DiptyxDriver right(DiptyxDriver::Side::Right);
  return side == DiptyxDriver::Side::Left ? static_cast<PanelDriver&>(left) : static_cast<PanelDriver&>(right);
}

}  // namespace freeink

#endif  // FREEINK_DRIVER_DIPTYX
