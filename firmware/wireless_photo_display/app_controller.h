#pragma once

#include "display_service.h"
#include "photo_portal.h"
#include "photo_store.h"

class AppController {
 public:
  AppController();
  void begin();
  void loop();

 private:
  enum class MainPage : uint8_t { Today = 0, Inventory = 1, Photo = 2 };

  PhotoStore store_;
  DisplayService display_;
  PhotoPortal portal_;
  uint16_t touchStartX_ = 0, touchStartY_ = 0, touchLastX_ = 0, touchLastY_ = 0;
  uint32_t touchStartedMs_ = 0, lastTouchReadMs_ = 0;
  bool touching_ = false, longPressHandled_ = false;
  bool settingsOpen_ = false;
  MainPage currentPage_ = MainPage::Photo;

  void restoreDisplay(bool blankTransition);
  void handleTouch();
  void handleTap(uint16_t x, uint16_t y);
  void showMainPage(MainPage page);
  void showRelativePage(int direction);
  void enterPortal();
  void exitPortal();
  void showAdjacent(int direction);
};
