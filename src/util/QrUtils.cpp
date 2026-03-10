#include "QrUtils.h"

#include <qrcode.h>

#include <algorithm>
#include <memory>

#include "fontIds.h"
#include "Logging.h"

void QrUtils::drawQrCode(const GfxRenderer& renderer, const Rect& bounds, const std::string& textPayload) {
  // Dynamically calculate the QR code version based on text length (Byte Mode, ECC_LOW)
  // Max capacities for Byte Mode and ECC_LOW for Versions 1-40.
  static const uint16_t MAX_CAPACITY[40] = {
      17, 32, 53, 78, 106, 134, 154, 192, 230, 271, 321, 367, 425, 458, 520, 586, 644, 718, 792, 858,
      929, 1003, 1091, 1171, 1273, 1367, 1465, 1528, 1628, 1732, 1840, 1952, 2068, 2188, 2303, 2431, 2563, 2699, 2809, 2953
  };

  const size_t len = textPayload.length();
  if (len > MAX_CAPACITY[39]) {
    LOG_ERR("QR", "Text payload (%zu bytes) is too large for max QR Code version 40 (%u bytes)", len, MAX_CAPACITY[39]);
    renderer.drawText(UI_12_FONT_ID, bounds.x, bounds.y, "Text too long for QR Code", true);
    return;
  }

  int version = 4; // Start at 4 as a minimum size for aesthetic reasons (as in original code)
  for (int i = 0; i < 40; i++) {
    if (len <= MAX_CAPACITY[i]) {
      version = std::max(4, i + 1);
      break;
    }
  }

  // Make sure we have a large enough buffer on the heap to avoid blowing the stack
  uint32_t bufferSize = qrcode_getBufferSize(version);
  auto qrcodeBytes = std::make_unique<uint8_t[]>(bufferSize);

  QRCode qrcode;
  // Initialize the QR code. We use ECC_LOW for max capacity.
  int8_t res = qrcode_initText(&qrcode, qrcodeBytes.get(), version, ECC_LOW, textPayload.c_str());

  if (res == 0) {
    // Determine the optimal pixel size.
    const int maxDim = std::min(bounds.width, bounds.height);

    int px = maxDim / qrcode.size;
    if (px < 1) px = 1;

    // Calculate centering X and Y
    const int qrDisplaySize = qrcode.size * px;
    const int xOff = bounds.x + (bounds.width - qrDisplaySize) / 2;
    const int yOff = bounds.y + (bounds.height - qrDisplaySize) / 2;

    // Draw the QR Code
    for (uint8_t cy = 0; cy < qrcode.size; cy++) {
      for (uint8_t cx = 0; cx < qrcode.size; cx++) {
        if (qrcode_getModule(&qrcode, cx, cy)) {
          renderer.fillRect(xOff + px * cx, yOff + px * cy, px, px, true);
        }
      }
    }
  } else {
    // If it fails (e.g. text too large), log an error
    LOG_ERR("QR", "Text too large for QR Code version %d", version);
  }
}
