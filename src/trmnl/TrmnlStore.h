#pragma once
#include <cstdint>
#include <string>

/**
 * Singleton class for storing TRMNL device credentials on the SD card.
 * The API key is XOR-obfuscated with the device's unique hardware MAC address
 * and base64-encoded before writing to JSON (not cryptographically secure,
 * but prevents casual reading and ties credentials to the specific device).
 */
class TrmnlStore {
 private:
  static TrmnlStore instance;
  std::string serverUrl;    // Custom TRMNL server URL (empty = official server)
  std::string apiKey;       // Device API key (from /api/setup or manual entry)
  std::string friendlyId;   // Friendly device ID assigned by the server
  std::string customMacId;  // Custom MAC for the ID header (empty = hardware MAC)
  // Dashboard mode refresh interval (minutes). Always one of REFRESH_INTERVAL_OPTIONS;
  // an invalid value found on disk falls back to the default.
  uint8_t refreshIntervalMinutes = 5;
  // Orientation the dashboard image is requested and rendered in. Defaults to
  // the panel's native landscape, which is what TRMNL dashboards are authored
  // for.
  uint8_t orientation = ORIENTATION_LANDSCAPE_CCW;
  uint8_t renderMode = RENDER_MODE_GRAYSCALE;

  // Private constructor for singleton
  TrmnlStore() = default;

 public:
  // Delete copy constructor and assignment
  TrmnlStore(const TrmnlStore&) = delete;
  TrmnlStore& operator=(const TrmnlStore&) = delete;

  // Get singleton instance
  static TrmnlStore& getInstance() { return instance; }

  // Save/load from SD card
  bool saveToFile() const;
  bool loadFromFile();

  // API key management
  void setApiKey(const std::string& key);
  const std::string& getApiKey() const { return apiKey; }
  bool isLinked() const { return !apiKey.empty(); }
  void clearApiKey();

  // Friendly ID (informational, shown in settings)
  void setFriendlyId(const std::string& id);
  const std::string& getFriendlyId() const { return friendlyId; }

  // Server URL management
  void setServerUrl(const std::string& url);
  const std::string& getServerUrl() const { return serverUrl; }

  // Custom MAC ID (for devices registered in TRMNL under a different MAC).
  // Accepts AA:BB:CC:DD:EE:FF / AA-BB-... / bare hex; normalizes to the
  // colon format the API expects. Empty clears the override (use hardware
  // MAC). Returns false and leaves the stored value unchanged on input that
  // isn't 12 hex digits.
  bool setCustomMacId(const std::string& mac);
  const std::string& getCustomMacId() const { return customMacId; }

  // Get base URL for API calls (with http:// normalization if no protocol, falls back to default)
  std::string getBaseUrl() const;

  // Dashboard mode refresh interval, in minutes (TrmnlDashboardActivity). Only
  // values in REFRESH_INTERVAL_OPTIONS are accepted; anything else is ignored.
  static constexpr uint8_t REFRESH_INTERVAL_OPTIONS[] = {3, 5, 10, 15, 30};
  static constexpr uint8_t REFRESH_INTERVAL_OPTIONS_COUNT = 5;
  void setRefreshIntervalMinutes(uint8_t minutes);
  uint8_t getRefreshIntervalMinutes() const { return refreshIntervalMinutes; }

  // Orientation the dashboard image is fetched and rendered in, mirroring
  // GfxRenderer::Orientation's declared order. Stored as a plain uint8_t (not
  // the renderer enum) so the store stays free of a renderer dependency and
  // round-trips through JSON unchanged; callers cast on use. Out-of-range
  // values are rejected. Portrait orientations swap the width/height the TRMNL
  // API is asked for, so the server can render a portrait dashboard.
  static constexpr uint8_t ORIENTATION_PORTRAIT = 0;
  static constexpr uint8_t ORIENTATION_LANDSCAPE_CW = 1;
  static constexpr uint8_t ORIENTATION_PORTRAIT_INVERTED = 2;
  static constexpr uint8_t ORIENTATION_LANDSCAPE_CCW = 3;
  static constexpr uint8_t ORIENTATION_OPTIONS_COUNT = 4;
  void setOrientation(uint8_t value);
  uint8_t getOrientation() const { return orientation; }
  bool isPortraitOrientation() const {
    return orientation == ORIENTATION_PORTRAIT || orientation == ORIENTATION_PORTRAIT_INVERTED;
  }

  // How TrmnlDashboardActivity paints the cached image. Grayscale drives the
  // panel's 4 levels, which costs three extra passes over the BMP and a slower
  // refresh; the two single-pass modes collapse everything non-white to black,
  // which is faster and is all a 1-bit dashboard can show anyway. Out-of-range
  // values are rejected.
  static constexpr uint8_t RENDER_MODE_GRAYSCALE = 0;
  static constexpr uint8_t RENDER_MODE_BW = 1;
  static constexpr uint8_t RENDER_MODE_BW_INVERTED = 2;
  static constexpr uint8_t RENDER_MODE_OPTIONS_COUNT = 3;
  void setRenderMode(uint8_t value);
  uint8_t getRenderMode() const { return renderMode; }

  // Path of the cached TRMNL image rendered by the sleep screen
  static const char* cachedImagePath();
};

// Helper macro to access credential store
#define TRMNL_STORE TrmnlStore::getInstance()
