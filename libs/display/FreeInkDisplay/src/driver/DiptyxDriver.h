#pragma once

// Diptyx panel driver — one 648x480 UC8179-class B/W panel (Waveshare 5.83" B V2 class).
// Black/white only; no grayscale. Sequence and waveforms are the stock Diptyx
// firmware's (epd5in83b_V2.cpp), verified on hardware:
//   reset -> POWER SETTING/VCOM/BTST/PLL/PWS -> POWER ON -> panel settings (register LUT mode,
//   PSR 0x3F) -> LUTs -> new-frame data (0x13) -> refresh (0x12) -> POWER OFF.
// The controller is re-initialised before every refresh, like stock. A FULL refresh uses the full
// LUT; FAST/HALF use the partial LUT, promoted to a full refresh every kPartialsBeforeFull frames.
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

  void begin(EpdBus& bus) override;
  void deepSleep(EpdBus& bus) override;
  void display(EpdBus& bus, const uint8_t* fb, const uint8_t* prev, RefreshMode mode, bool turnOff) override;

 private:
  void initController(EpdBus& bus, bool full);
  void sendLuts(EpdBus& bus, bool full);
  void writePlane(EpdBus& bus, const uint8_t* fb);

  static constexpr uint8_t kPartialsBeforeFull = 5;  // stock firmware's partial budget
  uint8_t _partialsRemaining = 0;                    // 0 forces a full refresh next
  bool _isScreenOn = false;
};

PanelDriver& diptyxDriver();

}  // namespace freeink
