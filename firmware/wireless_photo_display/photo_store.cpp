#include "photo_store.h"

bool PhotoStore::begin() {
  if (!FFat.begin(true)) return false;
  if (!FFat.exists(AppConfig::kPhotoDirectory)) FFat.mkdir(AppConfig::kPhotoDirectory);
  refresh();
  selectedIndex_ = restoreSelectedIndex();
  return true;
}

void PhotoStore::refresh() {
  count_ = 0;
  File directory = FFat.open(AppConfig::kPhotoDirectory);
  if (!directory || !directory.isDirectory()) return;
  for (File file = directory.openNextFile(); file && count_ < kCapacity; file = directory.openNextFile()) {
    const char *name = file.name();
    if (file.isDirectory() || !strstr(name, ".jpg")) continue;
    const char *base = strrchr(name, '/');
    snprintf(photos_[count_].path, sizeof(photos_[count_].path), "/photos/%s", base ? base + 1 : name);
    photos_[count_++].bytes = file.size();
  }
  sort();
}

void PhotoStore::sort() {
  for (uint8_t i = 0; i < count_; ++i) {
    for (uint8_t j = i + 1; j < count_; ++j) {
      if (strcmp(photos_[i].path, photos_[j].path) <= 0) continue;
      PhotoEntry temporary = photos_[i];
      photos_[i] = photos_[j];
      photos_[j] = temporary;
    }
  }
}

String PhotoStore::idAt(uint8_t index) const {
  const char *name = strrchr(photos_[index].path, '/');
  String id = name ? name + 1 : photos_[index].path;
  if (id.endsWith(".jpg")) id.remove(id.length() - 4);
  return id;
}

int PhotoStore::findById(const String &id) const {
  if (id.length() != 8) return -1;
  for (size_t i = 0; i < id.length(); ++i) if (!isDigit(id[i])) return -1;
  char path[32];
  snprintf(path, sizeof(path), "/photos/%s.jpg", id.c_str());
  for (uint8_t i = 0; i < count_; ++i) if (strcmp(photos_[i].path, path) == 0) return i;
  return -1;
}

int PhotoStore::restoreSelectedIndex() const {
  if (count_ == 0) return -1;
  File file = FFat.open(AppConfig::kCurrentPath, FILE_READ);
  if (!file) return count_ - 1;
  String id = file.readString();
  file.close();
  id.trim();
  const int restored = findById(id);
  return restored >= 0 ? restored : count_ - 1;
}

void PhotoStore::select(int index) {
  if (index < 0 || index >= count_) return;
  selectedIndex_ = index;
  persistSelection();
}

void PhotoStore::persistSelection() const {
  if (selectedIndex_ < 0 || selectedIndex_ >= count_) return;
  File file = FFat.open(AppConfig::kCurrentPath, FILE_WRITE);
  if (!file) return;
  file.print(idAt(selectedIndex_));
  file.close();
}

uint32_t PhotoStore::nextNumber() const {
  uint32_t largest = 0;
  for (uint8_t i = 0; i < count_; ++i) largest = max(largest, static_cast<uint32_t>(idAt(i).toInt()));
  return largest + 1;
}

bool PhotoStore::acceptUploadedFile(const char *temporaryPath) {
  refresh();
  char finalPath[32];
  snprintf(finalPath, sizeof(finalPath), "/photos/%08lu.jpg", static_cast<unsigned long>(nextNumber()));
  if (!FFat.rename(temporaryPath, finalPath)) return false;
  FFat.remove(AppConfig::kLegacyImagePath);
  refresh();
  selectedIndex_ = findById(String(strrchr(finalPath, '/') + 1).substring(0, 8));
  enforceLimits();
  selectedIndex_ = findById(String(strrchr(finalPath, '/') + 1).substring(0, 8));
  persistSelection();
  return selectedIndex_ >= 0;
}

size_t PhotoStore::totalBytes() const {
  size_t total = 0;
  for (uint8_t i = 0; i < count_; ++i) total += photos_[i].bytes;
  return total;
}

void PhotoStore::enforceLimits() {
  while (count_ > AppConfig::kMaxPhotos || totalBytes() > AppConfig::kStorageBudgetBytes) {
    FFat.remove(photos_[0].path);
    refresh();
  }
}

bool PhotoStore::remove(int index) {
  if (index < 0 || index >= count_ || !FFat.remove(photos_[index].path)) return false;
  refresh();
  if (count_ == 0) {
    selectedIndex_ = -1;
    FFat.remove(AppConfig::kCurrentPath);
  } else {
    select(min<int>(index, count_ - 1));
  }
  return true;
}
