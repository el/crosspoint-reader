#pragma once

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

/**
 * Submenu for TRMNL settings.
 * Shows server URL, device MAC, link (auto-provision) and fetch options.
 */
class TrmnlSettingsActivity final : public Activity {
 public:
  explicit TrmnlSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("TrmnlSettings", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;

  size_t selectedIndex = 0;

  void handleSelection();
};
