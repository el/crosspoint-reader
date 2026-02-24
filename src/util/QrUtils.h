#pragma once

#include <GfxRenderer.h>

#include <string>

#include "components/themes/BaseTheme.h"

namespace QrUtils {

// Renders a QR code with the given text payload within the specified bounding box.
void drawQrCode(GfxRenderer& renderer, const Rect& bounds, const std::string& textPayload);

}  // namespace QrUtils
