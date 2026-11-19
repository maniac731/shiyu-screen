#pragma once

#include <Arduino.h>
#include "rgb_lcd_port.h"

namespace AppConfig {

constexpr bool kTouchPollingEnabled = true;
constexpr uint16_t kSettingsHitLeft = 900;
constexpr uint16_t kSettingsHitBottom = 120;
constexpr uint16_t kBackHitRight = 240;
constexpr uint16_t kBackHitBottom = 130;
constexpr uint16_t kMainNavigationTop = 512;
constexpr char kApName[] = "ShiYu-Screen-A02";
constexpr char kApPassword[] = "shiyu2026";
constexpr char kPhotoDirectory[] = "/photos";
constexpr char kTempImagePath[] = "/upload.tmp";
constexpr char kCurrentPath[] = "/current.txt";
constexpr char kLegacyImagePath[] = "/current.rgb";
constexpr size_t kFrameBytes = EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * 2;
constexpr size_t kMaxUploadBytes = 1024 * 1024;
constexpr size_t kStorageBudgetBytes = 7 * 1024 * 1024;
constexpr uint8_t kMaxPhotos = 20;
constexpr uint32_t kPortalIdleTimeoutMs = 2 * 60 * 1000;
constexpr uint32_t kPortalExitDelayMs = 700;
constexpr uint32_t kEnterPortalHoldMs = 1500;
constexpr uint16_t kPortalCornerX = 820;
constexpr uint16_t kPortalCornerY = 180;
}
