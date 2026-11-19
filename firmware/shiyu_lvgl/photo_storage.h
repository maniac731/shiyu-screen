#pragma once

#include <Arduino.h>
#include <FFat.h>

class PhotoStorage {
 public:
  static constexpr char kDirectory[] = "/photos";
  static constexpr char kTemporaryPath[] = "/upload.tmp";
  static constexpr char kSelectionPath[] = "/current.txt";
  static constexpr size_t kMaximumUploadBytes = 1024 * 1024;

  bool begin();
  bool acceptUpload();
  bool hasPhoto() const { return currentPath_[0] != '\0'; }
  const char *currentPath() const { return currentPath_; }

 private:
  char currentPath_[40] = {};

  bool restoreSelection();
  bool selectLatest();
  uint32_t nextNumber() const;
  bool persistSelection(const char *id);
};
