#include "photo_image.h"

#include <FFat.h>
#include <TJpg_Decoder.h>
#include <esp_heap_caps.h>

#include "rgb_lcd_port.h"

PhotoImage *PhotoImage::instance_ = nullptr;

bool PhotoImage::begin() {
  instance_ = this;
  const size_t bytes = EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * 2;
  pixels_ = static_cast<uint8_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!pixels_) return false;
  descriptor_.header.magic = LV_IMAGE_HEADER_MAGIC;
  descriptor_.header.cf = LV_COLOR_FORMAT_RGB565;
  descriptor_.header.flags = LV_IMAGE_FLAGS_MODIFIABLE;
  descriptor_.header.w = EXAMPLE_LCD_H_RES;
  descriptor_.header.h = EXAMPLE_LCD_V_RES;
  descriptor_.header.stride = EXAMPLE_LCD_H_RES * 2;
  descriptor_.data_size = bytes;
  descriptor_.data = pixels_;
  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(false);
  TJpgDec.setCallback(receiveBlock);
  return true;
}

bool PhotoImage::load(const char *path) {
  if (!pixels_ || !path) return false;
  uint16_t width = 0;
  uint16_t height = 0;
  if (TJpgDec.getFsJpgSize(&width, &height, path, FFat) != JDR_OK ||
      width != EXAMPLE_LCD_H_RES || height != EXAMPLE_LCD_V_RES) return false;
  ready_ = false;
  memset(pixels_, 0, descriptor_.data_size);
  ready_ = TJpgDec.drawFsJpg(0, 0, path, FFat) == JDR_OK;
  return ready_;
}

bool PhotoImage::receiveBlock(int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t *bitmap) {
  return instance_ && instance_->copyBlock(x, y, width, height, bitmap);
}

bool PhotoImage::copyBlock(int16_t x, int16_t y, uint16_t width, uint16_t height, const uint16_t *bitmap) {
  if (x >= EXAMPLE_LCD_H_RES || y >= EXAMPLE_LCD_V_RES) return false;
  const uint16_t copyWidth = min<uint16_t>(width, EXAMPLE_LCD_H_RES - x);
  const uint16_t copyHeight = min<uint16_t>(height, EXAMPLE_LCD_V_RES - y);
  for (uint16_t row = 0; row < copyHeight; ++row) {
    memcpy(pixels_ + ((y + row) * EXAMPLE_LCD_H_RES + x) * 2,
           bitmap + row * width, copyWidth * 2);
  }
  return true;
}
