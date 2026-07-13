#pragma once
#include <HalStorage.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

/**
 * HTTP client utility for fetching content and downloading files. Built on
 * esp_http_client: https is verified against the CA bundle, plain http is
 * used for local servers (transport is chosen from the URL scheme).
 */
class HttpDownloader {
 public:
  using ProgressCallback = std::function<void(size_t downloaded, size_t total)>;
  // Called with each body chunk as it arrives; return false to abort. Lets a
  // streaming parser consume the response without buffering the whole body.
  using DataCallback = std::function<bool(const uint8_t* data, size_t len)>;
  // Extra request headers (name/value pairs), e.g. API tokens
  using Headers = std::vector<std::pair<std::string, std::string>>;

  enum DownloadError {
    OK = 0,
    HTTP_ERROR,
    FILE_ERROR,
    ABORTED,
  };

  // Per-socket-op timeout used unless a call overrides it (0 = use default).
  // The default is generous because some OPDS servers are slow to respond;
  // time-bounded flows (e.g. fetching on the way into sleep) pass less.
  static constexpr int DEFAULT_TIMEOUT_MS = 60000;

  /**
   * Fetch text content from a URL with optional credentials.
   */
  static bool fetchUrl(const std::string& url, std::string& outContent, const std::string& username = "",
                       const std::string& password = "", const Headers& headers = {}, int timeoutMs = 0);

  static bool fetchUrl(const std::string& url, Stream& stream, const std::string& username = "",
                       const std::string& password = "", const Headers& headers = {}, int timeoutMs = 0);

  /**
   * Stream the response body to onData as it arrives, without buffering it.
   */
  static bool fetchUrl(const std::string& url, const DataCallback& onData, const std::string& username = "",
                       const std::string& password = "", const Headers& headers = {}, int timeoutMs = 0);

  /**
   * Download a file to the SD card with optional credentials.
   */
  static DownloadError downloadToFile(const std::string& url, const std::string& destPath,
                                      ProgressCallback progress = nullptr, bool* cancelFlag = nullptr,
                                      const std::string& username = "", const std::string& password = "",
                                      const Headers& headers = {}, int timeoutMs = 0);
};
