#pragma once

#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "util/ButtonNavigator.h"

/**
 * Submenu for TRMNL settings.
 * Shows server URL, device MAC, link (auto-provision), fetch, and the
 * dashboard mode refresh interval.
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
  OptionPopup optionPopup;

  size_t selectedIndex = 0;

  void handleSelection();
};
