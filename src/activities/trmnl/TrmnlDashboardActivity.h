#pragma once
#include "activities/Activity.h"

class Bitmap;

/**
 * Full-screen TRMNL dashboard mode, launched from the home screen when the
 * device is linked. Fetches the current dashboard image, renders it in the
 * orientation configured in TrmnlStore, then counts down the user-configured
 * refresh interval (5 dots, bottom-left, one fifth of the interval per dot)
 * before fetching the next image. WiFi is turned off between fetches to
 * preserve battery and no button hints are drawn: next/previous fetch a new
 * image immediately, Confirm opens the TRMNL settings screen, and Back exits
 * to the home screen.
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
  // Abandons the rest of the countdown so the next loop() iteration fetches,
  // announcing it with the fetch splash
  void startFetch();
  // Pushes the TRMNL settings screen, restarting the countdown on the way back
  void openSettings();
  void dotOrigin(int index, int& outX, int& outY) const;
  void drawCountdownDots() const;
  void clearDotsFromGrayPlane() const;
  // Paints the cached image (plus the countdown dots) into the framebuffer and
  // pushes it to the panel, running the configured 4-level grayscale pipeline
  // when the cached BMP carries more than one bit per pixel. Returns false when
  // the cached image is missing or unreadable.
  bool drawCachedImage(int pageWidth, int pageHeight);
  // Rewinds the BMP and renders one full pass of it in `mode`. False if the
  // rewind failed, in which case the framebuffer is left untouched.
  bool drawGrayPlane(const Bitmap& bitmap, int x, int y, int pageWidth, int pageHeight,
                     GfxRenderer::RenderMode mode) const;
  // Repaints the plain BW image + dots, the differential baseline the dot ticks
  // refresh against once a grayscale pass has overwritten the framebuffer. On a
  // read failure it leaves a blank frame and raises bwFrameStale instead.
  void restoreBwFrame(const Bitmap& bitmap, int x, int y, int pageWidth, int pageHeight);
  // RENDER_MODE_GRAYSCALE_OEM: absolute planes driven by the OEM 4-level
  // waveform, with no BW base pass and no differential nudge.
  void drawFactoryGrayscale(const Bitmap& bitmap, int x, int y, int pageWidth, int pageHeight);

  State state = FETCHING;
  bool usedWifi = false;  // A fetch cycle ran; silentRestart() on exit
  // Show the full-screen "fetching" splash for the fetch that is about to run.
  // Set for the first fetch and for button-triggered ones, where the user needs
  // to see that the press was registered; the automatic refreshes at the end of
  // the countdown leave the current dashboard on screen instead.
  bool showFetchSplash = true;
  bool fullRedraw = true;           // Redraw the bitmap from SD vs dots-only update
  bool bwFrameStale = false;        // BW baseline rebuild failed; force a full redraw
  bool consumeBackRelease = false;  // Swallow the Back release the settings screen left behind
  // Interval between dot ticks, i.e. 1/COUNTDOWN_DOTS of the configured
  // refresh interval. Read from TRMNL_STORE at the start of each fetch cycle
  // so a mid-countdown setting change takes effect on the next fetch.
  unsigned long tickIntervalMs = 0;
  uint8_t dotsRemaining = COUNTDOWN_DOTS;
  uint8_t renderedDots = 0xFF;  // Last dot count painted; skips no-op refreshes
  unsigned long lastTickAt = 0;
};
