#pragma once
#include <string>

/**
 * Client for the TRMNL device API (https://docs.trmnl.com).
 *
 * Endpoints (GET, authenticated via HTTP headers):
 *   /api/setup    ID: <mac>                          -> {status, api_key, friendly_id}
 *   /api/display  ID: <mac>, Access-Token: <api_key> -> {status, image_url, filename, refresh_rate}
 *
 * The display image (PNG or BMP, requested at the panel size for the
 * orientation configured in TrmnlStore) is downloaded and cached as a BMP on
 * the SD card for the sleep screen to render without a network connection.
 *
 * All calls are blocking and require an already-established WiFi connection.
 */
class TrmnlClient {
 public:
  enum Result {
    OK = 0,
    NO_CREDENTIALS,  // Device not linked (no API key stored)
    NETWORK_ERROR,   // WiFi down or server unreachable
    NOT_REGISTERED,  // /api/setup rejected the MAC (not registered in the TRMNL account)
    SERVER_ERROR,    // API responded with an error status or unparseable JSON
    IMAGE_ERROR,     // Image download or conversion failed
  };

  /**
   * Auto-provision this device: fetch an API key for our MAC address via
   * /api/setup and persist it in TRMNL_STORE on success.
   */
  static Result linkDevice();

  /**
   * Fetch the current dashboard image via /api/display and cache it as
   * TrmnlStore::cachedImagePath() (converting PNG to BMP if needed).
   */
  static Result fetchImageToCache();

  /** Device MAC address in the AA:BB:CC:DD:EE:FF format the TRMNL API expects. */
  static std::string macAddress();

  /** Translated, user-displayable description of a Result. */
  static const char* errorString(Result result);
};
