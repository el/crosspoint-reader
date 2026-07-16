#include "TrmnlDashboardActivity.h"

#include <Arduino.h>
#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <WiFi.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "fontIds.h"
#include "network/WifiAutoConnect.h"
#include "trmnl/TrmnlClient.h"
#include "trmnl/TrmnlStore.h"

namespace {
// Headless connect bound: an unreachable network must not stall the dashboard
constexpr uint32_t WIFI_TIMEOUT_MS = 15000;

// Countdown dots, landscape coordinates, bottom-left corner
constexpr int DOT_SIZE = 8;
constexpr int DOT_GAP = 6;
constexpr int DOT_MARGIN = 6;
}  // namespace

void TrmnlDashboardActivity::onEnter() {
  Activity::onEnter();
  // Paint the fetch splash; the first loop() iteration starts the fetch
  requestUpdate();
}

void TrmnlDashboardActivity::onExit() {
  Activity::onExit();
  // WiFi is already off between fetches, but each connect/TLS cycle leaves the
  // heap fragmented; restart to hand a clean heap back to the home screen
  // (same pattern as TrmnlActionActivity)
  if (usedWifi) {
    silentRestart();
  }
}

void TrmnlDashboardActivity::fetchImage() {
  usedWifi = true;
  if (WifiAutoConnect::tryConnectLastNetwork(WIFI_TIMEOUT_MS)) {
    const auto result = TrmnlClient::fetchImageToCache();
    if (result != TrmnlClient::OK) {
      LOG_ERR("TRM", "Dashboard fetch failed (%d), keeping cached image", static_cast<int>(result));
    }
    // Radio off between refreshes to preserve battery
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  } else {
    LOG_ERR("TRM", "Dashboard fetch skipped: no WiFi connection");
  }

  {
    RenderLock lock(*this);
    state = SHOWING;
    // Re-read the setting every cycle so a change made mid-countdown (from
    // the TRMNL settings screen) takes effect starting next fetch
    const uint8_t intervalMinutes = TRMNL_STORE.getRefreshIntervalMinutes();
    tickIntervalMs = (static_cast<unsigned long>(intervalMinutes) * MINUTE_MS) / COUNTDOWN_DOTS;
    dotsRemaining = COUNTDOWN_DOTS;
    fullRedraw = true;
  }
  lastTickAt = millis();
  requestUpdate();
}

void TrmnlDashboardActivity::loop() {
  if (state == FETCHING) {
    // Blocking, bounded by the WiFi/HTTP timeouts; buttons are unresponsive
    // during the fetch (same trade-off as TrmnlActionActivity)
    fetchImage();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    finish();
    return;
  }

  if (millis() - lastTickAt >= tickIntervalMs) {
    lastTickAt += tickIntervalMs;
    {
      RenderLock lock(*this);
      if (dotsRemaining > 0) {
        dotsRemaining--;
      }
      if (dotsRemaining == 0) {
        // Next loop() iteration re-fetches; the empty dots painted below tell
        // the user a refresh is due
        state = FETCHING;
      }
    }
    requestUpdate();
  }
}

void TrmnlDashboardActivity::drawCountdownDots() const {
  const int y = renderer.getScreenHeight() - DOT_MARGIN - DOT_SIZE;
  for (int i = 0; i < COUNTDOWN_DOTS; i++) {
    const int x = renderer.getScreenWidth() - 2 * DOT_MARGIN - (COUNTDOWN_DOTS - i) * (DOT_SIZE + DOT_GAP);
    // White backing + outline keeps the dots readable on any dashboard art
    renderer.fillRoundedRect(x, y, DOT_SIZE, DOT_SIZE, DOT_SIZE / 2, Color::White);
    renderer.drawRoundedRect(x, y, DOT_SIZE, DOT_SIZE, 1, DOT_SIZE / 2, true);
    if (i < dotsRemaining) {
      renderer.fillRoundedRect(x + 2, y + 2, DOT_SIZE - 4, DOT_SIZE - 4, (DOT_SIZE - 4) / 2, Color::Black);
    }
  }
}

void TrmnlDashboardActivity::render(RenderLock&&) {
  // TRMNL dashboards are landscape (800x480, exactly the panel size), so
  // render 1:1 in native landscape like the sleep screen does
  renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  if (state == FETCHING && !imageShown) {
    // First fetch: full-screen status while the network round-trip runs.
    // Re-fetches skip this and keep the current image on screen.
    renderer.clearScreen();
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_TRMNL_FETCHING));
    renderer.displayBuffer();
  } else if (fullRedraw) {
    renderer.clearScreen();
    bool drawn = false;
    HalFile file;
    if (Storage.openFileForRead("TRM", TrmnlStore::cachedImagePath(), file)) {
      Bitmap bitmap(file, true);
      if (bitmap.parseHeaders() == BmpReaderError::Ok) {
        const int x = (pageWidth - bitmap.getWidth()) / 2;
        const int y = (pageHeight - bitmap.getHeight()) / 2;
        renderer.drawBitmap(bitmap, std::max(x, 0), std::max(y, 0), pageWidth, pageHeight, 0, 0);
        drawn = true;
      }
    }
    if (!drawn) {
      // No cached image and the fetch failed; the countdown still runs, so
      // this retries automatically
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_TRMNL_FETCH_FAILED), true, EpdFontFamily::BOLD);
    }
    drawCountdownDots();
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    fullRedraw = false;
    imageShown = true;
    renderedDots = dotsRemaining;
  } else if (renderedDots != dotsRemaining) {
    // Dot tick: only the dot pixels change, so the fast differential
    // refresh updates them without disturbing the rest of the panel
    drawCountdownDots();
    renderer.displayBuffer();
    renderedDots = dotsRemaining;
  }

  // Parent activities render in portrait; don't leak the landscape orientation
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
}
