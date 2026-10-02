#pragma once

// Diptyx panel driver — one 648x480 UC8179-class B/W panel (Waveshare 5.83" B V2 class).
// Black/white only; no grayscale. Sequence and waveforms are the stock Diptyx
// firmware's (epd5in83b_V2.cpp), verified on hardware:
//   reset -> POWER SETTING/VCOM/BTST/PLL/PWS -> POWER ON -> panel settings (register LUT mode,
//   PSR 0x3F) -> LUTs -> new-frame data (0x13) -> refresh (0x12) -> POWER OFF.
// The controller is re-initialised before every refresh, like stock. FULL and HALF refreshes use the full
// LUT (the host schedules them, e.g. CrossPoint's "refresh every N pages"); FAST uses the partial LUT. A high
// safety cap (kMaxPartialsInRow) still forces a full refresh if the host never asks for one.
// A 1 bit in the data plane is BLACK with these settings, so the framebuffer is inverted on the
// way out, and the left panel is mounted rotated 180 degrees (stock flips it the same way).
//
// Selection: linked only when -DFREEINK_DEVICE_DIPTYX (FREEINK_DRIVER_DIPTYX).

#include "PanelDriver.h"

namespace freeink {

class DiptyxDriver : public PanelDriver {
 public:
  uint32_t spiHz() const override;
  BusyPolarity busyPolarity() const override { return BusyPolarity::ActiveLow; }  // BUSY high = idle
  PanelGeometry geometry() const override;
  int8_t coCs() const override { return 21; }  // right panel CS: share SCLK/MOSI, keep it deselected

  void begin(EpdBus& bus) override;
  void deepSleep(EpdBus& bus) override;
  void display(EpdBus& bus, const uint8_t* fb, const uint8_t* prev, RefreshMode mode, bool turnOff) override;

  // B/W only. Callers (the reader's anti-aliasing and image passes) still invoke the grayscale entry points. The
  // PanelDriver defaults would run an ordinary refresh with the facade framebuffer, which at that point holds a
  // gray-plane render, so the page would be replaced by an outline-only image. Do nothing instead: the B/W page
  // shown before the gray pass stays on the panel.
  void displayGray(EpdBus& bus, const uint8_t* fb, bool turnOff, const unsigned char* lut, bool factoryMode) override {
    (void)bus; (void)fb; (void)turnOff; (void)lut; (void)factoryMode;
  }
  void displayGrayCalibration(EpdBus& bus, const uint8_t* fb, uint16_t customX, uint16_t customY, uint16_t customW,
                              uint16_t customH) override {
    (void)bus; (void)fb; (void)customX; (void)customY; (void)customW; (void)customH;
  }

 private:
  void initController(EpdBus& bus, bool full);
  void sendLuts(EpdBus& bus, bool full);
  void writePlane(EpdBus& bus, const uint8_t* fb);

  static constexpr uint8_t kMaxPartialsInRow = 30;  // safety cap; the stock firmware's own budget is 5
  uint8_t _partialsRemaining = 0;                   // 0 forces a full refresh next
  bool _isScreenOn = false;
};

PanelDriver& diptyxDriver();

}  // namespace freeink
