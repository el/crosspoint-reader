#pragma once

#include <GfxRenderer.h>

#include <string>

namespace QrUtils {

// Renders a QR code with the given text payload within the specified bounding box.
void drawQrCode(GfxRenderer& renderer, const Rect& bounds, const std::string& textPayload);

}  // namespace QrUtils
