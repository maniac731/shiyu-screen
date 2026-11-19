#pragma once

#include <DNSServer.h>
#include <WebServer.h>
#include "photo_store.h"

class PhotoPortal {
 public:
  explicit PhotoPortal(PhotoStore &store);
  void start();
  void loop();
  void stop();
  bool active() const { return active_; }
  bool exitRequested() const;

 private:
  PhotoStore &store_;
  DNSServer dns_;
  WebServer server_{80};
  File uploadFile_;
  size_t expectedBytes_ = 0;
  size_t receivedBytes_ = 0;
  bool uploadSucceeded_ = false;
  bool active_ = false;
  bool routesRegistered_ = false;
  uint32_t lastActivityMs_ = 0;
  uint32_t exitRequestedMs_ = 0;

  void registerRoutes();
  void touchActivity();
  void handleUploadData();
  void sendPhotoList();
};
