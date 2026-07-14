#include "WifiAutoConnect.h"

#include <Arduino.h>
#include <Logging.h>
#include <WiFi.h>

#include "WifiCredentialStore.h"

namespace WifiAutoConnect {

bool tryConnectLastNetwork(const uint32_t timeoutMs, const bool* cancelFlag) {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  WIFI_STORE.loadFromFile();
  const std::string lastSsid = WIFI_STORE.getLastConnectedSsid();
  if (lastSsid.empty()) {
    LOG_DBG("WIFI", "Auto-connect: no last network");
    return false;
  }
  const auto* cred = WIFI_STORE.findCredential(lastSsid);
  if (!cred) {
    LOG_DBG("WIFI", "Auto-connect: no saved credential for %s", lastSsid.c_str());
    return false;
  }

  LOG_DBG("WIFI", "Auto-connect: trying %s", lastSsid.c_str());
  WiFi.persistent(false);  // Credentials are managed by WifiCredentialStore; suppress SDK NVS auto-connect
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);  // Abort any in-progress SDK auto-connect and clear NVS-saved SSID
  delay(100);

  // Set hostname so routers show "CrossPoint-Reader-AABBCCDDEEFF" instead of "esp32-XXXXXXXXXXXX"
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  const String hostname = "CrossPoint-Reader-" + mac;
  WiFi.setHostname(hostname.c_str());

  if (cred->password.empty()) {
    WiFi.begin(cred->ssid.c_str());
  } else {
    WiFi.begin(cred->ssid.c_str(), cred->password.c_str());
  }

  const uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (cancelFlag && *cancelFlag) {
      break;  // Caller abandoned the attempt
    }
    const wl_status_t status = WiFi.status();
    if (status == WL_CONNECTED) {
      LOG_DBG("WIFI", "Auto-connect: connected");
      return true;
    }
    if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL) {
      break;  // Definitive failure; don't burn the rest of the timeout
    }
    delay(100);
  }

  LOG_DBG("WIFI", "Auto-connect: failed");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  return false;
}

}  // namespace WifiAutoConnect
