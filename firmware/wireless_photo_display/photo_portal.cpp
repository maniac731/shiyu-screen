#include "photo_portal.h"

#include <FFat.h>
#include <TJpg_Decoder.h>
#include <WiFi.h>
#include "app_config.h"
#include "web_page.h"

PhotoPortal::PhotoPortal(PhotoStore &store) : store_(store) {}

void PhotoPortal::start() {
  if (active_) return;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AppConfig::kApName, AppConfig::kApPassword);
  dns_.start(53, "*", WiFi.softAPIP());
  if (!routesRegistered_) {
    registerRoutes();
    routesRegistered_ = true;
  }
  server_.begin();
  active_ = true;
  lastActivityMs_ = millis();
  exitRequestedMs_ = 0;
}

void PhotoPortal::loop() {
  if (!active_) return;
  dns_.processNextRequest();
  server_.handleClient();
}

bool PhotoPortal::exitRequested() const {
  if (!active_) return false;
  if (exitRequestedMs_ && millis() - exitRequestedMs_ >= AppConfig::kPortalExitDelayMs) return true;
  return millis() - lastActivityMs_ >= AppConfig::kPortalIdleTimeoutMs;
}

void PhotoPortal::stop() {
  if (!active_) return;
  server_.stop();
  dns_.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(150);
  active_ = false;
}

void PhotoPortal::touchActivity() { lastActivityMs_ = millis(); }

void PhotoPortal::registerRoutes() {
  server_.on("/", HTTP_GET, [this]() { touchActivity(); server_.send_P(200, "text/html; charset=utf-8", kUploadPage); });
  server_.on("/api/photos", HTTP_GET, [this]() { touchActivity(); sendPhotoList(); });
  server_.on("/photo", HTTP_GET, [this]() {
    touchActivity();
    const int index = store_.findById(server_.arg("id"));
    if (index < 0) return server_.send(404, "text/plain", "Not found");
    File file = FFat.open(store_.at(index).path, FILE_READ);
    server_.streamFile(file, "image/jpeg");
    file.close();
  });
  server_.on("/upload-start", HTTP_POST, [this]() {
    touchActivity();
    expectedBytes_ = server_.arg("size").toInt();
    if (!expectedBytes_ || expectedBytes_ > AppConfig::kMaxUploadBytes) return server_.send(400, "text/plain; charset=utf-8", "压缩后的图片过大");
    server_.send(200, "text/plain", "OK");
  });
  server_.on("/upload", HTTP_POST,
    [this]() { touchActivity(); server_.send(uploadSucceeded_ ? 200 : 400, "text/plain; charset=utf-8", uploadSucceeded_ ? "OK" : "图片不完整或格式不正确"); },
    [this]() { handleUploadData(); });
  server_.on("/show", HTTP_POST, [this]() {
    touchActivity();
    const int index = store_.findById(server_.arg("id"));
    if (index < 0) return server_.send(404, "text/plain; charset=utf-8", "照片不存在");
    store_.select(index);
    server_.send(200, "text/plain", "OK");
  });
  server_.on("/delete", HTTP_POST, [this]() {
    touchActivity();
    const int index = store_.findById(server_.arg("id"));
    if (index < 0 || !store_.remove(index)) return server_.send(404, "text/plain; charset=utf-8", "照片不存在");
    server_.send(200, "text/plain", "OK");
  });
  server_.on("/finish", HTTP_POST, [this]() {
    touchActivity();
    server_.send(200, "text/plain", "OK");
    exitRequestedMs_ = millis();
  });
  server_.onNotFound([this]() { touchActivity(); server_.sendHeader("Location", "http://192.168.4.1/", true); server_.send(302); });
}

void PhotoPortal::handleUploadData() {
  touchActivity();
  HTTPUpload &upload = server_.upload();
  if (upload.status == UPLOAD_FILE_START) {
    receivedBytes_ = 0;
    uploadSucceeded_ = false;
    if (uploadFile_) uploadFile_.close();
    FFat.remove(AppConfig::kTempImagePath);
    uploadFile_ = FFat.open(AppConfig::kTempImagePath, FILE_WRITE);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile_ && receivedBytes_ + upload.currentSize <= AppConfig::kMaxUploadBytes) receivedBytes_ += uploadFile_.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile_) uploadFile_.close();
    uint16_t width = 0, height = 0;
    const bool valid = expectedBytes_ && receivedBytes_ == expectedBytes_ &&
      TJpgDec.getFsJpgSize(&width, &height, AppConfig::kTempImagePath, FFat) == JDR_OK &&
      width == EXAMPLE_LCD_H_RES && height == EXAMPLE_LCD_V_RES;
    uploadSucceeded_ = valid && store_.acceptUploadedFile(AppConfig::kTempImagePath);
    if (!uploadSucceeded_) FFat.remove(AppConfig::kTempImagePath);
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (uploadFile_) uploadFile_.close();
    FFat.remove(AppConfig::kTempImagePath);
  }
}

void PhotoPortal::sendPhotoList() {
  String json = "[";
  for (uint8_t i = 0; i < store_.count(); ++i) {
    if (i) json += ',';
    json += "{\"id\":\"" + store_.idAt(i) + "\",\"bytes\":" + String(store_.at(i).bytes) +
            ",\"current\":" + String(i == store_.selectedIndex() ? "true" : "false") + "}";
  }
  json += ']';
  server_.send(200, "application/json; charset=utf-8", json);
}
