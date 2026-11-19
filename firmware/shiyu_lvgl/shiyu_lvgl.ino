#include <Arduino.h>
#include <WiFi.h>

#include "gt911.h"
#include "lvgl_port.h"
#include "photo_image.h"
#include "photo_portal.h"
#include "photo_storage.h"
#include "ui_controller.h"

UiController ui;
PhotoStorage photoStorage;
PhotoImage photoImage;
PhotoPortal photoPortal(photoStorage);

volatile bool uploadRequested = false;
volatile bool uploadExitRequested = false;

void requestUpload(void *) { uploadRequested = true; }
void requestUploadExit(void *) { uploadExitRequested = true; }

void withUiLock(void (*action)()) {
  if (lvgl_port_lock(-1)) {
    action();
    lvgl_port_unlock();
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting Shi Yu LVGL photo prototype...");
  WiFi.mode(WIFI_OFF);

  const bool storageReady = photoStorage.begin();
  const bool imageReady = photoImage.begin();
  if (storageReady && imageReady && photoStorage.hasPhoto()) {
    photoImage.load(photoStorage.currentPath());
  }

  esp_lcd_touch_handle_t touchHandle = touch_gt911_init();
  esp_lcd_panel_handle_t panelHandle = waveshare_esp32_s3_rgb_lcd_init();
  wavesahre_rgb_lcd_bl_on();
  ESP_ERROR_CHECK(lvgl_port_init(panelHandle, touchHandle));

  if (lvgl_port_lock(-1)) {
    ui.begin(requestUpload, requestUploadExit, nullptr);
    if (photoImage.ready()) ui.setAmbientPhoto(photoImage.descriptor());
    lvgl_port_unlock();
  }
}

void loop() {
  static PortalStage lastStage = PortalStage::Inactive;
  static uint8_t lastProgress = 255;

  if (uploadRequested) {
    uploadRequested = false;
    if (!photoPortal.start()) withUiLock([]() { ui.showUploadError(); });
  }

  if (uploadExitRequested) {
    uploadExitRequested = false;
    photoPortal.stop();
    withUiLock([]() { ui.closeUpload(); });
    lastStage = PortalStage::Inactive;
  }

  if (photoPortal.active()) {
    photoPortal.loop();
    const PortalStage stage = photoPortal.stage();
    const uint8_t progress = photoPortal.progress();
    if (stage == PortalStage::Receiving && progress != lastProgress) {
      lastProgress = progress;
      if (lvgl_port_lock(-1)) {
        ui.showUploadProgress(progress);
        lvgl_port_unlock();
      }
    }
    if (stage != lastStage) {
      lastStage = stage;
      if (stage == PortalStage::Saved) {
        if (photoImage.load(photoStorage.currentPath()) && lvgl_port_lock(-1)) {
          ui.setAmbientPhoto(photoImage.descriptor());
          ui.showUploadSaved();
          lvgl_port_unlock();
        } else {
          withUiLock([]() { ui.showUploadError(); });
        }
      } else if (stage == PortalStage::Error) {
        withUiLock([]() { ui.showUploadError(); });
      }
    }
    if (photoPortal.exitRequested()) {
      photoPortal.stop();
      withUiLock([]() { ui.closeUpload(); });
      lastStage = PortalStage::Inactive;
    }
  }

  delay(2);
}
