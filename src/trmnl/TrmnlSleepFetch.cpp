#include "TrmnlSleepFetch.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "TrmnlClient.h"
#include "TrmnlStore.h"
#include "activities/RenderLock.h"
#include "components/UITheme.h"
#include "network/WifiAutoConnect.h"

namespace {
constexpr uint32_t WIFI_TIMEOUT_MS = 10000;

// The fetch itself runs on the loop task: wolfSSL handshakes reliably fail
// when run from a spawned task (root cause not yet identified), while the
// same code on the loop task works. Only the input polling and progress bar
// live in a helper task.
//
// Shared state: plain bools/bytes are sufficient. Each field has a single
// writer, the ESP32-C3 is single-core, and every cross-task read goes through
// a pointer across opaque function calls, so the compiler cannot cache them.
struct UiPollState {
  GfxRenderer* renderer = nullptr;
  MappedInputManager* input = nullptr;
  Rect popupRect{};

  bool cancel = false;   // written by UI task on Cancel/Skip, read by the fetch
  bool skip = false;     // written by UI task: true = Skip, false = Cancel
  uint8_t progress = 0;  // written by the fetch, drawn by the UI task
  bool stop = false;     // written by the loop task when the fetch ends
  bool stopped = false;  // written by the UI task just before it deletes itself
};

void uiPollTask(void* param) {
  auto* ui = static_cast<UiPollState*>(param);
  uint8_t shownProgress = 0;
  unsigned long lastBarDraw = 0;
  while (!ui->stop) {
    ui->input->update();
    if (!ui->cancel) {  // accept input only until a choice is made
      if (ui->input->wasPressed(MappedInputManager::Button::Back)) {
        ui->skip = false;
        ui->cancel = true;
      } else if (ui->input->wasPressed(MappedInputManager::Button::Confirm)) {
        ui->skip = true;
        ui->cancel = true;
      }
    }
    // Throttle e-ink partial refreshes: >=5% steps, at most one every 500ms
    if (ui->progress >= shownProgress + 5 && millis() - lastBarDraw >= 500) {
      shownProgress = ui->progress;
      lastBarDraw = millis();
      GUI.fillPopupProgress(*ui->renderer, ui->popupRect, shownProgress);
    }
    delay(50);
  }
  // No shared-state access after stopped is set: run() may return and destroy it
  ui->stopped = true;
  vTaskDelete(nullptr);
}
}  // namespace

namespace TrmnlSleepFetch {

bool shouldRun() {
  return SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::TRMNL && TRMNL_STORE.isLinked();
}

Result run(GfxRenderer& renderer, MappedInputManager& mappedInput) {
  // Hold the render lock for the whole flow so a pending activity render
  // can't repaint over the popup while the fetch is running
  RenderLock lock;

  const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_TRMNL_SKIP), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  // drawPopup displays the full buffer, so the hints land in the same refresh
  const Rect popupRect = GUI.drawPopup(renderer, tr(STR_TRMNL_FETCHING_SCREEN));
  GUI.fillPopupProgress(renderer, popupRect, 0);

  UiPollState ui;
  ui.renderer = &renderer;
  ui.input = &mappedInput;
  ui.popupRect = popupRect;

  // Buttons + progress bar only; the network work stays on this task
  const bool uiTaskRunning = xTaskCreate(&uiPollTask, "TrmnlFetchUi", 4096, &ui, 1, nullptr) == pdPASS;
  if (!uiTaskRunning) {
    LOG_ERR("TRM", "Failed to start UI poll task; fetching without cancel/skip");
  }

  // TLS needs ~40KB of mostly contiguous heap on top of the WiFi driver's
  // ~45KB; the caller tears the outgoing activity down before calling run()
  // for exactly this reason. Log the budget so OOM-shaped failures (wolfSSL
  // MEMORY_E = -125) stay diagnosable from serial output.
  LOG_DBG("TRM", "Fetch heap: %u free, %u largest block", static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));

  ui.progress = 5;
  if (WifiAutoConnect::tryConnectLastNetwork(WIFI_TIMEOUT_MS, &ui.cancel) && !ui.cancel) {
    LOG_DBG("TRM", "Post-connect heap: %u free, %u largest block", static_cast<unsigned>(ESP.getFreeHeap()),
            static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
    ui.progress = 40;
    const auto fetchResult =
        TrmnlClient::fetchImageToCache(&ui.cancel, [&ui](const uint8_t percent) { ui.progress = percent; });
    if (fetchResult != TrmnlClient::OK && fetchResult != TrmnlClient::ABORTED) {
      LOG_ERR("TRM", "Sleep fetch failed (%d), cached image will be used", static_cast<int>(fetchResult));
    }
  }

  if (uiTaskRunning) {
    ui.stop = true;
    while (!ui.stopped) {
      delay(10);
    }
  }

  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }

  Result result = Result::FETCHED;
  if (ui.cancel) {
    result = ui.skip ? Result::SKIPPED : Result::CANCELLED;
  }
  LOG_DBG("TRM", "Sleep fetch result: %d", static_cast<int>(result));
  return result;
}

}  // namespace TrmnlSleepFetch
