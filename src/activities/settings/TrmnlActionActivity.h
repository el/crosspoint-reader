#pragma once
#include <string>

#include "activities/Activity.h"

/**
 * Runs a TRMNL network flow (link device or fetch image) with a status screen:
 * connects WiFi via WifiSelectionActivity when needed, performs the API call,
 * and shows the result. A successful fetch previews the dashboard image.
 */
class TrmnlActionActivity final : public Activity {
 public:
  enum class Mode { LINK, FETCH };

  TrmnlActionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Mode mode)
      : Activity("TrmnlAction", renderer, mappedInput), mode(mode) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum State { WORKING, SUCCESS, FAILED };

  void onWifiSelectionComplete(bool success);
  void performAction();
  void renderImagePreview();

  Mode mode;
  State state = WORKING;
  std::string statusMessage;
  std::string errorMessage;
};
