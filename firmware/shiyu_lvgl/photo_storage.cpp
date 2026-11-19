#include "photo_storage.h"

bool PhotoStorage::begin() {
  currentPath_[0] = '\0';
  if (!FFat.begin(false)) return false;
  if (!FFat.exists(kDirectory) && !FFat.mkdir(kDirectory)) return false;
  if (!restoreSelection()) selectLatest();
  return true;
}

bool PhotoStorage::restoreSelection() {
  File file = FFat.open(kSelectionPath, FILE_READ);
  if (!file) return false;
  String id = file.readString();
  file.close();
  id.trim();
  if (id.length() != 8) return false;
  for (size_t index = 0; index < id.length(); ++index) {
    if (!isDigit(id[index])) return false;
  }
  snprintf(currentPath_, sizeof(currentPath_), "%s/%s.jpg", kDirectory, id.c_str());
  if (FFat.exists(currentPath_)) return true;
  currentPath_[0] = '\0';
  return false;
}

bool PhotoStorage::selectLatest() {
  File directory = FFat.open(kDirectory);
  if (!directory || !directory.isDirectory()) return false;
  String latest;
  for (File file = directory.openNextFile(); file; file = directory.openNextFile()) {
    String name = file.name();
    if (!file.isDirectory() && name.endsWith(".jpg") && name > latest) latest = name;
  }
  if (latest.isEmpty()) return false;
  if (!latest.startsWith("/")) latest = String(kDirectory) + "/" + latest;
  latest.toCharArray(currentPath_, sizeof(currentPath_));
  const char *base = strrchr(currentPath_, '/');
  String id = base ? base + 1 : currentPath_;
  id.remove(id.length() - 4);
  persistSelection(id.c_str());
  return true;
}

uint32_t PhotoStorage::nextNumber() const {
  uint32_t largest = 0;
  File directory = FFat.open(kDirectory);
  if (!directory || !directory.isDirectory()) return 1;
  for (File file = directory.openNextFile(); file; file = directory.openNextFile()) {
    String name = file.name();
    const int slash = name.lastIndexOf('/');
    if (slash >= 0) name.remove(0, slash + 1);
    if (name.length() == 12 && name.endsWith(".jpg")) largest = max(largest, static_cast<uint32_t>(name.toInt()));
  }
  return largest + 1;
}

bool PhotoStorage::persistSelection(const char *id) {
  File file = FFat.open(kSelectionPath, FILE_WRITE);
  if (!file) return false;
  const size_t written = file.print(id);
  file.close();
  return written == strlen(id);
}

bool PhotoStorage::acceptUpload() {
  if (!FFat.exists(kTemporaryPath)) return false;
  char id[9];
  snprintf(id, sizeof(id), "%08lu", static_cast<unsigned long>(nextNumber()));
  char finalPath[40];
  snprintf(finalPath, sizeof(finalPath), "%s/%s.jpg", kDirectory, id);
  if (!FFat.rename(kTemporaryPath, finalPath)) return false;
  if (!persistSelection(id)) {
    FFat.remove(finalPath);
    return false;
  }
  strlcpy(currentPath_, finalPath, sizeof(currentPath_));
  return true;
}
