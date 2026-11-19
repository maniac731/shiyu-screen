#include "display_service.h"

#include <FFat.h>
#include <TJpg_Decoder.h>
#include "gt911.h"
#include "gui_paint.h"
#include "rgb_lcd_port.h"

DisplayService *DisplayService::instance_ = nullptr;

bool DisplayService::begin() {
  instance_ = this;
  touch_gt911_init();
  waveshare_esp32_s3_rgb_lcd_init();
  wavesahre_rgb_lcd_bl_on();
  waveshare_get_frame_buffer(reinterpret_cast<void **>(&framebuffers_[0]), reinterpret_cast<void **>(&framebuffers_[1]));
  if (!framebuffers_[0] || !framebuffers_[1]) return false;
  memset(framebuffers_[0], 0, AppConfig::kFrameBytes);
  memset(framebuffers_[1], 0, AppConfig::kFrameBytes);
  frame_ = framebuffers_[0];
  Paint_NewImage(frame_, EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES, 0, WHITE);
  Paint_SetScale(65);
  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(false);
  TJpgDec.setCallback(receiveJpegBlock);
  return true;
}

void DisplayService::beginBackFrame() {
  const int8_t backIndex = 1 - frontIndex_;
  frame_ = framebuffers_[backIndex];
  Paint_SelectImage(frame_);
}

void DisplayService::presentBackFrame() {
  wavesahre_rgb_lcd_display(frame_);
  frontIndex_ = frame_ == framebuffers_[0] ? 0 : 1;
}

void DisplayService::presentFrame(int8_t index) {
  if (index < 0 || index > 1) return;
  frame_ = framebuffers_[index];
  wavesahre_rgb_lcd_display(frame_);
  frontIndex_ = index;
}

bool DisplayService::receiveJpegBlock(int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t *bitmap) {
  return instance_ && instance_->copyJpegBlock(x, y, width, height, bitmap);
}

bool DisplayService::copyJpegBlock(int16_t x, int16_t y, uint16_t width, uint16_t height, uint16_t *bitmap) {
  if (x >= EXAMPLE_LCD_H_RES || y >= EXAMPLE_LCD_V_RES) return false;
  const uint16_t copyWidth = min<uint16_t>(width, EXAMPLE_LCD_H_RES - x);
  const uint16_t copyHeight = min<uint16_t>(height, EXAMPLE_LCD_V_RES - y);
  for (uint16_t row = 0; row < copyHeight; ++row) {
    memcpy(frame_ + ((y + row) * EXAMPLE_LCD_H_RES + x) * 2, bitmap + row * width, copyWidth * 2);
  }
  return true;
}

bool DisplayService::showJpeg(const char *path, bool blankTransition) {
  if (blankTransition) {
    backlightOff();
    delay(30);
  }
  uint16_t width = 0, height = 0;
  const bool dimensionsValid = TJpgDec.getFsJpgSize(&width, &height, path, FFat) == JDR_OK &&
                               width == EXAMPLE_LCD_H_RES && height == EXAMPLE_LCD_V_RES;
  bool displayed = false;
  if (dimensionsValid) {
    beginBackFrame();
    memset(frame_, 0, AppConfig::kFrameBytes);
    displayed = TJpgDec.drawFsJpg(0, 0, path, FFat) == JDR_OK;
    if (displayed) {
      drawSettingsButton();
      drawMainNavigation(2);
      presentBackFrame();
      photoIndex_ = frontIndex_;
      mainIndex_ = frontIndex_;
    }
  }
  if (blankTransition) {
    delay(100);
    backlightOn();
  }
  return displayed;
}

bool DisplayService::showLegacyImage() {
  File image = FFat.open(AppConfig::kLegacyImagePath, FILE_READ);
  if (!image || image.size() != AppConfig::kFrameBytes) {
    if (image) image.close();
    return false;
  }
  beginBackFrame();
  const size_t count = image.read(frame_, AppConfig::kFrameBytes);
  image.close();
  if (count != AppConfig::kFrameBytes) return false;
  drawSettingsButton();
  drawMainNavigation(2);
  presentBackFrame();
  photoIndex_ = frontIndex_;
  mainIndex_ = frontIndex_;
  return true;
}

bool DisplayService::showCachedPhoto() {
  if (photoIndex_ < 0 || photoIndex_ > 1) return false;
  if (frontIndex_ != photoIndex_) {
    frame_ = framebuffers_[photoIndex_];
    wavesahre_rgb_lcd_display(frame_);
    frontIndex_ = photoIndex_;
  }
  return true;
}

bool DisplayService::showCachedMain() {
  if (mainIndex_ < 0 || mainIndex_ > 1) return false;
  presentFrame(mainIndex_);
  return true;
}

void DisplayService::drawMainNavigation(uint8_t activePage) {
  constexpr UWORD bar = 0xFFDF;
  constexpr UWORD ink = 0x22A7;
  constexpr UWORD muted = 0x8C51;
  constexpr UWORD accent = 0x6A69;
  Paint_DrawRectangle(0, 512, 1023, 599, bar, DOT_PIXEL_1X1, DRAW_FILL_FULL);
  Paint_DrawLine(0, 512, 1023, 512, 0xDEFB, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
  Paint_DrawString_EN(125, 548, "TODAY", &Font24, activePage == 0 ? ink : muted, bar);
  Paint_DrawString_EN(409, 548, "INVENTORY", &Font24, activePage == 1 ? ink : muted, bar);
  Paint_DrawString_EN(793, 548, "PHOTO", &Font24, activePage == 2 ? ink : muted, bar);
  const UWORD centers[3] = {168, 485, 835};
  Paint_DrawRectangle(centers[activePage] - 42, 588, centers[activePage] + 42, 594, accent,
                      DOT_PIXEL_1X1, DRAW_FILL_FULL);
}

void DisplayService::drawTodayPage() {
  constexpr UWORD background = 0xFF7A;
  constexpr UWORD ink = 0x22A7;
  constexpr UWORD muted = 0x7BCF;
  constexpr UWORD card = 0xFFFF;
  constexpr UWORD sage = 0xC632;
  constexpr UWORD peach = 0xF5CC;
  constexpr UWORD alert = 0xFBAE;
  Paint_Clear(background);
  Paint_DrawString_EN(48, 36, "SHI YU", &Font24, muted, background);
  Paint_DrawString_EN(48, 82, "TODAY", &Font24, ink, background);
  Paint_DrawString_EN(690, 55, "TUE  NOV 10", &Font24, muted, background);
  drawSettingsButton();

  Paint_DrawRectangle(48, 150, 605, 478, card, DOT_PIXEL_2X2, DRAW_FILL_FULL);
  Paint_DrawRectangle(48, 150, 66, 478, sage, DOT_PIXEL_1X1, DRAW_FILL_FULL);
  Paint_DrawString_EN(96, 184, "DINNER IDEA", &Font24, muted, card);
  Paint_DrawString_EN(96, 240, "TOMATO EGG", &Font24, ink, card);
  Paint_DrawString_EN(96, 290, "WITH WARM RICE", &Font24, ink, card);
  Paint_DrawString_EN(96, 382, "Uses 3 items already at home", &Font24, muted, card);
  Paint_DrawRectangle(96, 425, 330, 462, sage, DOT_PIXEL_1X1, DRAW_FILL_FULL);
  Paint_DrawString_EN(121, 432, "VIEW RECIPE", &Font24, ink, sage);

  Paint_DrawRectangle(633, 150, 976, 295, peach, DOT_PIXEL_2X2, DRAW_FILL_FULL);
  Paint_DrawString_EN(669, 180, "USE SOON", &Font24, muted, peach);
  Paint_DrawString_EN(669, 224, "2 ITEMS", &Font24, ink, peach);

  Paint_DrawRectangle(633, 315, 976, 478, alert, DOT_PIXEL_2X2, DRAW_FILL_FULL);
  Paint_DrawString_EN(669, 345, "AT HOME", &Font24, muted, alert);
  Paint_DrawString_EN(669, 392, "18 ITEMS", &Font24, ink, alert);
  Paint_DrawString_EN(669, 449, "ENOUGH FOR 4 MEALS", &Font24, muted, alert);
  drawMainNavigation(0);
}

void DisplayService::showToday() {
  beginBackFrame();
  drawTodayPage();
  presentBackFrame();
  mainIndex_ = frontIndex_;
  photoIndex_ = -1;
  settingsIndex_ = -1;
}

void DisplayService::drawInventoryPage() {
  constexpr UWORD background = 0xF7BD;
  constexpr UWORD ink = 0x22A7;
  constexpr UWORD muted = 0x7BCF;
  constexpr UWORD card = 0xFFFF;
  constexpr UWORD sage = 0xC632;
  constexpr UWORD peach = 0xF5CC;
  Paint_Clear(background);
  Paint_DrawString_EN(48, 36, "SHI YU", &Font24, muted, background);
  Paint_DrawString_EN(48, 82, "INVENTORY", &Font24, ink, background);
  drawSettingsButton();

  Paint_DrawRectangle(48, 150, 310, 478, sage, DOT_PIXEL_2X2, DRAW_FILL_FULL);
  Paint_DrawString_EN(84, 184, "AT HOME", &Font24, muted, sage);
  Paint_DrawString_EN(84, 235, "18", &Font24, ink, sage);
  Paint_DrawString_EN(84, 300, "items", &Font24, ink, sage);
  Paint_DrawLine(84, 350, 274, 350, muted, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
  Paint_DrawString_EN(84, 380, "2 USE SOON", &Font24, ink, sage);
  Paint_DrawString_EN(84, 425, "4 MEALS", &Font24, ink, sage);

  Paint_DrawRectangle(338, 150, 976, 248, card, DOT_PIXEL_2X2, DRAW_FILL_FULL);
  Paint_DrawCircle(386, 199, 22, peach, DOT_PIXEL_1X1, DRAW_FILL_FULL);
  Paint_DrawString_EN(430, 175, "Tomatoes", &Font24, ink, card);
  Paint_DrawString_EN(430, 213, "3 LEFT  |  USE TODAY", &Font24, muted, card);
  Paint_DrawString_EN(891, 185, "3", &Font24, ink, card);

  Paint_DrawRectangle(338, 265, 976, 363, card, DOT_PIXEL_2X2, DRAW_FILL_FULL);
  Paint_DrawCircle(386, 314, 22, sage, DOT_PIXEL_1X1, DRAW_FILL_FULL);
  Paint_DrawString_EN(430, 290, "Eggs", &Font24, ink, card);
  Paint_DrawString_EN(430, 328, "6 LEFT  |  FRESH", &Font24, muted, card);
  Paint_DrawString_EN(891, 300, "6", &Font24, ink, card);

  Paint_DrawRectangle(338, 380, 976, 478, card, DOT_PIXEL_2X2, DRAW_FILL_FULL);
  Paint_DrawCircle(386, 429, 22, 0xBDF7, DOT_PIXEL_1X1, DRAW_FILL_FULL);
  Paint_DrawString_EN(430, 405, "Milk", &Font24, ink, card);
  Paint_DrawString_EN(430, 443, "1 BOTTLE  |  3 DAYS", &Font24, muted, card);
  Paint_DrawString_EN(891, 415, "1", &Font24, ink, card);
  drawMainNavigation(1);
}

void DisplayService::showInventory() {
  beginBackFrame();
  drawInventoryPage();
  presentBackFrame();
  mainIndex_ = frontIndex_;
  photoIndex_ = -1;
  settingsIndex_ = -1;
}

void DisplayService::drawSettingsPage() {
  constexpr UWORD background = 0xF79D;
  constexpr UWORD ink = 0x22A7;
  constexpr UWORD muted = 0x7BCF;
  constexpr UWORD card = 0xEF7D;
  Paint_Clear(background);
  Paint_DrawRectangle(40, 30, 210, 108, card, DOT_PIXEL_2X2, DRAW_FILL_FULL);
  Paint_DrawString_EN(76, 57, "<  BACK", &Font24, ink, card);
  Paint_DrawString_EN(72, 145, "SETTINGS", &Font24, ink, background);
  Paint_DrawRectangle(72, 220, 952, 360, card, DOT_PIXEL_2X2, DRAW_FILL_FULL);
  Paint_DrawString_EN(120, 258, "PHONE MANAGEMENT", &Font24, ink, card);
  Paint_DrawString_EN(120, 305, "Photo upload and inventory setup", &Font24, muted, card);
  Paint_DrawString_EN(72, 440, "Wi-Fi remains off in this touch test", &Font24, muted, background);
}

void DisplayService::prepareSettings() {
  beginBackFrame();
  drawSettingsPage();
  settingsIndex_ = frame_ == framebuffers_[0] ? 0 : 1;
}

void DisplayService::showSetup() {
  beginBackFrame();
  Paint_Clear(WHITE);
  Paint_DrawString_EN(90, 120, "SHI YU PHOTO SCREEN", &Font24, 0x2468, WHITE);
  Paint_DrawString_EN(90, 220, "No photo yet", &Font24, BLACK, WHITE);
  Paint_DrawString_EN(90, 280, "Open settings to connect a phone", &Font24, BLACK, WHITE);
  drawSettingsButton();
  drawMainNavigation(2);
  presentBackFrame();
  photoIndex_ = -1;
  mainIndex_ = frontIndex_;
}

void DisplayService::drawSettingsButton() {
  Paint_SelectImage(frame_);
  constexpr UWORD centerX = 964;
  constexpr UWORD centerY = 60;
  constexpr UWORD surface = 0xEF7D;
  constexpr UWORD ink = 0x22A7;
  Paint_DrawCircle(centerX, centerY, 42, surface, DOT_PIXEL_2X2, DRAW_FILL_FULL);
  Paint_DrawCircle(centerX, centerY, 23, ink, DOT_PIXEL_3X3, DRAW_FILL_EMPTY);
  Paint_DrawCircle(centerX, centerY, 8, ink, DOT_PIXEL_3X3, DRAW_FILL_EMPTY);
  Paint_DrawLine(centerX, 27, centerX, 36, ink, DOT_PIXEL_5X5, LINE_STYLE_SOLID);
  Paint_DrawLine(centerX, 84, centerX, 93, ink, DOT_PIXEL_5X5, LINE_STYLE_SOLID);
  Paint_DrawLine(931, centerY, 940, centerY, ink, DOT_PIXEL_5X5, LINE_STYLE_SOLID);
  Paint_DrawLine(988, centerY, 997, centerY, ink, DOT_PIXEL_5X5, LINE_STYLE_SOLID);
  Paint_DrawLine(941, 37, 947, 43, ink, DOT_PIXEL_5X5, LINE_STYLE_SOLID);
  Paint_DrawLine(981, 77, 987, 83, ink, DOT_PIXEL_5X5, LINE_STYLE_SOLID);
  Paint_DrawLine(941, 83, 947, 77, ink, DOT_PIXEL_5X5, LINE_STYLE_SOLID);
  Paint_DrawLine(981, 43, 987, 37, ink, DOT_PIXEL_5X5, LINE_STYLE_SOLID);
}

void DisplayService::showSettings() {
  if (settingsIndex_ < 0) prepareSettings();
  presentFrame(settingsIndex_);
}

void DisplayService::showPortalInstructions() {
  beginBackFrame();
  Paint_Clear(BLACK);
  Paint_DrawString_EN(340, 170, "UPLOAD MODE", &Font24, WHITE, BLACK);
  Paint_DrawString_EN(210, 250, "Wi-Fi: ShiYu-Screen-A02", &Font24, WHITE, BLACK);
  Paint_DrawString_EN(210, 300, "Password: shiyu2026", &Font24, WHITE, BLACK);
  Paint_DrawString_EN(210, 350, "Open: http://192.168.4.1", &Font24, WHITE, BLACK);
  presentBackFrame();
}

void DisplayService::backlightOff() { wavesahre_rgb_lcd_bl_off(); }
void DisplayService::backlightOn() { wavesahre_rgb_lcd_bl_on(); }
