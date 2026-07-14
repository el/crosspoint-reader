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
 * When cancelFlag is non-null and becomes true, the attempt is abandoned.
 */
bool tryConnectLastNetwork(uint32_t timeoutMs, const bool* cancelFlag = nullptr);

}  // namespace WifiAutoConnect
