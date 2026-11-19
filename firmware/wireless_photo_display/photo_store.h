#pragma once

#include <Arduino.h>
#include <FFat.h>
#include "app_config.h"

struct PhotoEntry {
  char path[32];
  size_t bytes;
};

class PhotoStore {
 public:
  bool begin();
  void refresh();
  uint8_t count() const { return count_; }
  const PhotoEntry &at(uint8_t index) const { return photos_[index]; }
  String idAt(uint8_t index) const;
  int findById(const String &id) const;
  int restoreSelectedIndex() const;
  void select(int index);
  int selectedIndex() const { return selectedIndex_; }
  bool acceptUploadedFile(const char *temporaryPath);
  bool remove(int index);

 private:
  static constexpr uint8_t kCapacity = AppConfig::kMaxPhotos + 4;
  PhotoEntry photos_[kCapacity]{};
  uint8_t count_ = 0;
  int selectedIndex_ = -1;

  void sort();
  void enforceLimits();
  size_t totalBytes() const;
  uint32_t nextNumber() const;
  void persistSelection() const;
};
