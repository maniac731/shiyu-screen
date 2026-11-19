#pragma once

#include <lvgl.h>

class PhotoImage {
 public:
  bool begin();
  bool load(const char *path);
  bool ready() const { return ready_; }
  const lv_image_dsc_t *descriptor() const { return ready_ ? &descriptor_ : nullptr; }

 private:
  static PhotoImage *instance_;
  uint8_t *pixels_ = nullptr;
  lv_image_dsc_t descriptor_{};
  bool ready_ = false;

  static bool receiveBlock(int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t *bitmap);
  bool copyBlock(int16_t x, int16_t y, uint16_t width, uint16_t height, const uint16_t *bitmap);
};
