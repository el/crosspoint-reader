#include "TrmnlClient.h"

#include <ArduinoJson.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <PngToBmpConverter.h>
#include <WiFi.h>
#include <esp_mac.h>

#include <cstdio>

#include "TrmnlStore.h"
#include "network/HttpDownloader.h"

namespace {
constexpr char TRMNL_TMP_DOWNLOAD[] = "/.crosspoint/trmnl_dl.tmp";
constexpr char TRMNL_TMP_BMP[] = "/.crosspoint/trmnl_new.bmp";
constexpr char MODEL_NAME[] = "crosspoint-x4";

// Tighter than HttpDownloader's default: the fetch also runs on the way into
// sleep, where an unreachable server must not stall the device for a minute
constexpr int API_TIMEOUT_MS = 15000;
constexpr int IMAGE_TIMEOUT_MS = 30000;

void addCommonHeaders(HttpDownloader::Headers& headers) {
  headers.emplace_back("ID", TrmnlClient::macAddress());
  headers.emplace_back("FW-Version", CROSSPOINT_VERSION);
  headers.emplace_back("Model", MODEL_NAME);
}

// The TRMNL API wraps errors in HTTP 200 responses with a JSON status field:
// /api/setup uses 200 for success, /api/display uses 0 (and some BYOS servers
// omit the field entirely, which ArduinoJson defaults to 0).
bool isDisplayStatusOk(const int status) { return status == 0 || status == 200; }

// Resolve a possibly relative image_url (some BYOS servers) against the base URL
std::string resolveUrl(const std::string& baseUrl, const std::string& url) {
  if (!url.empty() && url[0] == '/') {
    return baseUrl + url;
  }
  return url;
}

// Replace targetPath with srcPath as atomically as SD allows, so a failed
// fetch never destroys the previously cached image.
bool replaceFile(const char* srcPath, const char* targetPath) {
  if (Storage.exists(targetPath) && !Storage.remove(targetPath)) {
    LOG_ERR("TRM", "Failed to remove %s", targetPath);
    return false;
  }
  if (!Storage.rename(srcPath, targetPath)) {
    LOG_ERR("TRM", "Failed to rename %s -> %s", srcPath, targetPath);
    return false;
  }
  return true;
}
}  // namespace

std::string TrmnlClient::macAddress() {
  // A custom MAC ID takes precedence: it lets this device act as a TRMNL
  // device that was registered under a different MAC
  if (!TRMNL_STORE.getCustomMacId().empty()) {
    return TRMNL_STORE.getCustomMacId();
  }

  // Read the station MAC straight from eFuse so this works with WiFi off
  // (the settings screen shows it before any connection is made)
  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return std::string(buf);
}

TrmnlClient::Result TrmnlClient::linkDevice() {
  if (WiFi.status() != WL_CONNECTED) {
    LOG_ERR("TRM", "Link failed: WiFi not connected");
    return NETWORK_ERROR;
  }

  HttpDownloader::Headers headers;
  addCommonHeaders(headers);

  const std::string url = TRMNL_STORE.getBaseUrl() + "/api/setup";
  std::string body;
  if (!HttpDownloader::fetchUrl(url, body, "", "", headers, API_TIMEOUT_MS)) {
    LOG_ERR("TRM", "Setup request failed");
    return NETWORK_ERROR;
  }

  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    LOG_ERR("TRM", "Setup response is not valid JSON");
    return SERVER_ERROR;
  }

  const int status = doc["status"] | 0;
  if (status != 200) {
    // 404 = MAC not registered in the TRMNL account yet
    LOG_ERR("TRM", "Setup rejected, status %d", status);
    return NOT_REGISTERED;
  }

  const std::string apiKey = doc["api_key"] | std::string("");
  if (apiKey.empty()) {
    LOG_ERR("TRM", "Setup response missing api_key");
    return SERVER_ERROR;
  }

  TRMNL_STORE.setApiKey(apiKey);
  TRMNL_STORE.setFriendlyId(doc["friendly_id"] | std::string(""));
  TRMNL_STORE.saveToFile();
  LOG_INF("TRM", "Device linked (friendly ID: %s)", TRMNL_STORE.getFriendlyId().c_str());
  return OK;
}

TrmnlClient::Result TrmnlClient::fetchImageToCache() {
  if (!TRMNL_STORE.isLinked()) {
    return NO_CREDENTIALS;
  }
  if (WiFi.status() != WL_CONNECTED) {
    LOG_ERR("TRM", "Fetch failed: WiFi not connected");
    return NETWORK_ERROR;
  }

  HttpDownloader::Headers headers;
  addCommonHeaders(headers);
  headers.emplace_back("Access-Token", TRMNL_STORE.getApiKey());
  {
    char buf[12];
    snprintf(buf, sizeof(buf), "%u", display.getDisplayWidth());
    headers.emplace_back("Width", buf);
    snprintf(buf, sizeof(buf), "%u", display.getDisplayHeight());
    headers.emplace_back("Height", buf);
    snprintf(buf, sizeof(buf), "%d", static_cast<int>(WiFi.RSSI()));
    headers.emplace_back("RSSI", buf);
  }

  const std::string baseUrl = TRMNL_STORE.getBaseUrl();
  std::string body;
  if (!HttpDownloader::fetchUrl(baseUrl + "/api/display", body, "", "", headers, API_TIMEOUT_MS)) {
    LOG_ERR("TRM", "Display request failed");
    return NETWORK_ERROR;
  }

  JsonDocument doc;
  if (deserializeJson(doc, body)) {
    LOG_ERR("TRM", "Display response is not valid JSON");
    return SERVER_ERROR;
  }

  const int status = doc["status"] | 0;
  if (!isDisplayStatusOk(status)) {
    LOG_ERR("TRM", "Display API error, status %d", status);
    return SERVER_ERROR;
  }

  const std::string imageUrl = resolveUrl(baseUrl, doc["image_url"] | std::string(""));
  if (imageUrl.empty()) {
    LOG_ERR("TRM", "Display response missing image_url");
    return SERVER_ERROR;
  }
  LOG_DBG("TRM", "Image: %s (refresh %us)", imageUrl.c_str(), static_cast<unsigned>(doc["refresh_rate"] | 0U));

  // Free the response body before the image download opens a second TLS session
  body.clear();
  body.shrink_to_fit();
  doc.clear();

  // Keep only the auth headers for the image fetch (matches the stock firmware)
  HttpDownloader::Headers imageHeaders;
  imageHeaders.emplace_back("ID", macAddress());
  imageHeaders.emplace_back("Access-Token", TRMNL_STORE.getApiKey());
  if (HttpDownloader::downloadToFile(imageUrl, TRMNL_TMP_DOWNLOAD, nullptr, nullptr, "", "", imageHeaders,
                                     IMAGE_TIMEOUT_MS) != HttpDownloader::OK) {
    LOG_ERR("TRM", "Image download failed");
    return IMAGE_ERROR;
  }

  // Sniff the format from the file magic: pre-signed image URLs often carry
  // query strings, so the extension is unreliable
  uint8_t magic[8] = {};
  {
    HalFile file;
    if (!Storage.openFileForRead("TRM", TRMNL_TMP_DOWNLOAD, file) || file.read(magic, sizeof(magic)) < 2) {
      Storage.remove(TRMNL_TMP_DOWNLOAD);
      return IMAGE_ERROR;
    }
  }

  const bool isPng = magic[0] == 0x89 && magic[1] == 'P' && magic[2] == 'N' && magic[3] == 'G';
  const bool isBmp = magic[0] == 'B' && magic[1] == 'M';

  bool cached = false;
  if (isBmp) {
    cached = replaceFile(TRMNL_TMP_DOWNLOAD, TrmnlStore::cachedImagePath());
  } else if (isPng) {
    // Convert to BMP after the HTTP connection is torn down so the TLS
    // buffers and the PNG inflate window never coexist on the heap
    bool converted = false;
    {
      HalFile pngFile;
      HalFile bmpFile;
      if (Storage.openFileForRead("TRM", TRMNL_TMP_DOWNLOAD, pngFile) &&
          Storage.openFileForWrite("TRM", TRMNL_TMP_BMP, bmpFile)) {
        // Landscape panel dimensions: TRMNL images are 800x480, so this is 1:1
        converted = PngToBmpConverter::pngFileToBmpStreamWithSize(pngFile, bmpFile, display.getDisplayWidth(),
                                                                  display.getDisplayHeight());
      }
      // Close before remove/rename on the same paths
      pngFile.close();
      bmpFile.close();
    }
    Storage.remove(TRMNL_TMP_DOWNLOAD);
    if (converted) {
      cached = replaceFile(TRMNL_TMP_BMP, TrmnlStore::cachedImagePath());
    } else {
      LOG_ERR("TRM", "PNG conversion failed");
      Storage.remove(TRMNL_TMP_BMP);
    }
  } else {
    LOG_ERR("TRM", "Unknown image format (magic %02x %02x)", magic[0], magic[1]);
    Storage.remove(TRMNL_TMP_DOWNLOAD);
  }

  if (!cached) {
    return IMAGE_ERROR;
  }
  LOG_INF("TRM", "Cached TRMNL image at %s", TrmnlStore::cachedImagePath());
  return OK;
}

const char* TrmnlClient::errorString(const Result result) {
  switch (result) {
    case OK:
      return tr(STR_TRMNL_FETCH_SUCCESS);
    case NO_CREDENTIALS:
      return tr(STR_TRMNL_NOT_LINKED);
    case NETWORK_ERROR:
      return tr(STR_CONNECTION_FAILED);
    case NOT_REGISTERED:
      return tr(STR_TRMNL_REGISTER_HINT);
    case SERVER_ERROR:
      return tr(STR_TRMNL_FETCH_FAILED);
    case IMAGE_ERROR:
      return tr(STR_DOWNLOAD_FAILED);
  }
  return tr(STR_TRMNL_FETCH_FAILED);
}
