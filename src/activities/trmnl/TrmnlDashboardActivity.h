#pragma once
#include "activities/Activity.h"

/**
 * Full-screen TRMNL dashboard mode, launched from the home screen when the
 * device is linked. Fetches the current dashboard image, renders it 1:1 in
 * landscape, then counts down the user-configured refresh interval (5 dots,
 * bottom-left, one fifth of the interval per dot) before fetching the next
 * image. WiFi is turned off between fetches to preserve battery and no
 * button hints are drawn; Back or Confirm exits to the home screen.
 */
class TrmnlDashboardActivity final : public Activity {
 public:
  TrmnlDashboardActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("TrmnlDashboard", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

  // The countdown runs with no button activity; without this the main loop
  // would deep-sleep the device after the inactivity timeout.
  bool preventAutoSleep() override { return true; }

 private:
  static constexpr uint8_t COUNTDOWN_DOTS = 5;
  static constexpr unsigned long MINUTE_MS = 60000;

  enum State { FETCHING, SHOWING };

  void fetchImage();
  void drawCountdownDots() const;

  State state = FETCHING;
  bool usedWifi = false;    // A fetch cycle ran; silentRestart() on exit
  bool imageShown = false;  // First image render done (gates the fetch splash)
  bool fullRedraw = true;   // Redraw the bitmap from SD vs dots-only update
  // Interval between dot ticks, i.e. 1/COUNTDOWN_DOTS of the configured
  // refresh interval. Read from TRMNL_STORE at the start of each fetch cycle
  // so a mid-countdown setting change takes effect on the next fetch.
  unsigned long tickIntervalMs = 0;
  uint8_t dotsRemaining = COUNTDOWN_DOTS;
  uint8_t renderedDots = 0xFF;  // Last dot count painted; skips no-op refreshes
  unsigned long lastTickAt = 0;
};
