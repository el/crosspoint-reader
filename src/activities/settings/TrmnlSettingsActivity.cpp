#include "TrmnlSettingsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "TrmnlActionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "trmnl/TrmnlClient.h"
#include "trmnl/TrmnlStore.h"

namespace fui = freeink::ui;

namespace {
const StrId menuNames[TrmnlSettingsActivity::MENU_ITEMS] = {
    StrId::STR_TRMNL_SERVER_URL, StrId::STR_TRMNL_MAC_ID,           StrId::STR_TRMNL_LINK_DEVICE,
    StrId::STR_TRMNL_FETCH_NOW,  StrId::STR_TRMNL_REFRESH_INTERVAL, StrId::STR_TRMNL_ORIENTATION,
    StrId::STR_TRMNL_RENDER_MODE};

// Dashboard mode countdown length; kept in sync with TrmnlStore::REFRESH_INTERVAL_OPTIONS
const StrId refreshIntervalLabels[TrmnlStore::REFRESH_INTERVAL_OPTIONS_COUNT] = {
    StrId::STR_TRMNL_INTERVAL_3_MIN, StrId::STR_TRMNL_INTERVAL_5_MIN, StrId::STR_TRMNL_INTERVAL_10_MIN,
    StrId::STR_TRMNL_INTERVAL_15_MIN, StrId::STR_TRMNL_INTERVAL_30_MIN};

// Indexed by the stored orientation, i.e. GfxRenderer::Orientation order
const StrId orientationLabels[TrmnlStore::ORIENTATION_OPTIONS_COUNT] = {
    StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_ORIENTATION_INVERTED, StrId::STR_LANDSCAPE_CCW};

// The stored render mode values are append-only, so the popup needs its own
// order to keep the grayscale variants together.
constexpr uint8_t renderModeOrder[TrmnlStore::RENDER_MODE_OPTIONS_COUNT] = {TrmnlStore::RENDER_MODE_GRAYSCALE,
                                                                            TrmnlStore::RENDER_MODE_GRAYSCALE_ALT,
                                                                            TrmnlStore::RENDER_MODE_GRAYSCALE_OEM,
                                                                            TrmnlStore::RENDER_MODE_GRAYSCALE_OEM_INV,
                                                                            TrmnlStore::RENDER_MODE_BW,
                                                                            TrmnlStore::RENDER_MODE_BW_INVERTED};
const StrId renderModeLabels[TrmnlStore::RENDER_MODE_OPTIONS_COUNT] = {
    StrId::STR_GRAYSCALE,         StrId::STR_GRAYSCALE_ALT,   StrId::STR_GRAYSCALE_OEM,
    StrId::STR_GRAYSCALE_OEM_INV, StrId::STR_BLACK_AND_WHITE, StrId::STR_INVERTED};

int renderModeIndex() {
  const uint8_t mode = TRMNL_STORE.getRenderMode();
  for (uint8_t i = 0; i < TrmnlStore::RENDER_MODE_OPTIONS_COUNT; i++) {
    if (renderModeOrder[i] == mode) return i;
  }
  return 0;
}

int refreshIntervalIndex() {
  const uint8_t minutes = TRMNL_STORE.getRefreshIntervalMinutes();
  for (uint8_t i = 0; i < TrmnlStore::REFRESH_INTERVAL_OPTIONS_COUNT; i++) {
    if (TrmnlStore::REFRESH_INTERVAL_OPTIONS[i] == minutes) return i;
  }
  return 1;  // REFRESH_INTERVAL_OPTIONS[1] == 5, the default
}
}  // namespace

TrmnlSettingsActivity::TrmnlSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiListActivity("TrmnlSettings", renderer, mappedInput) {
  // Labels never change (unlike the values, which track live TRMNL_STORE
  // state), so they're set once here rather than every buildScreen() call.
  for (int i = 0; i < MENU_ITEMS; i++) {
    rowItems_[i].label = I18N.get(menuNames[i]);
    rowItems_[i].actionValue = static_cast<int16_t>(i);
  }
}

const char* TrmnlSettingsActivity::headerTitle() const { return tr(STR_TRMNL); }

bool TrmnlSettingsActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

void TrmnlSettingsActivity::activateIndex(const int index) {
  if (optionPopup.isActive()) return;
  nav.selected = index;
  // Activation opens a popup/sub-activity or repaints a new value; a lingering
  // flash would gray an unrelated row.
  app.clearTapFlash();
  handleSelection();
  requestUpdate();
}

void TrmnlSettingsActivity::handleSelection() {
  if (nav.selected == 0) {
    // Server URL - prefill with https:// if empty to save typing
    const std::string currentUrl = TRMNL_STORE.getServerUrl();
    const std::string prefillUrl = currentUrl.empty() ? "https://" : currentUrl;
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_TRMNL_SERVER_URL),
                                                                   prefillUrl, 128, InputType::Url),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& kb = std::get<KeyboardResult>(result.data);
                               const std::string urlToSave =
                                   (kb.text == "https://" || kb.text == "http://") ? "" : kb.text;
                               TRMNL_STORE.setServerUrl(urlToSave);
                               TRMNL_STORE.saveToFile();
                             }
                           });
  } else if (nav.selected == 1) {
    // Custom MAC ID override; empty keeps the hardware MAC. The list row
    // always shows the effective MAC (which is what gets registered at TRMNL)
    startActivityForResult(std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_TRMNL_MAC_ID),
                                                                   TRMNL_STORE.getCustomMacId(), 17, InputType::Text),
                           [this](const ActivityResult& result) {
                             if (!result.isCancelled) {
                               const auto& kb = std::get<KeyboardResult>(result.data);
                               if (TRMNL_STORE.setCustomMacId(kb.text)) {
                                 TRMNL_STORE.saveToFile();
                               }
                             }
                           });
  } else if (nav.selected == 2) {
    // Link device (auto-provision via /api/setup)
    startActivityForResult(
        std::make_unique<TrmnlActionActivity>(renderer, mappedInput, TrmnlActionActivity::Mode::LINK),
        [this](const ActivityResult&) { requestUpdate(); });
  } else if (nav.selected == 3) {
    // Fetch image now
    if (!TRMNL_STORE.isLinked()) {
      // Can't fetch without an API key
      return;
    }
    startActivityForResult(
        std::make_unique<TrmnlActionActivity>(renderer, mappedInput, TrmnlActionActivity::Mode::FETCH),
        [this](const ActivityResult&) { requestUpdate(); });
  } else if (nav.selected == 4) {
    // Dashboard mode refresh interval
    optionPopup.show(StrId::STR_TRMNL_REFRESH_INTERVAL, refreshIntervalLabels,
                     TrmnlStore::REFRESH_INTERVAL_OPTIONS_COUNT, refreshIntervalIndex(), [](int idx) {
                       TRMNL_STORE.setRefreshIntervalMinutes(TrmnlStore::REFRESH_INTERVAL_OPTIONS[idx]);
                       TRMNL_STORE.saveToFile();
                     });
  } else if (nav.selected == 5) {
    // Dashboard/sleep-screen orientation. Also decides the image size asked of
    // the TRMNL server, so the next fetch picks the new aspect ratio up.
    optionPopup.show(StrId::STR_TRMNL_ORIENTATION, orientationLabels, TrmnlStore::ORIENTATION_OPTIONS_COUNT,
                     TRMNL_STORE.getOrientation(), [](int idx) {
                       TRMNL_STORE.setOrientation(static_cast<uint8_t>(idx));
                       TRMNL_STORE.saveToFile();
                     });
  } else if (nav.selected == 6) {
    // How the dashboard paints the cached image. Scoped to dashboard mode: the
    // TRMNL sleep screen goes through the shared cover renderer, which already
    // has its own filter setting (SETTINGS.sleepScreenCoverFilter).
    optionPopup.show(StrId::STR_TRMNL_RENDER_MODE, renderModeLabels, TrmnlStore::RENDER_MODE_OPTIONS_COUNT,
                     renderModeIndex(), [](int idx) {
                       TRMNL_STORE.setRenderMode(renderModeOrder[idx]);
                       TRMNL_STORE.saveToFile();
                     });
  }
}

void TrmnlSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // Content below the GUI.drawHeader band, above the button hints.
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0,
                                      static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // rowItems_'s labels/actionValue were set once in the constructor; only the
  // live value text needs refreshing here, by assigning into the existing
  // rowValues_ strings (no array growth) rather than building a new
  // items/values vector on every render.
  for (int i = 0; i < MENU_ITEMS; i++) {
    if (i == 0) {
      const auto serverUrl = TRMNL_STORE.getServerUrl();
      rowValues_[i] = serverUrl.empty() ? tr(STR_DEFAULT_VALUE) : serverUrl;
    } else if (i == 1) {
      rowValues_[i] = TrmnlClient::macAddress();
    } else if (i == 2) {
      if (!TRMNL_STORE.isLinked()) {
        rowValues_[i] = tr(STR_TRMNL_NOT_LINKED);
      } else {
        const auto friendlyId = TRMNL_STORE.getFriendlyId();
        rowValues_[i] = friendlyId.empty() ? tr(STR_TRMNL_LINKED) : friendlyId;
      }
    } else if (i == 3) {
      rowValues_[i] = TRMNL_STORE.isLinked() ? "" : std::string("[") + tr(STR_SET_CREDENTIALS_FIRST) + "]";
    } else if (i == 4) {
      rowValues_[i] = I18N.get(refreshIntervalLabels[refreshIntervalIndex()]);
    } else if (i == 5) {
      rowValues_[i] = I18N.get(orientationLabels[TRMNL_STORE.getOrientation()]);
    } else {
      rowValues_[i] = I18N.get(renderModeLabels[renderModeIndex()]);
    }
    rowItems_[i].value = rowValues_[i].empty() ? nullptr : rowValues_[i].c_str();
  }

  fui::ListProps props;
  props.items = rowItems_;
  props.count = static_cast<uint16_t>(MENU_ITEMS);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;               // air between the value and the row edge
  // Label at the value's font size: both sides of the row read as one unit.
  // maxLines=2 also marks the style caller-owned (see textStyleUnset).
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

void TrmnlSettingsActivity::render(RenderLock&& lock) {
  // The popup owns the screen while it is up; the list underneath keeps its
  // last painted frame.
  if (optionPopup.processRender(renderer, mappedInput)) return;
  UiListActivity::render(std::move(lock));
}
