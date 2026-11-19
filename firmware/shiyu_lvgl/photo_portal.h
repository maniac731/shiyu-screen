#pragma once

#include <DNSServer.h>
#include <WebServer.h>

#include "photo_storage.h"

enum class PortalStage : uint8_t { Inactive, Waiting, Receiving, Saved, Error };

class PhotoPortal {
 public:
  explicit PhotoPortal(PhotoStorage &storage) : storage_(storage) {}
  bool start();
  void loop();
  void stop();
  bool active() const { return active_; }
  bool exitRequested() const;
  bool consumePhotoReady();
  PortalStage stage() const { return stage_; }
  uint8_t progress() const;
  const char *ssid() const { return kApName; }
  const char *password() const { return kApPassword; }

 private:
  static constexpr char kApName[] = "ShiYu-Screen-A02";
  static constexpr char kApPassword[] = "shiyu2026";
  static constexpr uint32_t kIdleTimeoutMs = 2 * 60 * 1000;

  PhotoStorage &storage_;
  DNSServer dns_;
  WebServer server_{80};
  File uploadFile_;
  size_t expectedBytes_ = 0;
  size_t receivedBytes_ = 0;
  bool active_ = false;
  bool routesRegistered_ = false;
  bool photoReady_ = false;
  bool finishRequested_ = false;
  uint32_t lastActivityMs_ = 0;
  PortalStage stage_ = PortalStage::Inactive;

  void registerRoutes();
  void handleUploadData();
  void touchActivity();
};
