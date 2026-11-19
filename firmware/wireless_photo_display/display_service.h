#pragma once

#include <Arduino.h>
#include "app_config.h"

class DisplayService {
 public:
  bool begin();
  bool showJpeg(const char *path, bool blankTransition);
  bool showLegacyImage();
  bool showCachedPhoto();
  bool showCachedMain();
  void showToday();
  void showInventory();
  void prepareSettings();
  void showSetup();
  void showSettings();
  void showPortalInstructions();
  void backlightOff();
  void backlightOn();

 private:
  static DisplayService *instance_;
  uint8_t *frame_ = nullptr;
  uint8_t *framebuffers_[2] = {nullptr, nullptr};
  int8_t frontIndex_ = 0;
  int8_t photoIndex_ = -1;
  int8_t mainIndex_ = -1;
  int8_t settingsIndex_ = -1;

  static bool receiveJpegBlock(int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t *bitmap);
  bool copyJpegBlock(int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t *bitmap);
  void beginBackFrame();
  void presentBackFrame();
  void presentFrame(int8_t index);
  void drawSettingsButton();
  void drawMainNavigation(uint8_t activePage);
  void drawTodayPage();
  void drawInventoryPage();
  void drawSettingsPage();
};
