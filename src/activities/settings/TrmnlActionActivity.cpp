#include "TrmnlActionActivity.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "trmnl/TrmnlClient.h"
#include "trmnl/TrmnlStore.h"

void TrmnlActionActivity::onEnter() {
  Activity::onEnter();

  statusMessage = (mode == Mode::LINK) ? tr(STR_TRMNL_LINKING) : tr(STR_TRMNL_FETCHING);

  // Check if already connected
  if (WiFi.status() == WL_CONNECTED) {
    onWifiSelectionComplete(true);
    return;
  }

  // Launch WiFi selection
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void TrmnlActionActivity::onExit() {
  Activity::onExit();

  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void TrmnlActionActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    {
      RenderLock lock(*this);
      state = FAILED;
      errorMessage = tr(STR_WIFI_CONN_FAILED);
    }
    requestUpdate();
    return;
  }

  {
    RenderLock lock(*this);
    state = WORKING;
  }
  requestUpdate();

  performAction();
}

void TrmnlActionActivity::performAction() {
  const TrmnlClient::Result result =
      (mode == Mode::LINK) ? TrmnlClient::linkDevice() : TrmnlClient::fetchImageToCache();

  {
    RenderLock lock(*this);
    if (result == TrmnlClient::OK) {
      state = SUCCESS;
    } else {
      state = FAILED;
      errorMessage = TrmnlClient::errorString(result);
    }
  }
  requestUpdate();
}

void TrmnlActionActivity::loop() {
  if (state == SUCCESS || state == FAILED) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      finish();
    }
  }
}

void TrmnlActionActivity::renderImagePreview() {
  // TRMNL dashboards are landscape (800x480, exactly the panel size): preview
  // 1:1 in native landscape, the same way the sleep screen will render it
  renderer.setOrientation(GfxRenderer::Orientation::LandscapeCounterClockwise);
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

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
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_TRMNL_FETCH_FAILED), true, EpdFontFamily::BOLD);
  }

  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  // Parent activities render in portrait; don't leak the preview orientation
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
}

void TrmnlActionActivity::render(RenderLock&&) {
  if (state == SUCCESS && mode == Mode::FETCH) {
    renderImagePreview();
    return;
  }

  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_TRMNL));
  const auto height = renderer.getLineHeight(UI_10_FONT_ID);
  const auto top = (pageHeight - height) / 2;

  // The MAC is what the user registers in their TRMNL account, so show it on
  // every link screen
  char macLine[64];
  snprintf(macLine, sizeof(macLine), "%s %s", tr(STR_MAC_ADDRESS), TrmnlClient::macAddress().c_str());

  if (state == WORKING) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, statusMessage.c_str());
  } else if (state == SUCCESS) {
    renderer.drawCenteredText(UI_10_FONT_ID, top, tr(STR_TRMNL_LINK_SUCCESS), true, EpdFontFamily::BOLD);
    if (!TRMNL_STORE.getFriendlyId().empty()) {
      renderer.drawCenteredText(UI_10_FONT_ID, top + height + 10, TRMNL_STORE.getFriendlyId().c_str());
    }
  } else if (state == FAILED) {
    renderer.drawCenteredText(UI_10_FONT_ID, top,
                              (mode == Mode::LINK) ? tr(STR_TRMNL_LINK_FAILED) : tr(STR_TRMNL_FETCH_FAILED), true,
                              EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, top + height + 10, errorMessage.c_str());
    if (mode == Mode::LINK) {
      renderer.drawCenteredText(UI_10_FONT_ID, top + (height + 10) * 2, macLine);
    }
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
