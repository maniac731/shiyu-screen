#pragma once

#include <lvgl.h>

#include "app_state.h"

class UiController {
 public:
  using ActionCallback = void (*)(void *context);

  void begin(ActionCallback uploadRequested, ActionCallback uploadExitRequested, void *callbackContext);
  void showUploadWaiting(const char *ssid, const char *password);
  void showUploadProgress(uint8_t percent);
  void showUploadSaved();
  void showUploadError();
  void closeUpload();
  void setAmbientPhoto(const lv_image_dsc_t *photo);

 private:
  AppMode mode_ = AppMode::Active;
  ActiveScreen activeScreen_ = ActiveScreen::Home;
  lv_obj_t *homeScreen_ = nullptr;
  lv_obj_t *settingsScreen_ = nullptr;
  lv_obj_t *ambientScreen_ = nullptr;
  lv_obj_t *uploadScreen_ = nullptr;
  lv_obj_t *ambientImage_ = nullptr;
  lv_obj_t *ambientModeLabel_ = nullptr;
  lv_obj_t *ambientTitle_ = nullptr;
  lv_obj_t *ambientHint_ = nullptr;
  lv_obj_t *uploadStatus_ = nullptr;
  lv_obj_t *uploadProgress_ = nullptr;
  lv_timer_t *stateTimer_ = nullptr;
  ActionCallback uploadRequested_ = nullptr;
  ActionCallback uploadExitRequested_ = nullptr;
  void *callbackContext_ = nullptr;

  void createHomeScreen();
  void createSettingsScreen();
  void createAmbientScreen();
  void createUploadScreen();
  void showHome();
  void showSettings();
  void showAmbient();
  void checkAutomaticState();

  static void onSettingsPressed(lv_event_t *event);
  static void onBackPressed(lv_event_t *event);
  static void onAmbientPressed(lv_event_t *event);
  static void onUploadPressed(lv_event_t *event);
  static void onUploadExitPressed(lv_event_t *event);
  static void onStateTimer(lv_timer_t *timer);
};

