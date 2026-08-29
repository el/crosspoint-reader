#pragma once

#include <string>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

/**
 * Submenu for TRMNL settings.
 * Shows server URL, device MAC, link (auto-provision), fetch, and the
 * dashboard mode refresh interval.
 */
class TrmnlSettingsActivity final : public UiListActivity {
 public:
  explicit TrmnlSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  static constexpr int MENU_ITEMS = 7;

  void render(RenderLock&&) override;

 private:
  OptionPopup optionPopup;

  int listCount() const override { return MENU_ITEMS; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;
  const char* headerTitle() const override;

  void handleSelection();

  // Row storage: MENU_ITEMS is a compile-time constant, so fixed-capacity
  // storage avoids any heap allocation for the row list. Labels are set once
  // in the constructor; buildScreen() only refreshes the live value text
  // (rowValues_) by assigning into the existing strings (no array growth).
  std::string rowValues_[MENU_ITEMS];
  freeink::ui::ListItem rowItems_[MENU_ITEMS]{};
};
