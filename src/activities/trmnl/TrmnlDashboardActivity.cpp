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

void TrmnlDashboardActivity::startFetch() {
  {
    RenderLock lock(*this);
    // The splash confirms the press before the blocking fetch begins; the next
    // loop() iteration then runs it (the same handover the countdown expiry
    // uses).
    showFetchSplash = true;
    state = FETCHING;
  }
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

  // Next/previous skip the rest of the countdown and pull a new image now.
  // This screen has nothing to navigate, so the nav buttons are free to mean
  // "refresh" — and it is the only way to see a dashboard update on demand.
  if (mappedInput.wasReleased(MappedInputManager::Button::NavNext) ||
      mappedInput.wasReleased(MappedInputManager::Button::NavPrevious)) {
    startFetch();
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

void TrmnlDashboardActivity::dotOrigin(const int index, int& outX, int& outY) const {
  outY = renderer.getScreenHeight() - DOT_MARGIN - DOT_SIZE;
  outX = renderer.getScreenWidth() - 2 * DOT_MARGIN - (COUNTDOWN_DOTS - index) * (DOT_SIZE + DOT_GAP);
}

void TrmnlDashboardActivity::drawCountdownDots() const {
  for (int i = 0; i < COUNTDOWN_DOTS; i++) {
    int x, y;
    dotOrigin(i, x, y);
    // White backing + outline keeps the dots readable on any dashboard art
    renderer.fillRoundedRect(x, y, DOT_SIZE, DOT_SIZE, DOT_SIZE / 2, Color::White);
    renderer.drawRoundedRect(x, y, DOT_SIZE, DOT_SIZE, 1, DOT_SIZE / 2, true);
    if (i < dotsRemaining) {
      renderer.fillRoundedRect(x + 2, y + 2, DOT_SIZE - 4, DOT_SIZE - 4, (DOT_SIZE - 4) / 2, Color::Black);
    }
  }
}

void TrmnlDashboardActivity::clearDotsFromGrayPlane() const {
  // A set bit in a gray plane means "nudge this pixel to a gray level". The
  // dots are painted into the BW frame only, so the image's gray pixels behind
  // them would tint them; clearing the plane over each dot keeps them pure
  // black and white. fillRect(state=true) clears the bits.
  for (int i = 0; i < COUNTDOWN_DOTS; i++) {
    int x, y;
    dotOrigin(i, x, y);
    renderer.fillRect(x, y, DOT_SIZE, DOT_SIZE, true);
  }
}

bool TrmnlDashboardActivity::drawCachedImage(const int pageWidth, const int pageHeight) {
  HalFile file;
  if (!Storage.openFileForRead("TRM", TrmnlStore::cachedImagePath(), file)) {
    return false;
  }

  Bitmap bitmap(file, true);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) {
    return false;
  }
  // A 1 bpp cache means the server sent a plain black-and-white BMP, in which
  // case no render mode can produce gray — worth seeing in the log before
  // blaming the waveform.
  LOG_INF("TRM", "Cached image %dx%d, %u bpp (grayscale source: %s)", bitmap.getWidth(), bitmap.getHeight(),
          static_cast<unsigned>(bitmap.getBpp()), bitmap.hasGreyscale() ? "yes" : "no");

  // drawBitmap only ever shrinks, and applies the scale to the pixels but not
  // to the offset, so centering has to be done against the drawn size. This
  // matters in the portrait orientations, where a landscape dashboard (or a
  // server that ignores the requested size) is scaled down to fit.
  float scale = 1.0f;
  if (bitmap.getWidth() > 0 && bitmap.getHeight() > 0) {
    const float fitScale = std::min(static_cast<float>(pageWidth) / static_cast<float>(bitmap.getWidth()),
                                    static_cast<float>(pageHeight) / static_cast<float>(bitmap.getHeight()));
    if (fitScale < 1.0f) scale = fitScale;
  }
  const int drawnWidth = static_cast<int>(static_cast<float>(bitmap.getWidth()) * scale);
  const int drawnHeight = static_cast<int>(static_cast<float>(bitmap.getHeight()) * scale);
  const int x = std::max((pageWidth - drawnWidth) / 2, 0);
  const int y = std::max((pageHeight - drawnHeight) / 2, 0);

  const uint8_t renderMode = TRMNL_STORE.getRenderMode();
  // The gray levels need a multi-bit source, and the inverted mode stays black
  // and white for the same reason the sleep screen's inverted filter does:
  // neither waveform has an inverted flavour to drive the planes with.
  const bool grayscale = TRMNL_STORE.isGrayscaleRenderMode() && bitmap.hasGreyscale();

  if (grayscale && TRMNL_STORE.isFactoryRenderMode()) {
    // The OEM waveform drives all four levels from scratch, so it needs no BW
    // base at all — the planes carry the whole image.
    drawFactoryGrayscale(bitmap, x, y, pageWidth, pageHeight);
    return true;
  }

  // Pass 1: the black-and-white frame. The single-pass modes finish here, and
  // so does a 1-bit dashboard in any mode. For the two differential modes it
  // doubles as the base their planes are nudged off.
  renderer.clearScreen();
  renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, 0, 0);
  if (renderMode == TrmnlStore::RENDER_MODE_BW_INVERTED) {
    // Before the dots, so they keep their white backing and black fill
    renderer.invertScreen();
  }
  drawCountdownDots();

  if (!grayscale) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    return true;
  }

  // Must stay HALF: the differential gray LUT is calibrated against the pixel
  // state the single-pass HALF waveform leaves behind (see
  // SleepActivity::renderBitmapSleepScreen for the full reasoning).
  renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);

  // Passes 2 and 3 mark the pixels to nudge. GRAYSCALE_LSB marks dark gray only
  // and GRAYSCALE_MSB marks dark + light, so one mark set is a subset of the
  // other and the pair can only ever select two of the LUT's three gray groups.
  // ALT feeds the same two passes to the opposite planes, which selects the
  // other pair — including the group the LUT calls "light gray".
  const bool swapPlanes = renderMode == TrmnlStore::RENDER_MODE_GRAYSCALE_ALT;
  bool grayOk = drawGrayPlane(bitmap, x, y, pageWidth, pageHeight, GfxRenderer::GRAYSCALE_LSB);
  if (grayOk) {
    clearDotsFromGrayPlane();
    if (swapPlanes) {
      renderer.copyGrayscaleMsbBuffers();
    } else {
      renderer.copyGrayscaleLsbBuffers();
    }
    grayOk = drawGrayPlane(bitmap, x, y, pageWidth, pageHeight, GfxRenderer::GRAYSCALE_MSB);
  }
  if (grayOk) {
    clearDotsFromGrayPlane();
    if (swapPlanes) {
      renderer.copyGrayscaleLsbBuffers();
    } else {
      renderer.copyGrayscaleMsbBuffers();
    }
    renderer.displayGrayBuffer();
  } else {
    LOG_ERR("TRM", "Rewind failed, showing dashboard without grayscale");
  }
  renderer.setRenderMode(GfxRenderer::BW);

  // Pass 4: rebuild the BW frame the gray passes overwrote, so the per-tick
  // dot updates keep differential-refreshing against the image instead of
  // against a leftover gray plane. Cheaper than storeBwBuffer()/restore for
  // the same reason as above (this is the XtcReaderActivity pattern).
  restoreBwFrame(bitmap, x, y, pageWidth, pageHeight);
  renderer.cleanupGrayscaleWithFrameBuffer();
  return true;
}

bool TrmnlDashboardActivity::drawGrayPlane(const Bitmap& bitmap, const int x, const int y, const int pageWidth,
                                           const int pageHeight, const GfxRenderer::RenderMode mode) const {
  // Every pass re-reads the BMP rather than caching a decode: SD time is the
  // cheap resource here, while storeBwBuffer() would cost a 48KB peak on a heap
  // the preceding TLS session has already fragmented.
  if (bitmap.rewindToData() != BmpReaderError::Ok) return false;
  renderer.clearScreen(0x00);
  renderer.setRenderMode(mode);
  renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, 0, 0);
  return true;
}

void TrmnlDashboardActivity::restoreBwFrame(const Bitmap& bitmap, const int x, const int y, const int pageWidth,
                                            const int pageHeight) {
  if (bitmap.rewindToData() == BmpReaderError::Ok) {
    renderer.clearScreen();
    renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, 0, 0);
    drawCountdownDots();
    return;
  }
  // The framebuffer still holds a gray plane, and the caller is about to hand
  // it to cleanupGrayscaleWithFrameBuffer() as the differential baseline. Give
  // it a defined frame instead of plane bits, and repaint from scratch on the
  // next tick rather than diffing against this blank one.
  LOG_ERR("TRM", "BW restore failed, forcing a full redraw next tick");
  renderer.clearScreen();
  drawCountdownDots();
  bwFrameStale = true;
}

void TrmnlDashboardActivity::drawFactoryGrayscale(const Bitmap& bitmap, const int x, const int y, const int pageWidth,
                                                  const int pageHeight) {
  // Absolute planes: the bit pair *is* the level, so the dots need no punch-out
  // pass — white dot pixels set both bits, black ones clear both, which is the
  // very same bit pattern drawCountdownDots() paints in BW.
  // The panel however reads that pair inverted (measured: level bits straight
  // through come out as a negative), so complement both planes. invertScreen()
  // is a plain framebuffer bit flip, i.e. exactly the complement, and it takes
  // the dots along with the image. _OEM_INV skips it and keeps the negative.
  const bool complement = TRMNL_STORE.getRenderMode() == TrmnlStore::RENDER_MODE_GRAYSCALE_OEM;

  bool ok = drawGrayPlane(bitmap, x, y, pageWidth, pageHeight, GfxRenderer::GRAYSCALE_ABS_LSB);
  if (ok) {
    drawCountdownDots();
    if (complement) renderer.invertScreen();
    renderer.copyGrayscaleLsbBuffers();
    ok = drawGrayPlane(bitmap, x, y, pageWidth, pageHeight, GfxRenderer::GRAYSCALE_ABS_MSB);
  }
  if (ok) {
    drawCountdownDots();
    if (complement) renderer.invertScreen();
    renderer.copyGrayscaleMsbBuffers();
  }
  renderer.setRenderMode(GfxRenderer::BW);

  if (!ok) {
    LOG_ERR("TRM", "Rewind failed, falling back to a plain BW dashboard");
    restoreBwFrame(bitmap, x, y, pageWidth, pageHeight);
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    return;
  }

  renderer.displayGrayBuffer(true);

  // The factory waveform self-cleans, but RED RAM is still holding the MSB
  // plane; the dot ticks need it back to the displayed BW frame.
  restoreBwFrame(bitmap, x, y, pageWidth, pageHeight);
  renderer.cleanupGrayscaleWithFrameBuffer();
}

void TrmnlDashboardActivity::render(RenderLock&&) {
  // TRMNL dashboards render in the orientation configured in TRMNL settings
  // (default: the panel's native landscape, which is what dashboards are
  // authored for), independent of the reader orientation.
  renderer.setOrientation(static_cast<GfxRenderer::Orientation>(TRMNL_STORE.getOrientation()));

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  if (state == FETCHING && showFetchSplash) {
    // Full-screen status while the network round-trip runs. Automatic
    // refreshes skip this and keep the current image on screen.
    renderer.clearScreen();
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_TRMNL_FETCHING));
    // HALF, not the differential default: a button-triggered fetch replaces a
    // full-screen dashboard, which a fast refresh would leave ghosting behind
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    showFetchSplash = false;
  } else if (fullRedraw) {
    if (!drawCachedImage(pageWidth, pageHeight)) {
      // No cached image and the fetch failed; the countdown still runs, so
      // this retries automatically
      renderer.clearScreen();
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_TRMNL_FETCH_FAILED), true, EpdFontFamily::BOLD);
      drawCountdownDots();
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    }
    // Normally the frame just painted is the baseline the dot ticks diff
    // against; if rebuilding it failed, repaint everything next tick instead.
    fullRedraw = bwFrameStale;
    bwFrameStale = false;
    renderedDots = dotsRemaining;
  } else if (renderedDots != dotsRemaining) {
    // Dot tick: only the dot pixels change, so the fast differential
    // refresh updates them without disturbing the rest of the panel
    drawCountdownDots();
    renderer.displayBuffer();
    renderedDots = dotsRemaining;
  }

  // Parent activities render in portrait; don't leak the dashboard orientation
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
}
