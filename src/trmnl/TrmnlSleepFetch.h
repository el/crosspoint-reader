#pragma once

class GfxRenderer;
class MappedInputManager;

/**
 * Interactive TRMNL refresh shown on the way into sleep, before the firmware
 * commits to sleeping: a "Fetching TRMNL screen" popup with a progress bar
 * and Cancel (stay awake) / Skip (sleep with the cached image) options.
 */
namespace TrmnlSleepFetch {

enum class Result {
  FETCHED,    // Fetch finished (successfully or not) - continue into sleep
  SKIPPED,    // User chose to sleep now with the cached image
  CANCELLED,  // User chose not to sleep
};

// True when the sleep screen is set to TRMNL and the device is linked
bool shouldRun();

// Blocking; returns once the fetch ends or the user cancels/skips it.
// WiFi is always off again by the time this returns.
// The caller must tear down the current activity BEFORE calling this: the
// TLS handshake needs the reclaimed heap (wolfSSL fails with MEMORY_E
// otherwise), which also means CANCELLED must be handled with a restart
// rather than by returning to the previous activity.
Result run(GfxRenderer& renderer, MappedInputManager& mappedInput);

}  // namespace TrmnlSleepFetch
