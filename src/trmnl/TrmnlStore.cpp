#include "TrmnlStore.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <ObfuscationUtils.h>

#include <cctype>

// Initialize the static instance
TrmnlStore TrmnlStore::instance;

namespace {
constexpr char TRMNL_FILE_JSON[] = "/.crosspoint/trmnl.json";
constexpr char TRMNL_IMAGE_BMP[] = "/.crosspoint/trmnl.bmp";

// Official TRMNL server (same default as the stock TRMNL firmware)
constexpr char DEFAULT_SERVER_URL[] = "https://trmnl.app";
}  // namespace

const char* TrmnlStore::cachedImagePath() { return TRMNL_IMAGE_BMP; }

bool TrmnlStore::saveToFile() const {
  Storage.mkdir("/.crosspoint");

  JsonDocument doc;
  doc["serverUrl"] = serverUrl;
  doc["apiKey_obf"] = obfuscation::obfuscateToBase64(apiKey);
  doc["friendlyId"] = friendlyId;
  doc["macId"] = customMacId;
  doc["refreshIntervalMinutes"] = refreshIntervalMinutes;
  doc["orientation"] = orientation;
  doc["renderMode"] = renderMode;

  String json;
  serializeJson(doc, json);
  return Storage.writeFile(TRMNL_FILE_JSON, json);
}

bool TrmnlStore::loadFromFile() {
  if (!Storage.exists(TRMNL_FILE_JSON)) {
    LOG_DBG("TRM", "No credentials file found");
    return false;
  }

  const String json = Storage.readFile(TRMNL_FILE_JSON);
  if (json.isEmpty()) {
    return false;
  }

  JsonDocument doc;
  const auto error = deserializeJson(doc, json.c_str());
  if (error) {
    LOG_ERR("TRM", "JSON parse error: %s", error.c_str());
    return false;
  }

  serverUrl = doc["serverUrl"] | std::string("");
  friendlyId = doc["friendlyId"] | std::string("");
  customMacId = doc["macId"] | std::string("");
  setRefreshIntervalMinutes(doc["refreshIntervalMinutes"] | refreshIntervalMinutes);
  setOrientation(doc["orientation"] | orientation);
  setRenderMode(doc["renderMode"] | renderMode);

  bool ok = false;
  apiKey = obfuscation::deobfuscateFromBase64(doc["apiKey_obf"] | "", &ok);
  if (!ok) {
    apiKey.clear();
  }

  LOG_DBG("TRM", "Loaded TRMNL credentials (linked: %d)", isLinked() ? 1 : 0);
  return true;
}

void TrmnlStore::setApiKey(const std::string& key) {
  apiKey = key;
  LOG_DBG("TRM", "Set API key (%u chars)", static_cast<unsigned>(key.size()));
}

void TrmnlStore::clearApiKey() {
  apiKey.clear();
  friendlyId.clear();
  saveToFile();
  LOG_DBG("TRM", "Cleared TRMNL credentials");
}

void TrmnlStore::setFriendlyId(const std::string& id) { friendlyId = id; }

bool TrmnlStore::setCustomMacId(const std::string& mac) {
  // Strip separators and normalize case so AA:BB / aa-bb / bare hex all work
  std::string hex;
  hex.reserve(12);
  for (const char c : mac) {
    if (c == ':' || c == '-' || c == ' ') continue;
    if (!isxdigit(static_cast<unsigned char>(c))) return false;
    hex += static_cast<char>(toupper(static_cast<unsigned char>(c)));
  }

  if (hex.empty()) {
    customMacId.clear();
    LOG_DBG("TRM", "Cleared custom MAC ID (using hardware MAC)");
    return true;
  }
  if (hex.size() != 12) {
    LOG_ERR("TRM", "Invalid MAC ID: %s", mac.c_str());
    return false;
  }

  customMacId.clear();
  customMacId.reserve(17);
  for (size_t i = 0; i < hex.size(); i += 2) {
    if (i > 0) customMacId += ':';
    customMacId += hex[i];
    customMacId += hex[i + 1];
  }
  LOG_DBG("TRM", "Set custom MAC ID: %s", customMacId.c_str());
  return true;
}

void TrmnlStore::setServerUrl(const std::string& url) {
  serverUrl = url;
  LOG_DBG("TRM", "Set server URL: %s", url.empty() ? "(default)" : url.c_str());
}

void TrmnlStore::setRefreshIntervalMinutes(const uint8_t minutes) {
  for (uint8_t i = 0; i < REFRESH_INTERVAL_OPTIONS_COUNT; i++) {
    if (REFRESH_INTERVAL_OPTIONS[i] == minutes) {
      refreshIntervalMinutes = minutes;
      return;
    }
  }
  LOG_ERR("TRM", "Invalid refresh interval: %u min", minutes);
}

void TrmnlStore::setOrientation(const uint8_t value) {
  if (value >= ORIENTATION_OPTIONS_COUNT) {
    LOG_ERR("TRM", "Invalid orientation: %u", value);
    return;
  }
  orientation = value;
}

void TrmnlStore::setRenderMode(const uint8_t value) {
  if (value >= RENDER_MODE_OPTIONS_COUNT) {
    LOG_ERR("TRM", "Invalid render mode: %u", value);
    return;
  }
  renderMode = value;
}

std::string TrmnlStore::getBaseUrl() const {
  std::string url;
  if (serverUrl.empty()) {
    url = DEFAULT_SERVER_URL;
  } else if (serverUrl.find("://") == std::string::npos) {
    // Normalize URL: add http:// if no protocol specified (local BYOS servers typically don't have SSL)
    url = "http://" + serverUrl;
  } else {
    url = serverUrl;
  }

  // Strip trailing slashes to avoid double-slash in API paths
  while (!url.empty() && url.back() == '/') {
    url.pop_back();
  }

  return url;
}
