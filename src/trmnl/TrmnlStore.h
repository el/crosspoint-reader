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

  // Path of the cached TRMNL image rendered by the sleep screen
  static const char* cachedImagePath();
};

// Helper macro to access credential store
#define TRMNL_STORE TrmnlStore::getInstance()
