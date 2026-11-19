#include "app_controller.h"

#include <WiFi.h>
#include "app_config.h"
#include "gt911.h"

AppController::AppController() : portal_(store_) {}

void AppController::begin() {
  Serial.begin(115200);
  delay(100);
  WiFi.mode(WIFI_OFF);
  if (!psramFound() || !display_.begin()) return;
  if (!store_.begin()) {
    display_.showSetup();
    return;
  }
  restoreDisplay(false);
}

void AppController::loop() {
  if (portal_.active()) {
    portal_.loop();
    if (portal_.exitRequested()) exitPortal();
  } else if (AppConfig::kTouchPollingEnabled) {
    handleTouch();
  }
  delay(2);
}

void AppController::restoreDisplay(bool blankTransition) {
  currentPage_ = MainPage::Photo;
  const int selected = store_.selectedIndex();
  if (selected >= 0 && selected < store_.count() && display_.showJpeg(store_.at(selected).path, blankTransition)) {
    display_.prepareSettings();
    return;
  }
  if (display_.showLegacyImage()) {
    display_.prepareSettings();
    return;
  }
  display_.showSetup();
  display_.prepareSettings();
}

void AppController::enterPortal() {
  longPressHandled_ = true;
  display_.showPortalInstructions();
  delay(1200);
  display_.backlightOff();
  portal_.start();
}

void AppController::exitPortal() {
  portal_.stop();
  restoreDisplay(false);
  delay(120);
  display_.backlightOn();
}

void AppController::showAdjacent(int direction) {
  if (store_.count() < 2 || store_.selectedIndex() < 0) return;
  const int next = (store_.selectedIndex() + direction + store_.count()) % store_.count();
  if (display_.showJpeg(store_.at(next).path, true)) store_.select(next);
}

void AppController::handleTouch() {
  if (millis() - lastTouchReadMs_ < 25) return;
  lastTouchReadMs_ = millis();
  const touch_gt911_point_t point = touch_gt911_read_point(1);
  if (point.cnt > 0) {
    touchLastX_ = point.x[0];
    touchLastY_ = point.y[0];
    if (!touching_) {
      touching_ = true;
      longPressHandled_ = false;
      touchStartX_ = touchLastX_;
      touchStartY_ = touchLastY_;
      touchStartedMs_ = millis();
    }
    return;
  }
  if (!touching_) return;
  touching_ = false;
  if (longPressHandled_) return;
  const int deltaX = abs(static_cast<int>(touchLastX_) - touchStartX_);
  const int deltaY = abs(static_cast<int>(touchLastY_) - touchStartY_);
  if (!settingsOpen_ && deltaX >= 80 && deltaX > deltaY) {
    showRelativePage(touchLastX_ < touchStartX_ ? 1 : -1);
    return;
  }
  if (deltaX < 40 && deltaY < 40) handleTap(touchLastX_, touchLastY_);
}

void AppController::handleTap(uint16_t x, uint16_t y) {
  if (!settingsOpen_ && x >= AppConfig::kSettingsHitLeft && y <= AppConfig::kSettingsHitBottom) {
    settingsOpen_ = true;
    display_.showSettings();
    return;
  }
  if (settingsOpen_ && x <= AppConfig::kBackHitRight && y <= AppConfig::kBackHitBottom) {
    settingsOpen_ = false;
    if (!display_.showCachedMain()) showMainPage(currentPage_);
    display_.prepareSettings();
    return;
  }
  if (!settingsOpen_ && y >= AppConfig::kMainNavigationTop) {
    if (x < 341) showMainPage(MainPage::Today);
    else if (x < 682) showMainPage(MainPage::Inventory);
    else showMainPage(MainPage::Photo);
  }
}

void AppController::showMainPage(MainPage page) {
  currentPage_ = page;
  if (page == MainPage::Today) {
    display_.showToday();
  } else if (page == MainPage::Inventory) {
    display_.showInventory();
  } else {
    const int selected = store_.selectedIndex();
    if (selected >= 0 && selected < store_.count() && display_.showJpeg(store_.at(selected).path, false)) {
      // The selected photo is now the active main page.
    } else if (!display_.showLegacyImage()) {
      display_.showSetup();
    }
  }
  display_.prepareSettings();
}

void AppController::showRelativePage(int direction) {
  constexpr int kPageCount = 3;
  const int current = static_cast<int>(currentPage_);
  const int next = (current + direction + kPageCount) % kPageCount;
  showMainPage(static_cast<MainPage>(next));
}
