#pragma once
#include <cstdint>

/**
 * Headless, blocking WiFi connection for flows without a selection UI
 * (e.g. refreshing the TRMNL image while entering sleep).
 */
namespace WifiAutoConnect {

/**
 * Try to connect to the last-used saved network. Returns true when connected
 * (or already connected). Bounded by timeoutMs; turns the radio back off on
 * failure. Does nothing and returns false when no saved network is available.
 */
bool tryConnectLastNetwork(uint32_t timeoutMs);

}  // namespace WifiAutoConnect
