#include "FsHelpers.h"

#include "Bitmap.h" // Required for BmpHeader struct definition
#include <BitmapHelpers.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>

#include <string>
#include <vector>

std::string FsHelpers::normalisePath(const std::string &path) {
  std::vector<std::string> components;
  std::string component;

  for (const auto c : path) {
    if (c == '/') {
      if (!component.empty()) {
        if (component == "..") {
          if (!components.empty()) {
            components.pop_back();
          }
        } else {
          components.push_back(component);
        }
        component.clear();
      }
    } else {
      component += c;
    }
  }

  if (!component.empty()) {
    components.push_back(component);
  }

  std::string result;
  for (const auto &c : components) {
    if (!result.empty()) {
      result += "/";
    }
    result += c;
  }

  return result;
}

bool FsHelpers::saveFramebufferAsBmp(const char *filename,
                                     const uint8_t *framebuffer, int width,
                                     int height,
                                     GfxRenderer::Orientation orientation) {
  if (!framebuffer) {
    return false;
  }

  std::string path(filename);
  size_t last_slash = path.find_last_of('/');
  if (last_slash != std::string::npos) {
    std::string dir = path.substr(0, last_slash);
    if (!SdMan.exists(dir.c_str())) {
      if (!SdMan.mkdir(dir.c_str())) {
        return false;
      }
    }
  }

  FsFile file;
  if (!SdMan.openFileForWrite("SCR", filename, file)) {
    Serial.printf("[%lu] [SCR] Failed to open file for writing\n", millis());
    return false;
  }

  int bmpWidth = width;
  int bmpHeight = height;
  bool rotate = false;

  switch (orientation) {
    case GfxRenderer::Orientation::LandscapeClockwise:
    case GfxRenderer::Orientation::LandscapeCounterClockwise:
      bmpWidth = height;
      bmpHeight = width;
      rotate = true;
      break;
    default:
      // Portrait, PortraitInverted
      break;
  }

  BmpHeader header;
  createBmpHeader(&header, bmpWidth, bmpHeight);

  if (file.write((uint8_t *)&header, sizeof(header)) != sizeof(header)) {
    file.close();
    SdMan.remove(filename);
    return false;
  }

  bool write_error = false;
  if (rotate) {
    const uint32_t src_row_bytes = (width + 7) / 8;
    const uint32_t dest_row_bytes = (bmpWidth + 7) / 8;
    const uint32_t bmp_row_size = (dest_row_bytes + 3) & ~3;
    std::vector<uint8_t> row_buf(bmp_row_size);

    for (int y_bmp = 0; y_bmp < bmpHeight; y_bmp++) {
      std::fill(row_buf.begin(), row_buf.end(), 0);
      int y_new = bmpHeight - 1 - y_bmp;

      for (int x_new = 0; x_new < bmpWidth; x_new++) {
        int x_old, y_old;
        if (orientation == GfxRenderer::Orientation::LandscapeCounterClockwise) {
          // 90 deg CCW rotation: x_new = y_old, y_new = width - 1 - x_old
          x_old = width - 1 - y_new;
          y_old = x_new;
        } else {  // LandscapeClockwise, 90 deg CW
          // 90 deg CW rotation: x_new = height - 1 - y_old, y_new = x_old
          x_old = y_new;
          y_old = height - 1 - x_new;
        }

        const uint8_t *src_byte_ptr = framebuffer + (y_old * src_row_bytes) + (x_old / 8);
        uint8_t src_bit_mask = 1 << (7 - (x_old % 8));

        if (*src_byte_ptr & src_bit_mask) {
          uint8_t *dest_byte_ptr = row_buf.data() + (x_new / 8);
          uint8_t dest_bit_mask = 1 << (7 - (x_new % 8));
          *dest_byte_ptr |= dest_bit_mask;
        }
      }

      if (file.write(row_buf.data(), bmp_row_size) != bmp_row_size) {
        write_error = true;
        break;
      }
    }
  } else { // No rotation, just handle potential inversion
    const uint32_t rowSize = (width + 31) / 32 * 4;
    const uint32_t fbRowSize = width / 8;
    const uint32_t paddingSize = rowSize - fbRowSize;
    uint8_t padding[4] = {0, 0, 0, 0};
    bool inverted = (orientation == GfxRenderer::Orientation::PortraitInverted);

    for (int y = 0; y < height; y++) {
      int y_src = inverted ? y : height - 1 - y;
      const uint8_t *fbRow = framebuffer + y_src * fbRowSize;
      
      if (inverted) { // need to invert bits
        std::vector<uint8_t> inverted_row(fbRowSize);
        for(size_t i = 0; i < fbRowSize; ++i) {
            inverted_row[i] = ~fbRow[i];
        }
        if (file.write(inverted_row.data(), fbRowSize) != fbRowSize) {
            write_error = true;
            break;
        }
      } else {
        if (file.write(fbRow, fbRowSize) != fbRowSize) {
          write_error = true;
          break;
        }
      }

      if (paddingSize > 0) {
        if (file.write(padding, paddingSize) != paddingSize) {
          write_error = true;
          break;
        }
      }
    }
  }

  file.close();

  if (write_error) {
    SdMan.remove(filename);
    return false;
  }

  return true;
}
