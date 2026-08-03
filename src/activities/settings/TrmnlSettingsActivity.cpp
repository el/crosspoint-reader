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

namespace {
constexpr int MENU_ITEMS = 7;
const StrId menuNames[MENU_ITEMS] = {StrId::STR_TRMNL_SERVER_URL,       StrId::STR_TRMNL_MAC_ID,
                                     StrId::STR_TRMNL_LINK_DEVICE,      StrId::STR_TRMNL_FETCH_NOW,
                                     StrId::STR_TRMNL_REFRESH_INTERVAL, StrId::STR_TRMNL_ORIENTATION,
                                     StrId::STR_TRMNL_RENDER_MODE};

// Dashboard mode countdown length; kept in sync with TrmnlStore::REFRESH_INTERVAL_OPTIONS
const StrId refreshIntervalLabels[TrmnlStore::REFRESH_INTERVAL_OPTIONS_COUNT] = {
    StrId::STR_TRMNL_INTERVAL_3_MIN, StrId::STR_TRMNL_INTERVAL_5_MIN, StrId::STR_TRMNL_INTERVAL_10_MIN,
    StrId::STR_TRMNL_INTERVAL_15_MIN, StrId::STR_TRMNL_INTERVAL_30_MIN};

// Indexed by the stored orientation, i.e. GfxRenderer::Orientation order
const StrId orientationLabels[TrmnlStore::ORIENTATION_OPTIONS_COUNT] = {
    StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_ORIENTATION_INVERTED, StrId::STR_LANDSCAPE_CCW};

// Indexed by TrmnlStore::RENDER_MODE_*
const StrId renderModeLabels[TrmnlStore::RENDER_MODE_OPTIONS_COUNT] = {StrId::STR_GRAYSCALE, StrId::STR_BLACK_AND_WHITE,
                                                                       StrId::STR_INVERTED};

int refreshIntervalIndex() {
  const uint8_t minutes = TRMNL_STORE.getRefreshIntervalMinutes();
  for (uint8_t i = 0; i < TrmnlStore::REFRESH_INTERVAL_OPTIONS_COUNT; i++) {
    if (TrmnlStore::REFRESH_INTERVAL_OPTIONS[i] == minutes) return i;
  }
  return 1;  // REFRESH_INTERVAL_OPTIONS[1] == 5, the default
}
}  // namespace

void TrmnlSettingsActivity::onEnter() {
  Activity::onEnter();

  selectedIndex = 0;
  requestUpdate();
}

void TrmnlSettingsActivity::onExit() { Activity::onExit(); }

void TrmnlSettingsActivity::loop() {
  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleSelection();
    return;
  }

  // Handle navigation
  buttonNavigator.onNext([this] {
    selectedIndex = (selectedIndex + 1) % MENU_ITEMS;
    requestUpdate();
  });

  buttonNavigator.onPrevious([this] {
    selectedIndex = (selectedIndex + MENU_ITEMS - 1) % MENU_ITEMS;
    requestUpdate();
  });
}

void TrmnlSettingsActivity::handleSelection() {
  if (selectedIndex == 0) {
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
  } else if (selectedIndex == 1) {
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
  } else if (selectedIndex == 2) {
    // Link device (auto-provision via /api/setup)
    startActivityForResult(
        std::make_unique<TrmnlActionActivity>(renderer, mappedInput, TrmnlActionActivity::Mode::LINK),
        [this](const ActivityResult&) { requestUpdate(); });
  } else if (selectedIndex == 3) {
    // Fetch image now
    if (!TRMNL_STORE.isLinked()) {
      // Can't fetch without an API key
      return;
    }
    startActivityForResult(
        std::make_unique<TrmnlActionActivity>(renderer, mappedInput, TrmnlActionActivity::Mode::FETCH),
        [this](const ActivityResult&) { requestUpdate(); });
  } else if (selectedIndex == 4) {
    // Dashboard mode refresh interval
    optionPopup.show(StrId::STR_TRMNL_REFRESH_INTERVAL, refreshIntervalLabels,
                     TrmnlStore::REFRESH_INTERVAL_OPTIONS_COUNT, refreshIntervalIndex(), [](int idx) {
                       TRMNL_STORE.setRefreshIntervalMinutes(TrmnlStore::REFRESH_INTERVAL_OPTIONS[idx]);
                       TRMNL_STORE.saveToFile();
                     });
    requestUpdate();
  } else if (selectedIndex == 5) {
    // Dashboard/sleep-screen orientation. Also decides the image size asked of
    // the TRMNL server, so the next fetch picks the new aspect ratio up.
    optionPopup.show(StrId::STR_TRMNL_ORIENTATION, orientationLabels, TrmnlStore::ORIENTATION_OPTIONS_COUNT,
                     TRMNL_STORE.getOrientation(), [](int idx) {
                       TRMNL_STORE.setOrientation(static_cast<uint8_t>(idx));
                       TRMNL_STORE.saveToFile();
                     });
    requestUpdate();
  } else if (selectedIndex == 6) {
    // How the dashboard paints the cached image. Scoped to dashboard mode: the
    // TRMNL sleep screen goes through the shared cover renderer, which already
    // has its own filter setting (SETTINGS.sleepScreenCoverFilter).
    optionPopup.show(StrId::STR_TRMNL_RENDER_MODE, renderModeLabels, TrmnlStore::RENDER_MODE_OPTIONS_COUNT,
                     TRMNL_STORE.getRenderMode(), [](int idx) {
                       TRMNL_STORE.setRenderMode(static_cast<uint8_t>(idx));
                       TRMNL_STORE.saveToFile();
                     });
    requestUpdate();
  }
}

void TrmnlSettingsActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;

  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_TRMNL));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  GUI.drawList(
      renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(MENU_ITEMS),
      static_cast<int>(selectedIndex), [](int index) { return std::string(I18N.get(menuNames[index])); }, nullptr,
      nullptr,
      [](int index) {
        // Draw status for each setting
        if (index == 0) {
          auto serverUrl = TRMNL_STORE.getServerUrl();
          return serverUrl.empty() ? std::string(tr(STR_DEFAULT_VALUE)) : serverUrl;
        } else if (index == 1) {
          return TrmnlClient::macAddress();
        } else if (index == 2) {
          if (!TRMNL_STORE.isLinked()) {
            return std::string(tr(STR_TRMNL_NOT_LINKED));
          }
          return TRMNL_STORE.getFriendlyId().empty() ? std::string(tr(STR_TRMNL_LINKED)) : TRMNL_STORE.getFriendlyId();
        } else if (index == 3) {
          return TRMNL_STORE.isLinked() ? std::string("") : std::string("[") + tr(STR_SET_CREDENTIALS_FIRST) + "]";
        } else if (index == 4) {
          return std::string(I18N.get(refreshIntervalLabels[refreshIntervalIndex()]));
        } else if (index == 5) {
          return std::string(I18N.get(orientationLabels[TRMNL_STORE.getOrientation()]));
        } else if (index == 6) {
          return std::string(I18N.get(renderModeLabels[TRMNL_STORE.getRenderMode()]));
        }
        return std::string(tr(STR_NOT_SET));
      },
      true);

  // Draw help text at bottom
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
