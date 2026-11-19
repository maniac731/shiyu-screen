#include "ui_controller.h"

#include <Arduino.h>

#include "ui_theme.h"

namespace {

void setTextStyle(lv_obj_t *label, uint32_t color, const lv_font_t *font) {
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
  lv_obj_set_style_text_font(label, font, 0);
}

lv_obj_t *makeLabel(lv_obj_t *parent, const char *text, int x, int y,
                    uint32_t color, const lv_font_t *font) {
  lv_obj_t *label = lv_label_create(parent);
  lv_label_set_text(label, text);
  setTextStyle(label, color, font);
  lv_obj_set_pos(label, x, y);
  return label;
}

void prepareScreen(lv_obj_t *screen, uint32_t background) {
  lv_obj_set_style_bg_color(screen, lv_color_hex(background), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(screen, 0, 0);
  lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *makeRoundedButton(lv_obj_t *parent, int x, int y, int width, int height,
                            uint32_t background, const char *text) {
  lv_obj_t *button = lv_button_create(parent);
  lv_obj_set_pos(button, x, y);
  lv_obj_set_size(button, width, height);
  lv_obj_set_style_radius(button, height / 2, 0);
  lv_obj_set_style_bg_color(button, lv_color_hex(background), 0);
  lv_obj_set_style_shadow_width(button, 0, 0);
  lv_obj_t *label = lv_label_create(button);
  lv_label_set_text(label, text);
  setTextStyle(label, UiTheme::kPrimary, &lv_font_montserrat_16);
  lv_obj_center(label);
  return button;
}

}  // namespace

void UiController::begin(ActionCallback uploadRequested, ActionCallback uploadExitRequested, void *callbackContext) {
  uploadRequested_ = uploadRequested;
  uploadExitRequested_ = uploadExitRequested;
  callbackContext_ = callbackContext;
  createHomeScreen();
  createSettingsScreen();
  createAmbientScreen();
  createUploadScreen();
  stateTimer_ = lv_timer_create(onStateTimer, UiTheme::kStateCheckPeriodMs, this);
  showHome();
}

void UiController::createHomeScreen() {
  homeScreen_ = lv_obj_create(nullptr);
  prepareScreen(homeScreen_, UiTheme::kBackground);

  makeLabel(homeScreen_, "SHI YU", 48, 36, UiTheme::kSecondary, &lv_font_montserrat_24);
  makeLabel(homeScreen_, "Your kitchen, at a glance", 48, 79, UiTheme::kPrimary,
            &lv_font_montserrat_32);

  lv_obj_t *settings = makeRoundedButton(homeScreen_, 852, 32, 132, 56,
                                         UiTheme::kSurface, "SETTINGS");
  lv_obj_add_event_cb(settings, onSettingsPressed, LV_EVENT_CLICKED, this);

  lv_obj_t *mealCard = lv_obj_create(homeScreen_);
  lv_obj_set_pos(mealCard, 48, 150);
  lv_obj_set_size(mealCard, 574, 330);
  lv_obj_set_style_radius(mealCard, 24, 0);
  lv_obj_set_style_bg_color(mealCard, lv_color_hex(UiTheme::kSurface), 0);
  lv_obj_set_style_border_width(mealCard, 0, 0);
  lv_obj_set_style_pad_all(mealCard, 32, 0);
  lv_obj_remove_flag(mealCard, LV_OBJ_FLAG_SCROLLABLE);
  makeLabel(mealCard, "DINNER IDEA", 0, 0, UiTheme::kSecondary, &lv_font_montserrat_18);
  makeLabel(mealCard, "Tomato egg", 0, 56, UiTheme::kPrimary, &lv_font_montserrat_32);
  makeLabel(mealCard, "with warm rice", 0, 101, UiTheme::kPrimary, &lv_font_montserrat_32);
  makeLabel(mealCard, "Uses 3 ingredients already at home", 0, 176,
            UiTheme::kSecondary, &lv_font_montserrat_18);
  makeRoundedButton(mealCard, 0, 232, 220, 46, UiTheme::kAccent, "VIEW SUGGESTION");

  lv_obj_t *expiryCard = lv_obj_create(homeScreen_);
  lv_obj_set_pos(expiryCard, 650, 150);
  lv_obj_set_size(expiryCard, 326, 214);
  lv_obj_set_style_radius(expiryCard, 24, 0);
  lv_obj_set_style_bg_color(expiryCard, lv_color_hex(UiTheme::kWarning), 0);
  lv_obj_set_style_border_width(expiryCard, 0, 0);
  lv_obj_set_style_pad_all(expiryCard, 28, 0);
  lv_obj_remove_flag(expiryCard, LV_OBJ_FLAG_SCROLLABLE);
  makeLabel(expiryCard, "USE SOON", 0, 0, UiTheme::kSecondary, &lv_font_montserrat_18);
  makeLabel(expiryCard, "2 items", 0, 50, UiTheme::kPrimary, &lv_font_montserrat_32);
  makeLabel(expiryCard, "Milk and tomatoes", 0, 112, UiTheme::kPrimary,
            &lv_font_montserrat_18);

  lv_obj_t *inventoryCard = lv_obj_create(homeScreen_);
  lv_obj_set_pos(inventoryCard, 650, 388);
  lv_obj_set_size(inventoryCard, 326, 92);
  lv_obj_set_style_radius(inventoryCard, 24, 0);
  lv_obj_set_style_bg_color(inventoryCard, lv_color_hex(UiTheme::kAccent), 0);
  lv_obj_set_style_border_width(inventoryCard, 0, 0);
  lv_obj_remove_flag(inventoryCard, LV_OBJ_FLAG_SCROLLABLE);
  makeLabel(inventoryCard, "18 ITEMS AT HOME", 24, 21, UiTheme::kPrimary,
            &lv_font_montserrat_18);

  makeLabel(homeScreen_, "NO TOUCH FOR 60 SEC  ->  AMBIENT", 48, 542,
            UiTheme::kSecondary, &lv_font_montserrat_18);
}

void UiController::createSettingsScreen() {
  settingsScreen_ = lv_obj_create(nullptr);
  prepareScreen(settingsScreen_, UiTheme::kBackground);

  lv_obj_t *back = makeRoundedButton(settingsScreen_, 40, 32, 132, 56,
                                     UiTheme::kSurface, "<  BACK");
  lv_obj_add_event_cb(back, onBackPressed, LV_EVENT_CLICKED, this);
  makeLabel(settingsScreen_, "SETTINGS", 48, 124, UiTheme::kPrimary,
            &lv_font_montserrat_32);
  makeLabel(settingsScreen_, "Phone and display", 48, 174, UiTheme::kSecondary,
            &lv_font_montserrat_18);

  lv_obj_t *uploadRow = lv_obj_create(settingsScreen_);
  lv_obj_set_pos(uploadRow, 48, 210);
  lv_obj_set_size(uploadRow, 928, 120);
  lv_obj_set_style_radius(uploadRow, 20, 0);
  lv_obj_set_style_bg_color(uploadRow, lv_color_hex(UiTheme::kAccent), 0);
  lv_obj_set_style_border_width(uploadRow, 0, 0);
  lv_obj_remove_flag(uploadRow, LV_OBJ_FLAG_SCROLLABLE);
  makeLabel(uploadRow, "PHONE PHOTO UPLOAD", 28, 22, UiTheme::kPrimary,
            &lv_font_montserrat_18);
  makeLabel(uploadRow, "Wi-Fi starts only when requested", 28, 62, UiTheme::kPrimary,
            &lv_font_montserrat_16);
  lv_obj_t *upload = makeRoundedButton(uploadRow, 700, 30, 180, 58,
                                       UiTheme::kSurface, "START UPLOAD");
  lv_obj_add_event_cb(upload, onUploadPressed, LV_EVENT_CLICKED, this);

  lv_obj_t *ambientRow = lv_obj_create(settingsScreen_);
  lv_obj_set_pos(ambientRow, 48, 350);
  lv_obj_set_size(ambientRow, 450, 120);
  lv_obj_set_style_radius(ambientRow, 20, 0);
  lv_obj_set_style_bg_color(ambientRow, lv_color_hex(UiTheme::kSurface), 0);
  lv_obj_set_style_border_width(ambientRow, 0, 0);
  lv_obj_remove_flag(ambientRow, LV_OBJ_FLAG_SCROLLABLE);
  makeLabel(ambientRow, "AMBIENT AFTER", 28, 20, UiTheme::kPrimary,
            &lv_font_montserrat_18);
  makeLabel(ambientRow, "60 seconds without touch", 28, 58, UiTheme::kSecondary,
            &lv_font_montserrat_18);
  makeLabel(ambientRow, "ON", 366, 43, UiTheme::kPrimary, &lv_font_montserrat_18);

  lv_obj_t *nightRow = lv_obj_create(settingsScreen_);
  lv_obj_set_pos(nightRow, 522, 350);
  lv_obj_set_size(nightRow, 454, 120);
  lv_obj_set_style_radius(nightRow, 20, 0);
  lv_obj_set_style_bg_color(nightRow, lv_color_hex(UiTheme::kSurface), 0);
  lv_obj_set_style_border_width(nightRow, 0, 0);
  lv_obj_remove_flag(nightRow, LV_OBJ_FLAG_SCROLLABLE);
  makeLabel(nightRow, "NIGHT SCREEN OFF", 28, 20, UiTheme::kPrimary,
            &lv_font_montserrat_18);
  makeLabel(nightRow, "23:00  -  07:00", 28, 58, UiTheme::kSecondary,
            &lv_font_montserrat_18);
  makeLabel(nightRow, "NEXT", 354, 43, UiTheme::kSecondary, &lv_font_montserrat_16);

  makeLabel(settingsScreen_, "AUTO-AMBIENT IS PAUSED ON THIS SCREEN", 48, 542,
            UiTheme::kSecondary, &lv_font_montserrat_18);
}

void UiController::createAmbientScreen() {
  ambientScreen_ = lv_obj_create(nullptr);
  prepareScreen(ambientScreen_, UiTheme::kAmbientTop);

  ambientImage_ = lv_image_create(ambientScreen_);
  lv_obj_set_pos(ambientImage_, 0, 0);
  lv_obj_add_flag(ambientImage_, LV_OBJ_FLAG_HIDDEN);
  ambientModeLabel_ = makeLabel(ambientScreen_, "DARK DISPLAY TEST", 48, 42,
                                UiTheme::kWhite, &lv_font_montserrat_18);
  ambientTitle_ = makeLabel(ambientScreen_, "A quiet moment at home", 48, 420,
                            UiTheme::kWhite, &lv_font_montserrat_32);
  ambientHint_ = makeLabel(ambientScreen_, "Tap anywhere to open Shi Yu", 48, 478,
                           0xDCE8DE, &lv_font_montserrat_18);

  lv_obj_t *touchLayer = lv_obj_create(ambientScreen_);
  lv_obj_set_size(touchLayer, LV_PCT(100), LV_PCT(100));
  lv_obj_set_pos(touchLayer, 0, 0);
  lv_obj_set_style_bg_opa(touchLayer, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(touchLayer, 0, 0);
  lv_obj_set_style_pad_all(touchLayer, 0, 0);
  lv_obj_remove_flag(touchLayer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(touchLayer, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(touchLayer, onAmbientPressed, LV_EVENT_CLICKED, this);
}

void UiController::createUploadScreen() {
  uploadScreen_ = lv_obj_create(nullptr);
  prepareScreen(uploadScreen_, UiTheme::kBackground);
  makeLabel(uploadScreen_, "PHONE PHOTO UPLOAD", 48, 42, UiTheme::kSecondary,
            &lv_font_montserrat_18);
  makeLabel(uploadScreen_, "Connect your phone", 48, 96, UiTheme::kPrimary,
            &lv_font_montserrat_32);
  makeLabel(uploadScreen_, "Wi-Fi", 48, 180, UiTheme::kSecondary, &lv_font_montserrat_18);
  makeLabel(uploadScreen_, "ShiYu-Screen-A02", 48, 218, UiTheme::kPrimary,
            &lv_font_montserrat_24);
  makeLabel(uploadScreen_, "Password", 48, 276, UiTheme::kSecondary,
            &lv_font_montserrat_18);
  makeLabel(uploadScreen_, "shiyu2026", 48, 314, UiTheme::kPrimary,
            &lv_font_montserrat_24);
  makeLabel(uploadScreen_, "Open  http://192.168.4.1", 48, 386, UiTheme::kPrimary,
            &lv_font_montserrat_24);
  uploadStatus_ = makeLabel(uploadScreen_, "Waiting for phone", 560, 190,
                            UiTheme::kPrimary, &lv_font_montserrat_24);
  uploadProgress_ = lv_bar_create(uploadScreen_);
  lv_obj_set_pos(uploadProgress_, 560, 248);
  lv_obj_set_size(uploadProgress_, 380, 28);
  lv_bar_set_range(uploadProgress_, 0, 100);
  lv_bar_set_value(uploadProgress_, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(uploadProgress_, lv_color_hex(UiTheme::kSurface), LV_PART_MAIN);
  lv_obj_set_style_bg_color(uploadProgress_, lv_color_hex(UiTheme::kAccent), LV_PART_INDICATOR);
  lv_obj_t *exit = makeRoundedButton(uploadScreen_, 760, 500, 216, 62,
                                     UiTheme::kSurface, "CLOSE UPLOAD");
  lv_obj_add_event_cb(exit, onUploadExitPressed, LV_EVENT_CLICKED, this);
}

void UiController::showUploadWaiting(const char *ssid, const char *password) {
  LV_UNUSED(ssid);
  LV_UNUSED(password);
  mode_ = AppMode::Active;
  activeScreen_ = ActiveScreen::Upload;
  lv_label_set_text(uploadStatus_, "Waiting for phone");
  lv_bar_set_value(uploadProgress_, 0, LV_ANIM_OFF);
  lv_screen_load(uploadScreen_);
}

void UiController::showUploadProgress(uint8_t percent) {
  char text[40];
  snprintf(text, sizeof(text), "Receiving photo  %u%%", percent);
  lv_label_set_text(uploadStatus_, text);
  lv_bar_set_value(uploadProgress_, percent, LV_ANIM_OFF);
}

void UiController::showUploadSaved() {
  lv_label_set_text(uploadStatus_, "Photo saved");
  lv_bar_set_value(uploadProgress_, 100, LV_ANIM_OFF);
}

void UiController::showUploadError() {
  lv_label_set_text(uploadStatus_, "Upload failed - try again");
}

void UiController::closeUpload() { showSettings(); }

void UiController::setAmbientPhoto(const lv_image_dsc_t *photo) {
  if (!photo) return;
  lv_image_set_src(ambientImage_, photo);
  lv_obj_invalidate(ambientImage_);
  lv_obj_remove_flag(ambientImage_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ambientModeLabel_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ambientTitle_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ambientHint_, LV_OBJ_FLAG_HIDDEN);
}

void UiController::showHome() {
  mode_ = AppMode::Active;
  activeScreen_ = ActiveScreen::Home;
  lv_screen_load(homeScreen_);
  lv_display_trigger_activity(nullptr);
  Serial.println("UI state: ACTIVE/HOME");
}

void UiController::showSettings() {
  mode_ = AppMode::Active;
  activeScreen_ = ActiveScreen::Settings;
  lv_screen_load(settingsScreen_);
  lv_display_trigger_activity(nullptr);
  Serial.println("UI state: ACTIVE/SETTINGS");
}

void UiController::showAmbient() {
  mode_ = AppMode::Ambient;
  lv_screen_load(ambientScreen_);
  Serial.println("UI state: AMBIENT");
}

void UiController::checkAutomaticState() {
  if (mode_ != AppMode::Active || activeScreen_ != ActiveScreen::Home) return;
  if (lv_display_get_inactive_time(nullptr) >= UiTheme::kAmbientTimeoutMs) showAmbient();
}

void UiController::onSettingsPressed(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  static_cast<UiController *>(lv_event_get_user_data(event))->showSettings();
}

void UiController::onBackPressed(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  static_cast<UiController *>(lv_event_get_user_data(event))->showHome();
}

void UiController::onAmbientPressed(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  static_cast<UiController *>(lv_event_get_user_data(event))->showHome();
}

void UiController::onUploadPressed(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  UiController *controller = static_cast<UiController *>(lv_event_get_user_data(event));
  controller->showUploadWaiting(nullptr, nullptr);
  if (controller->uploadRequested_) controller->uploadRequested_(controller->callbackContext_);
}

void UiController::onUploadExitPressed(lv_event_t *event) {
  if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
  UiController *controller = static_cast<UiController *>(lv_event_get_user_data(event));
  if (controller->uploadExitRequested_) controller->uploadExitRequested_(controller->callbackContext_);
}

void UiController::onStateTimer(lv_timer_t *timer) {
  static_cast<UiController *>(lv_timer_get_user_data(timer))->checkAutomaticState();
}

