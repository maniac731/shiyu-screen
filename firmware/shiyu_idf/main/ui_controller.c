#include "ui_controller.h"
#include "cloud_sync.h"
#include "photo_storage.h"
#include "carousel_settings.h"
#include "inventory_model.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "draw/lv_draw.h"
#include "lvgl.h"
#include "misc/cache/instance/lv_image_cache.h"
#include "fonts/shiyu_fonts.h"
#include "ui_theme.h"

#if !LV_FONT_MONTSERRAT_24
#error "LV_FONT_MONTSERRAT_24 is required for keyboard symbols"
#endif

static lv_font_t s_ui_font_body;
static uint32_t s_carousel_tick;
static lv_obj_t *s_carousel_screen,*s_carousel_value,*s_carousel_status;
static unsigned s_carousel_draft;
static bool s_carousel_save_requested;

#define UI_FONT_BODY (&s_ui_font_body)
#define UI_FONT_TITLE (&shiyu_font_28)
#define SHIYU_KB_BTN(width) (LV_BUTTONMATRIX_CTRL_POPOVER | (width))

static const char *const SHIYU_KB_MAP_LOWER[] = {
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", LV_SYMBOL_BACKSPACE, "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", "\n",
    "Shift", "z", "x", "c", "v", "b", "n", "m", ".", "-", "\n",
    "123", " ", "连接", ""
};

static const char *const SHIYU_KB_MAP_UPPER[] = {
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", LV_SYMBOL_BACKSPACE, "\n",
    "A", "S", "D", "F", "G", "H", "J", "K", "L", "\n",
    "Shift", "Z", "X", "C", "V", "B", "N", "M", ".", "-", "\n",
    "123", " ", "连接", ""
};

static const char *const SHIYU_KB_MAP_SPECIAL[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", LV_SYMBOL_BACKSPACE, "\n",
    "!", "@", "#", "$", "%", "^", "&", "*", "(", ")", "\n",
    "-", "_", "=", "+", "[", "]", "{", "}", "\\", "|", "\n",
    ";", ":", "'", "\"", ",", ".", "<", ">", "/", "?", "`", "~", "\n",
    "ABC", " ", "连接", ""
};

static const char SHIYU_WIFI_ASCII_PUNCTUATION[] =
    "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";

static const lv_buttonmatrix_ctrl_t SHIYU_KB_CTRL_TEXT[] = {
    SHIYU_KB_BTN(4), SHIYU_KB_BTN(4), SHIYU_KB_BTN(4), SHIYU_KB_BTN(4), SHIYU_KB_BTN(4), SHIYU_KB_BTN(4), SHIYU_KB_BTN(4), SHIYU_KB_BTN(4), SHIYU_KB_BTN(4), SHIYU_KB_BTN(4), LV_BUTTONMATRIX_CTRL_CHECKED | 7,
    SHIYU_KB_BTN(4), SHIYU_KB_BTN(4), SHIYU_KB_BTN(4), SHIYU_KB_BTN(4), SHIYU_KB_BTN(4), SHIYU_KB_BTN(4), SHIYU_KB_BTN(4), SHIYU_KB_BTN(4), SHIYU_KB_BTN(4),
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 6, SHIYU_KB_BTN(3), SHIYU_KB_BTN(3), SHIYU_KB_BTN(3), SHIYU_KB_BTN(3), SHIYU_KB_BTN(3), SHIYU_KB_BTN(3), SHIYU_KB_BTN(3), LV_BUTTONMATRIX_CTRL_CHECKED | 3, LV_BUTTONMATRIX_CTRL_CHECKED | 3,
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 3, 12, LV_KEYBOARD_CTRL_BUTTON_FLAGS | 4
};

static const lv_buttonmatrix_ctrl_t SHIYU_KB_CTRL_SPECIAL[] = {
    SHIYU_KB_BTN(1), SHIYU_KB_BTN(1), SHIYU_KB_BTN(1), SHIYU_KB_BTN(1), SHIYU_KB_BTN(1), SHIYU_KB_BTN(1), SHIYU_KB_BTN(1), SHIYU_KB_BTN(1), SHIYU_KB_BTN(1), SHIYU_KB_BTN(1), LV_BUTTONMATRIX_CTRL_CHECKED | 2,
    SHIYU_KB_BTN(1), SHIYU_KB_BTN(1), SHIYU_KB_BTN(1), SHIYU_KB_BTN(1), SHIYU_KB_BTN(1), SHIYU_KB_BTN(1), SHIYU_KB_BTN(1), SHIYU_KB_BTN(1), SHIYU_KB_BTN(1), SHIYU_KB_BTN(1),
    SHIYU_KB_BTN(1), SHIYU_KB_BTN(1), SHIYU_KB_BTN(1), SHIYU_KB_BTN(1), SHIYU_KB_BTN(1), SHIYU_KB_BTN(1), SHIYU_KB_BTN(1), SHIYU_KB_BTN(1), SHIYU_KB_BTN(1), SHIYU_KB_BTN(1),
    SHIYU_KB_BTN(1), SHIYU_KB_BTN(1), SHIYU_KB_BTN(1), SHIYU_KB_BTN(1), SHIYU_KB_BTN(1), SHIYU_KB_BTN(1), SHIYU_KB_BTN(1), SHIYU_KB_BTN(1), SHIYU_KB_BTN(1), SHIYU_KB_BTN(1), SHIYU_KB_BTN(1), SHIYU_KB_BTN(1),
    LV_KEYBOARD_CTRL_BUTTON_FLAGS | 3, 12, LV_KEYBOARD_CTRL_BUTTON_FLAGS | 4
};

_Static_assert(sizeof(SHIYU_KB_MAP_LOWER) / sizeof(SHIYU_KB_MAP_LOWER[0]) == 37,
               "Lower keyboard map must contain 33 buttons and row markers");
_Static_assert(sizeof(SHIYU_KB_MAP_UPPER) / sizeof(SHIYU_KB_MAP_UPPER[0]) == 37,
               "Upper keyboard map must contain 33 buttons and row markers");
_Static_assert(sizeof(SHIYU_KB_MAP_SPECIAL) / sizeof(SHIYU_KB_MAP_SPECIAL[0]) == 51,
               "Special keyboard map must contain 46 buttons and row markers");
_Static_assert(sizeof(SHIYU_KB_CTRL_TEXT) / sizeof(SHIYU_KB_CTRL_TEXT[0]) == 33,
               "Text keyboard control map must match button count");
_Static_assert(sizeof(SHIYU_KB_CTRL_SPECIAL) / sizeof(SHIYU_KB_CTRL_SPECIAL[0]) == 46,
               "Special keyboard control map must match button count");
_Static_assert(sizeof(SHIYU_WIFI_ASCII_PUNCTUATION) == 33,
               "Printable ASCII must contain exactly 32 punctuation characters");

typedef enum { MODE_ACTIVE, MODE_AMBIENT } app_mode_t;
typedef enum {
    SCREEN_HOME,
    SCREEN_INVENTORY,
    SCREEN_FOOD_DETAIL,
    SCREEN_MEAL,
    SCREEN_SETTINGS,
    SCREEN_NETWORK,
} active_screen_t;
typedef struct {
    app_mode_t mode;
    active_screen_t active_screen;
    lv_obj_t *home;
    lv_obj_t *inventory;
    lv_obj_t *food_detail;
    lv_obj_t *meal;
    lv_obj_t *settings;
    lv_obj_t *ambient;
    lv_obj_t *network;
    lv_obj_t *inventory_context;
    lv_obj_t *inventory_milk;
    lv_obj_t *food_detail_name;
    lv_obj_t *food_detail_quantity;
    lv_obj_t *food_detail_status;
    lv_obj_t *ambient_image;
    lv_obj_t *ambient_title;
    lv_obj_t *ambient_hint;
    lv_obj_t *settings_network_status;
    lv_obj_t *network_status;
    lv_obj_t *network_ssid;
    lv_obj_t *network_password;
    lv_obj_t *network_textarea;
    wifi_manager_scan_item_t network_choices[10];
    size_t network_choice_count;
    size_t selected_network;
    bool inventory_expiry_only;
    shiyu_ui_action_cb_t start_network_scan;
    shiyu_ui_action_cb_t start_network_setup;
    shiyu_ui_action_cb_t stop_network_setup;
    shiyu_ui_action_cb_t retry_network;
    shiyu_ui_action_cb_t forget_network;
    shiyu_ui_connect_cb_t connect_network;
} ui_state_t;

static const char *TAG = "shiyu_ui";
static ui_state_t s_ui;
static lv_obj_t *s_stock_count, *s_stock_list, *s_stock_page_label;
static inventory_row_t *s_stock_rows;
static unsigned s_stock_page, s_stock_rows_count;
static char s_stock_selected[37];
static void refresh_inventory_ui(bool force);

static bool special_keyboard_contains(char character)
{
    for (size_t index = 0;
         index < sizeof(SHIYU_KB_MAP_SPECIAL) / sizeof(SHIYU_KB_MAP_SPECIAL[0]);
         ++index) {
        const char *button = SHIYU_KB_MAP_SPECIAL[index];
        if (button[0] == character && button[1] == '\0') return true;
    }
    return false;
}

static void initialize_ui_fonts(void)
{
    s_ui_font_body = shiyu_font_cjk_24;
    s_ui_font_body.fallback = &lv_font_montserrat_24;

    static const struct {
        const char *name;
        uint32_t codepoint;
    } keyboard_symbols[] = {
        {"left", 0xF053},
        {"eye-open", 0xF06E},
        {"eye-close", 0xF070},
        {"backspace", 0xF55A},
    };
    bool symbols_ready = true;
    for (size_t index = 0; index < sizeof(keyboard_symbols) / sizeof(keyboard_symbols[0]); ++index) {
        lv_font_glyph_dsc_t glyph;
        if (!lv_font_get_glyph_dsc(UI_FONT_BODY, &glyph,
                                   keyboard_symbols[index].codepoint, 0) ||
            glyph.resolved_font != &lv_font_montserrat_24) {
            symbols_ready = false;
            ESP_LOGE(TAG, "Missing keyboard symbol %s U+%04lX",
                     keyboard_symbols[index].name,
                     (unsigned long)keyboard_symbols[index].codepoint);
        }
    }
    if (symbols_ready) {
        ESP_LOGI(TAG, "Keyboard symbol fallback ready (Montserrat 24)");
    }

    bool punctuation_ready = true;
    for (size_t index = 0; SHIYU_WIFI_ASCII_PUNCTUATION[index] != '\0'; ++index) {
        char character = SHIYU_WIFI_ASCII_PUNCTUATION[index];
        if (!special_keyboard_contains(character)) {
            punctuation_ready = false;
            ESP_LOGE(TAG, "Missing Wi-Fi ASCII punctuation 0x%02X",
                     (unsigned int)(unsigned char)character);
        }
    }
    if (punctuation_ready) {
        ESP_LOGI(TAG, "Wi-Fi keyboard covers all 32 printable ASCII punctuation characters");
    }
}

static void create_inventory(void);
static void create_food_detail(void);
static void create_meal(void);
static void create_network(void);
static void show_network_scan_view(const char *message);
static void show_network_password_view(void);
static void show_network_connecting_view(const char *message);
static void show_inventory(bool expiry_only);
static const char *get_active_screen_name(active_screen_t screen);

static void log_memory(const char *stage)
{
    ESP_LOGI(TAG, "Memory %s: internal=%u largest=%u psram=%u psram_largest=%u", stage,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

static void log_layout_geometry(const char *name, lv_obj_t *object)
{
    if (object == NULL) return;
    lv_area_t area;
    lv_obj_get_coords(object, &area);
    ESP_LOGI(TAG, "network-password-layout-v5 %s x=%ld y=%ld w=%ld h=%ld",
             name, (long)area.x1, (long)area.y1,
             (long)lv_area_get_width(&area), (long)lv_area_get_height(&area));
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, int x, int y,
                            uint32_t color, const lv_font_t *font)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_pos(label, x, y);
    return label;
}

static void prepare_screen(lv_obj_t *screen, uint32_t background)
{
    lv_obj_set_style_bg_color(screen, lv_color_hex(background), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *make_button(lv_obj_t *parent, int x, int y, int width, int height,
                             uint32_t background, const char *text)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_radius(button, height / 2, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(background), 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_t *label = make_label(button, text, 0, 0, SHIYU_COLOR_PRIMARY, UI_FONT_BODY);
    lv_obj_center(label);
    return button;
}

static lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int width, int height,
                           uint32_t background)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, width, height);
    lv_obj_set_style_radius(card, 24, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(background), 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static lv_obj_t *get_active_screen(active_screen_t screen)
{
    switch (screen) {
        case SCREEN_HOME:
            return s_ui.home;
        case SCREEN_INVENTORY:
            return s_ui.inventory;
        case SCREEN_FOOD_DETAIL:
            return s_ui.food_detail;
        case SCREEN_MEAL:
            return s_ui.meal;
        case SCREEN_SETTINGS:
            return s_ui.settings;
        case SCREEN_NETWORK:
            return s_ui.network;
        default:
            return s_ui.home;
    }
}

static void ensure_active_screen(active_screen_t screen)
{
    switch (screen) {
        case SCREEN_INVENTORY:
            if (s_ui.inventory == NULL) create_inventory();
            break;
        case SCREEN_FOOD_DETAIL:
            if (s_ui.food_detail == NULL) create_food_detail();
            break;
        case SCREEN_MEAL:
            if (s_ui.meal == NULL) create_meal();
            break;
        case SCREEN_NETWORK:
            if (s_ui.network == NULL) create_network();
            break;
        default:
            break;
    }
}

static void release_transient_screen(active_screen_t screen)
{
    lv_obj_t *object = NULL;
    switch (screen) {
        case SCREEN_INVENTORY:
            object = s_ui.inventory;
            s_ui.inventory = NULL;
            s_ui.inventory_context = NULL;
            s_ui.inventory_milk = NULL;
            s_stock_list = NULL; s_stock_page_label = NULL;
            break;
        case SCREEN_FOOD_DETAIL:
            object = s_ui.food_detail;
            s_ui.food_detail = NULL;
            s_ui.food_detail_name = NULL;
            s_ui.food_detail_quantity = NULL;
            s_ui.food_detail_status = NULL;
            break;
        case SCREEN_MEAL:
            object = s_ui.meal;
            s_ui.meal = NULL;
            break;
        case SCREEN_NETWORK:
            object = s_ui.network;
            if (s_ui.network_textarea != NULL) {
                lv_textarea_set_text(s_ui.network_textarea, "");
            }
            s_ui.network = NULL;
            s_ui.network_status = NULL;
            s_ui.network_ssid = NULL;
            s_ui.network_password = NULL;
            s_ui.network_textarea = NULL;
            memset(s_ui.network_choices, 0, sizeof(s_ui.network_choices));
            s_ui.network_choice_count = 0;
            break;
        default:
            return;
    }
    if (object != NULL) {
        /* Event callbacks can navigate away from their own screen, so delete later. */
        lv_obj_delete_async(object);
        ESP_LOGI(TAG, "Release ACTIVE/%s", get_active_screen_name(screen));
        log_memory("transient-release-scheduled");
    }
}

static const char *get_active_screen_name(active_screen_t screen)
{
    switch (screen) {
        case SCREEN_HOME:
            return "HOME";
        case SCREEN_INVENTORY:
            return "INVENTORY";
        case SCREEN_FOOD_DETAIL:
            return "FOOD_DETAIL";
        case SCREEN_MEAL:
            return "MEAL";
        case SCREEN_SETTINGS:
            return "SETTINGS";
        case SCREEN_NETWORK:
            return "NETWORK";
        default:
            return "UNKNOWN";
    }
}

static bool active_screen_allows_ambient(active_screen_t screen)
{
    return screen == SCREEN_HOME || screen == SCREEN_INVENTORY ||
           screen == SCREEN_FOOD_DETAIL || screen == SCREEN_MEAL;
}

static void show_active(active_screen_t screen)
{
    app_mode_t previous_mode = s_ui.mode;
    active_screen_t previous_screen = s_ui.active_screen;
    ensure_active_screen(screen);
    s_ui.mode = MODE_ACTIVE;
    s_ui.active_screen = screen;
    lv_screen_load(get_active_screen(screen));
    lv_display_trigger_activity(NULL);
    ESP_LOGI(TAG, "State ACTIVE/%s", get_active_screen_name(screen));
    log_memory("active-screen-loaded");
    if (previous_mode == MODE_ACTIVE && previous_screen != screen) {
        release_transient_screen(previous_screen);
    }
}

static void show_ambient(void)
{
    app_mode_t previous_mode = s_ui.mode;
    active_screen_t previous_screen = s_ui.active_screen;
    log_memory("before-ambient-load");
    s_ui.mode = MODE_AMBIENT;
    s_carousel_tick=lv_tick_get();
    lv_screen_load(s_ui.ambient);
    ESP_LOGI(TAG, "State AMBIENT");
    if (previous_mode == MODE_ACTIVE) release_transient_screen(previous_screen);
    log_memory("after-ambient-load");
}

static void on_settings(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) show_active(SCREEN_SETTINGS);
}

static void on_back_home(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) show_active(SCREEN_HOME);
}

static void on_back_inventory(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        show_inventory(s_ui.inventory_expiry_only);
    }
}

static void show_inventory(bool expiry_only)
{
    (void)expiry_only;
    s_ui.inventory_expiry_only = false;
    ensure_active_screen(SCREEN_INVENTORY);
    refresh_inventory_ui(true);
    show_active(SCREEN_INVENTORY);
}

static void on_inventory(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) show_inventory(false);
}

static void on_food_detail(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    unsigned index=(unsigned)(uintptr_t)lv_event_get_user_data(event);
    if (!s_stock_rows || index>=s_stock_rows_count) return;
    strlcpy(s_stock_selected,s_stock_rows[index].id,sizeof(s_stock_selected));
    ensure_active_screen(SCREEN_FOOD_DETAIL);
    refresh_inventory_ui(true);
    show_active(SCREEN_FOOD_DETAIL);
}

static void on_meal(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) show_active(SCREEN_MEAL);
}

static void on_ambient(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) show_active(SCREEN_HOME);
}

static void on_network_setup(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    ensure_active_screen(SCREEN_NETWORK);
    show_network_scan_view("正在扫描附近网络");
    show_active(SCREEN_NETWORK);
    if (s_ui.start_network_scan != NULL) s_ui.start_network_scan();
}

static void on_network_close(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    if (s_ui.stop_network_setup != NULL) s_ui.stop_network_setup();
    show_active(SCREEN_SETTINGS);
}

static void on_network_refresh(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    memset(s_ui.network_choices, 0, sizeof(s_ui.network_choices));
    s_ui.network_choice_count = 0;
    show_network_scan_view("正在重新扫描");
    if (s_ui.start_network_scan != NULL) s_ui.start_network_scan();
}

static void on_network_choice(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    size_t index = (size_t)(uintptr_t)lv_event_get_user_data(event);
    if (index >= s_ui.network_choice_count) return;
    s_ui.selected_network = index;
    if (s_ui.network_choices[index].auth == WIFI_MANAGER_AUTH_UNSUPPORTED) {
        if (s_ui.network_status != NULL) {
            lv_label_set_text(s_ui.network_status,
                              "暂不支持 WEP 或企业认证网络");
        }
        return;
    }
    show_network_password_view();
}

static void submit_selected_network(const char *password)
{
    if (s_ui.selected_network >= s_ui.network_choice_count ||
        s_ui.connect_network == NULL) return;
    const wifi_manager_scan_item_t *choice =
        &s_ui.network_choices[s_ui.selected_network];
    s_ui.connect_network(choice->ssid, choice->ssid_len, password);
    show_network_connecting_view("正在连接网络");
}

static void on_open_network_connect(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) submit_selected_network("");
}

static void reset_password_field(void)
{
    if (s_ui.network_textarea == NULL) return;
    lv_textarea_set_password_mode(s_ui.network_textarea, true);
    lv_textarea_set_password_show_time(s_ui.network_textarea, 0);
    lv_textarea_set_text(s_ui.network_textarea, "");
}

static void return_to_network_list(void)
{
    reset_password_field();
    show_network_scan_view("请选择网络");
}

static void submit_password_field(void)
{
    if (s_ui.network_textarea == NULL) return;
    const char *password = lv_textarea_get_text(s_ui.network_textarea);
    size_t length = strlen(password);
    if (length < 8 || length > 63) {
        lv_textarea_set_placeholder_text(s_ui.network_textarea,
                                         "密码需要 8-63 个字符");
        return;
    }
    char password_copy[64] = {0};
    strlcpy(password_copy, password, sizeof(password_copy));
    reset_password_field();
    submit_selected_network(password_copy);
    memset(password_copy, 0, sizeof(password_copy));
}

static void on_password_keyboard_value_changed(lv_event_t *event)
{
    lv_obj_t *keyboard = lv_event_get_target_obj(event);
    uint32_t button = lv_keyboard_get_selected_button(keyboard);
    const char *text = lv_keyboard_get_button_text(keyboard, button);
    if (text != NULL && strcmp(text, "连接") == 0) {
        submit_password_field();
        return;
    }
    if (text != NULL && strcmp(text, "123") == 0) {
        lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_SPECIAL);
        return;
    }
    if (text != NULL && strcmp(text, "ABC") == 0) {
        lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
        return;
    }
    if (text != NULL && strcmp(text, "Shift") == 0) {
        lv_keyboard_mode_t mode = lv_keyboard_get_mode(keyboard);
        lv_keyboard_set_mode(keyboard,
                             mode == LV_KEYBOARD_MODE_TEXT_UPPER
                                 ? LV_KEYBOARD_MODE_TEXT_LOWER
                                 : LV_KEYBOARD_MODE_TEXT_UPPER);
        return;
    }
    lv_keyboard_def_event_cb(event);
}

static void on_password_eye(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED ||
        s_ui.network_textarea == NULL) return;
    bool was_hidden = lv_textarea_get_password_mode(s_ui.network_textarea);
    lv_textarea_set_password_mode(s_ui.network_textarea, !was_hidden);
    lv_textarea_set_password_show_time(s_ui.network_textarea, 0);
    lv_obj_t *label = lv_obj_get_child(lv_event_get_target_obj(event), 0);
    if (label != NULL) {
        lv_label_set_text(label, was_hidden ? LV_SYMBOL_EYE_CLOSE
                                           : LV_SYMBOL_EYE_OPEN);
    }
}

static void on_password_keyboard(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_CANCEL) {
        return_to_network_list();
        return;
    }
    if (code == LV_EVENT_READY) submit_password_field();
}

static void on_password_back(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    return_to_network_list();
}

static void on_state_timer(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    refresh_inventory_ui(false);
    if(s_carousel_status){int result=s_carousel_save_requested?carousel_settings_result():0;lv_label_set_text(s_carousel_status,result==2?"Saving...":result==1?"Saved":result<0?"Save failed - old value kept":"5-300 seconds; tap Save");}
    if(s_ui.mode==MODE_AMBIENT&&photo_storage_count()>=2&&lv_tick_elaps(s_carousel_tick)>=carousel_settings_seconds()*1000){char path[64];if(photo_storage_snapshot(path,sizeof(path),true)) {
        shiyu_ui_set_ambient_photo(path);
        ESP_LOGI(TAG, "Carousel advanced: %u photos, %us", photo_storage_count(), carousel_settings_seconds());
    }s_carousel_tick=lv_tick_get();}
    if (s_ui.mode == MODE_ACTIVE && active_screen_allows_ambient(s_ui.active_screen) &&
        lv_display_get_inactive_time(NULL) >= SHIYU_AMBIENT_TIMEOUT_MS) {
        show_ambient();
    }
}

static void create_home(void)
{
    s_ui.home = lv_obj_create(NULL);
    prepare_screen(s_ui.home, SHIYU_COLOR_BACKGROUND);
    make_label(s_ui.home, "食愈", 48, 36, SHIYU_COLOR_SECONDARY, UI_FONT_TITLE);
    make_label(s_ui.home, "一眼看懂家里的食材", 48, 79, SHIYU_COLOR_PRIMARY, UI_FONT_BODY);
    make_label(s_ui.home, "", 680, 79,
               SHIYU_COLOR_SECONDARY, UI_FONT_BODY);
    lv_obj_t *settings = make_button(s_ui.home, 852, 32, 132, 56,
                                     SHIYU_COLOR_SURFACE, "设置");
    lv_obj_add_event_cb(settings, on_settings, LV_EVENT_CLICKED, NULL);

    lv_obj_t *meal = make_card(s_ui.home, 48, 150, 574, 330, SHIYU_COLOR_SURFACE);
    lv_obj_add_flag(meal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(meal, on_meal, LV_EVENT_CLICKED, NULL);
    make_label(meal, "今晚吃什么", 32, 28, SHIYU_COLOR_SECONDARY, UI_FONT_BODY);
    make_label(meal, "番茄炒蛋", 32, 84, SHIYU_COLOR_PRIMARY, UI_FONT_TITLE);
    make_label(meal, "配一碗热米饭", 32, 125, SHIYU_COLOR_PRIMARY, UI_FONT_BODY);
    make_label(meal, "[DEMO] 示例菜单，未接入库存匹配", 32, 198, SHIYU_COLOR_SECONDARY, UI_FONT_BODY);
    lv_obj_t *meal_button = make_button(meal, 32, 252, 220, 46,
                                        SHIYU_COLOR_ACCENT, "查看建议");
    lv_obj_add_event_cb(meal_button, on_meal, LV_EVENT_CLICKED, NULL);

    lv_obj_t *expiry = make_card(s_ui.home, 650, 150, 326, 214, SHIYU_COLOR_WARNING);

    make_label(expiry, "临期提醒", 28, 26, SHIYU_COLOR_SECONDARY, UI_FONT_BODY);
    make_label(expiry, "未开启", 28, 76, SHIYU_COLOR_PRIMARY, UI_FONT_TITLE);
    make_label(expiry, "本阶段只管理数量", 28, 138, SHIYU_COLOR_PRIMARY, UI_FONT_BODY);

    lv_obj_t *inventory = make_card(s_ui.home, 650, 388, 326, 92, SHIYU_COLOR_ACCENT);
    lv_obj_add_flag(inventory, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(inventory, on_inventory, LV_EVENT_CLICKED, NULL);
    s_stock_count = make_label(inventory, "库存尚未同步", 24, 32, SHIYU_COLOR_PRIMARY, UI_FONT_BODY);
    make_label(s_ui.home, "60 秒无操作后进入环境模式", 48, 542,
               SHIYU_COLOR_SECONDARY, UI_FONT_BODY);
}

static void stock_page_change(lv_event_t *event)
{
    int step=(int)(intptr_t)lv_event_get_user_data(event);
    unsigned count, serial; bool online;
    inventory_model_meta(&count,&online,&serial);
    unsigned pages=(count+3)/4;
    if(step<0 && s_stock_page) --s_stock_page;
    if(step>0 && s_stock_page+1<pages) ++s_stock_page;
    refresh_inventory_ui(true);
}

static void stock_quantity(char *out, size_t capacity, const inventory_row_t *row)
{
    char number[16]; strlcpy(number,row->quantity,sizeof(number));
    if(strchr(number,'.')) {
        size_t n=strlen(number);
        while(n && number[n-1]=='0') number[--n]=0;
        if(n && number[n-1]=='.') number[--n]=0;
    }
    snprintf(out,capacity,"%s %s",number,row->unit);
}

static void refresh_inventory_ui(bool force)
{
    static unsigned previous=~0U; static bool was_online, was_known;
    unsigned count, serial; bool online;
    bool known=inventory_model_meta(&count,&online,&serial);
    if(!force && previous==serial && was_online==online && was_known==known) return;
    previous=serial; was_online=online; was_known=known;
    if(s_stock_count) {
        if(known) lv_label_set_text_fmt(s_stock_count,"库存记录 %u 条",count);
        else lv_label_set_text(s_stock_count,"库存尚未同步");
    }
    if(s_ui.inventory && s_stock_list && s_stock_rows) {
        lv_label_set_text(s_ui.inventory_context,!known?"尚未同步，请先联网并绑定厨房":
                          online?"已同步 · 请在手机管理食材":"离线缓存 · 请在手机管理食材");
        if(s_stock_page*4>=count) s_stock_page=0;
        s_stock_rows_count=inventory_model_page(s_stock_page*4,s_stock_rows,4,&count);
        lv_obj_clean(s_stock_list);
        if(!s_stock_rows_count) make_label(s_stock_list,known?"暂无食材，请在手机添加":"等待厨房库存同步",20,32,SHIYU_COLOR_SECONDARY,UI_FONT_BODY);
        for(unsigned i=0;i<s_stock_rows_count;++i) {
            lv_obj_t *card=make_card(s_stock_list,(i%2)*476,(i/2)*128,452,112,SHIYU_COLOR_SURFACE);
            lv_obj_add_flag(card,LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(card,on_food_detail,LV_EVENT_CLICKED,(void *)(uintptr_t)i);
            lv_obj_t *name=make_label(card,s_stock_rows[i].name,20,14,SHIYU_COLOR_PRIMARY,UI_FONT_BODY);
            lv_obj_set_width(name,408); lv_label_set_long_mode(name,LV_LABEL_LONG_DOT);
            char qty[64]; stock_quantity(qty,sizeof(qty),&s_stock_rows[i]);
            make_label(card,qty,20,62,SHIYU_COLOR_SECONDARY,UI_FONT_BODY);
        }
        lv_label_set_text_fmt(s_stock_page_label,"%u / %u",s_stock_page+1,count?(count+3)/4:1);
    }
    if(s_ui.food_detail) {
        inventory_row_t row; char qty[64];
        if(inventory_model_find(s_stock_selected,&row)) {
            lv_label_set_text(s_ui.food_detail_name,row.name);
            stock_quantity(qty,sizeof(qty),&row); lv_label_set_text(s_ui.food_detail_quantity,qty);
            lv_label_set_text(s_ui.food_detail_status,online?"已同步 · 屏幕只读":"离线缓存 · 屏幕只读");
        } else {
            lv_label_set_text(s_ui.food_detail_name,"食材已删除或尚未同步");
            lv_label_set_text(s_ui.food_detail_quantity,""); lv_label_set_text(s_ui.food_detail_status,"");
        }
    }
}

static void create_inventory(void)
{
    s_ui.inventory=lv_obj_create(NULL);
    prepare_screen(s_ui.inventory,SHIYU_COLOR_BACKGROUND);
    lv_obj_t *back=make_button(s_ui.inventory,40,32,132,56,SHIYU_COLOR_SURFACE,"<  返回");
    lv_obj_add_event_cb(back,on_back_home,LV_EVENT_CLICKED,NULL);
    make_label(s_ui.inventory,"家里的食材",48,124,SHIYU_COLOR_PRIMARY,UI_FONT_TITLE);
    s_ui.inventory_context=make_label(s_ui.inventory,"等待同步",48,174,SHIYU_COLOR_SECONDARY,UI_FONT_BODY);
    s_stock_list=lv_obj_create(s_ui.inventory);
    lv_obj_set_pos(s_stock_list,48,218); lv_obj_set_size(s_stock_list,928,258);
    lv_obj_set_style_pad_all(s_stock_list,0,0); lv_obj_set_style_border_width(s_stock_list,0,0);
    lv_obj_set_style_bg_opa(s_stock_list,LV_OPA_TRANSP,0); lv_obj_remove_flag(s_stock_list,LV_OBJ_FLAG_SCROLLABLE);
    if(!s_stock_rows) s_stock_rows=heap_caps_calloc(4,sizeof(*s_stock_rows),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    lv_obj_t *previous=make_button(s_ui.inventory,48,504,160,52,SHIYU_COLOR_SURFACE,"<");
    lv_obj_add_event_cb(previous,stock_page_change,LV_EVENT_CLICKED,(void *)(intptr_t)-1);
    s_stock_page_label=make_label(s_ui.inventory,"1 / 1",460,516,SHIYU_COLOR_SECONDARY,UI_FONT_BODY);
    lv_obj_t *next=make_button(s_ui.inventory,816,504,160,52,SHIYU_COLOR_SURFACE,">");
    lv_obj_add_event_cb(next,stock_page_change,LV_EVENT_CLICKED,(void *)(intptr_t)1);
    refresh_inventory_ui(true);
}

static void create_food_detail(void)
{
    s_ui.food_detail = lv_obj_create(NULL);
    prepare_screen(s_ui.food_detail, SHIYU_COLOR_BACKGROUND);
    lv_obj_t *back = make_button(s_ui.food_detail, 40, 32, 132, 56,
                                 SHIYU_COLOR_SURFACE, "<  返回");
    lv_obj_add_event_cb(back, on_back_inventory, LV_EVENT_CLICKED, NULL);
    make_label(s_ui.food_detail, "家里的食材", 48, 124,
               SHIYU_COLOR_PRIMARY, UI_FONT_TITLE);

    lv_obj_t *detail = make_card(s_ui.food_detail, 48, 194, 928, 240,
                                 SHIYU_COLOR_SURFACE);
    s_ui.food_detail_name = make_label(detail, "", 32, 28,
                                       SHIYU_COLOR_PRIMARY, UI_FONT_BODY);
    lv_obj_set_width(s_ui.food_detail_name,850);
    s_ui.food_detail_quantity = make_label(detail, "", 32, 112,
                                           SHIYU_COLOR_SECONDARY, UI_FONT_BODY);
    s_ui.food_detail_status = make_label(detail, "", 32, 174,
                                         SHIYU_COLOR_PRIMARY, UI_FONT_BODY);
    make_label(detail, "手机管理食材", 636, 174, SHIYU_COLOR_SECONDARY, UI_FONT_BODY);

    lv_obj_t *meal_button = make_button(s_ui.food_detail, 48, 470, 260, 58,
                                        SHIYU_COLOR_ACCENT, "今晚吃什么");
    lv_obj_add_event_cb(meal_button, on_meal, LV_EVENT_CLICKED, NULL);
    make_label(s_ui.food_detail, "60 秒无操作后进入环境模式", 570, 486,
               SHIYU_COLOR_SECONDARY, UI_FONT_BODY);
    ESP_LOGI(TAG, "Created transient FOOD_DETAIL");
    log_memory("food-detail-created");
}

static void create_meal(void)
{
    s_ui.meal = lv_obj_create(NULL);
    prepare_screen(s_ui.meal, SHIYU_COLOR_BACKGROUND);
    lv_obj_t *back = make_button(s_ui.meal, 40, 32, 132, 56,
                                 SHIYU_COLOR_SURFACE, "<  返回");
    lv_obj_add_event_cb(back, on_back_home, LV_EVENT_CLICKED, NULL);
    make_label(s_ui.meal, "今晚吃什么 [DEMO]", 48, 124,
               SHIYU_COLOR_PRIMARY, UI_FONT_TITLE);

    lv_obj_t *recommendation = make_card(s_ui.meal, 48, 194, 560, 286,
                                         SHIYU_COLOR_SURFACE);
    make_label(recommendation, "番茄炒蛋", 32, 28,
               SHIYU_COLOR_PRIMARY, UI_FONT_TITLE);
    make_label(recommendation, "[DEMO] 示例菜单，未接入库存匹配", 32, 86,
               SHIYU_COLOR_SECONDARY, UI_FONT_BODY);
    make_label(recommendation, "配一碗热米饭", 32, 142,
               SHIYU_COLOR_PRIMARY, UI_FONT_BODY);
    make_label(recommendation, "[DEMO]", 32, 220,
               SHIYU_COLOR_SECONDARY, UI_FONT_BODY);

    lv_obj_t *ingredients = make_card(s_ui.meal, 632, 194, 344, 286,
                                      SHIYU_COLOR_ACCENT);
    make_label(ingredients, "所需食材", 28, 28,
               SHIYU_COLOR_PRIMARY, UI_FONT_TITLE);
    make_label(ingredients, "番茄、蛋、米饭", 28, 92,
               SHIYU_COLOR_PRIMARY, UI_FONT_BODY);
    make_label(ingredients, "查看建议", 28, 196,
               SHIYU_COLOR_SECONDARY, UI_FONT_BODY);
    make_label(s_ui.meal, "60 秒无操作后进入环境模式", 48, 542,
               SHIYU_COLOR_SECONDARY, UI_FONT_BODY);
    ESP_LOGI(TAG, "Created transient MEAL");
    log_memory("meal-created");
}

static lv_obj_t *s_cloud_screen, *s_cloud_label, *s_cloud_qr;
static char s_cloud_text[96] = "Waiting for cloud";
static char s_cloud_url[256];
static void on_cloud_back(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    lv_screen_load(s_ui.settings); lv_obj_delete(s_cloud_screen);
    s_cloud_screen = NULL; s_cloud_label = NULL; s_cloud_qr = NULL;
}
void shiyu_ui_set_cloud_state(const char *text, const char *binding_url)
{
    strlcpy(s_cloud_text, text ? text : "", sizeof(s_cloud_text));
    strlcpy(s_cloud_url, binding_url ? binding_url : "", sizeof(s_cloud_url));
    if (s_cloud_label) lv_label_set_text(s_cloud_label, s_cloud_text);
    if (s_cloud_qr) {
        if (s_cloud_url[0]) {
            lv_obj_add_flag(s_cloud_qr, LV_OBJ_FLAG_HIDDEN);
            if (lv_qrcode_update(s_cloud_qr, s_cloud_url, strlen(s_cloud_url)) == LV_RESULT_OK)
                lv_obj_remove_flag(s_cloud_qr, LV_OBJ_FLAG_HIDDEN);
        } else lv_obj_add_flag(s_cloud_qr, LV_OBJ_FLAG_HIDDEN);
    }
}
static void on_cloud_open(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED || s_cloud_screen) return;
    s_cloud_screen = lv_obj_create(NULL); prepare_screen(s_cloud_screen, SHIYU_COLOR_BACKGROUND);
    lv_obj_t *back = make_button(s_cloud_screen, 48, 24, 52, 52, SHIYU_COLOR_SURFACE, LV_SYMBOL_LEFT);
    lv_obj_add_event_cb(back, on_cloud_back, LV_EVENT_CLICKED, NULL);
    make_label(s_cloud_screen, "Cloud / Bind screen", 120, 32, SHIYU_COLOR_PRIMARY, UI_FONT_BODY);
    s_cloud_label = make_label(s_cloud_screen, s_cloud_text, 48, 100, SHIYU_COLOR_SECONDARY, UI_FONT_BODY);
    s_cloud_qr = lv_qrcode_create(s_cloud_screen); lv_qrcode_set_size(s_cloud_qr, 320);
    lv_obj_align(s_cloud_qr, LV_ALIGN_CENTER, 0, 20); lv_obj_add_flag(s_cloud_qr, LV_OBJ_FLAG_HIDDEN);
    make_label(s_cloud_screen, "Use kitchen owner phone. QR expires in 10 minutes.", 48, 548, SHIYU_COLOR_SECONDARY, UI_FONT_BODY);
    lv_screen_load(s_cloud_screen); cloud_sync_request_pairing();
}
static void carousel_back(lv_event_t *e){(void)e;lv_screen_load(s_ui.settings);lv_obj_delete(s_carousel_screen);s_carousel_screen=NULL;s_carousel_status=NULL;s_carousel_value=NULL;}
static void carousel_adjust(lv_event_t *e){s_carousel_save_requested=false;int delta=(int)(intptr_t)lv_event_get_user_data(e);int value=(int)s_carousel_draft+delta;if(value>=5&&value<=300)s_carousel_draft=value;lv_label_set_text_fmt(s_carousel_value,"%u seconds",s_carousel_draft);}
static void carousel_save(lv_event_t *e){(void)e;if(carousel_settings_save(s_carousel_draft))s_carousel_save_requested=true;else lv_label_set_text(s_carousel_status,"Save unavailable - retry");}
static void carousel_open(lv_event_t *e){(void)e;s_carousel_save_requested=false;s_carousel_draft=carousel_settings_seconds();s_carousel_screen=lv_obj_create(NULL);prepare_screen(s_carousel_screen,SHIYU_COLOR_BACKGROUND);lv_obj_t *b=make_button(s_carousel_screen,40,32,180,56,SHIYU_COLOR_SURFACE,"< Back");lv_obj_add_event_cb(b,carousel_back,LV_EVENT_CLICKED,NULL);make_label(s_carousel_screen,"Photo carousel",80,140,SHIYU_COLOR_PRIMARY,UI_FONT_BODY);s_carousel_value=make_label(s_carousel_screen,"",380,260,SHIYU_COLOR_PRIMARY,UI_FONT_BODY);lv_label_set_text_fmt(s_carousel_value,"%u seconds",s_carousel_draft);b=make_button(s_carousel_screen,200,240,120,80,SHIYU_COLOR_SURFACE,"-");lv_obj_add_event_cb(b,carousel_adjust,LV_EVENT_CLICKED,(void*)(intptr_t)-1);b=make_button(s_carousel_screen,650,240,120,80,SHIYU_COLOR_SURFACE,"+");lv_obj_add_event_cb(b,carousel_adjust,LV_EVENT_CLICKED,(void*)(intptr_t)1);b=make_button(s_carousel_screen,380,380,220,70,SHIYU_COLOR_ACCENT,"Save");lv_obj_add_event_cb(b,carousel_save,LV_EVENT_CLICKED,NULL);s_carousel_status=make_label(s_carousel_screen,"5-300 seconds; tap Save",280,490,SHIYU_COLOR_SECONDARY,UI_FONT_BODY);lv_screen_load(s_carousel_screen);}

static void create_settings(void)
{
    s_ui.settings = lv_obj_create(NULL);
    prepare_screen(s_ui.settings, SHIYU_COLOR_BACKGROUND);
    lv_obj_t *back = make_button(s_ui.settings, 40, 32, 132, 56,
                                 SHIYU_COLOR_SURFACE, "<  返回");
    lv_obj_add_event_cb(back, on_back_home, LV_EVENT_CLICKED, NULL);
    make_label(s_ui.settings, "设置", 48, 124, SHIYU_COLOR_PRIMARY, UI_FONT_TITLE);
    make_label(s_ui.settings, "网络与屏幕", 48, 174, SHIYU_COLOR_SECONDARY, UI_FONT_BODY);

    lv_obj_t *network = make_card(s_ui.settings, 48, 210, 928, 120, SHIYU_COLOR_ACCENT);
    make_label(network, "网络", 28, 22, SHIYU_COLOR_PRIMARY, UI_FONT_TITLE);
    s_ui.settings_network_status = make_label(network, "正在检查网络", 28, 64,
                                              SHIYU_COLOR_PRIMARY, UI_FONT_BODY);
    lv_obj_t *network_button = make_button(network, 700, 30, 180, 58,
                                           SHIYU_COLOR_SURFACE, "配置网络");
    lv_obj_add_event_cb(network_button, on_network_setup, LV_EVENT_CLICKED, NULL);

    lv_obj_t *ambient = make_card(s_ui.settings, 48, 350, 450, 120, SHIYU_COLOR_SURFACE);
    make_label(ambient, "环境模式", 28, 20, SHIYU_COLOR_PRIMARY, UI_FONT_TITLE);
    make_label(ambient, "60 秒无操作后进入", 28, 62, SHIYU_COLOR_SECONDARY, UI_FONT_BODY);
    make_label(ambient, "已开启", 354, 45, SHIYU_COLOR_PRIMARY, UI_FONT_BODY);

    lv_obj_t *night = make_card(s_ui.settings, 522, 350, 454, 120, SHIYU_COLOR_SURFACE);
    make_label(night, "夜间息屏 [PLANNED]", 28, 20, SHIYU_COLOR_PRIMARY, UI_FONT_TITLE);
    make_label(night, "23:00  -  07:00", 28, 62,
               SHIYU_COLOR_SECONDARY, UI_FONT_BODY);
    make_label(night, "下一项", 354, 45, SHIYU_COLOR_SECONDARY, UI_FONT_BODY);
    lv_obj_t *cloud = make_button(s_ui.settings, 48, 506, 360, 62, SHIYU_COLOR_ACCENT, "Cloud / Bind screen");
    lv_obj_add_event_cb(cloud, on_cloud_open, LV_EVENT_CLICKED, NULL);
    lv_obj_t *carousel=make_button(s_ui.settings,440,506,360,62,SHIYU_COLOR_SURFACE,"Photo carousel");
    lv_obj_add_event_cb(carousel,carousel_open,LV_EVENT_CLICKED,NULL);
}

static void create_ambient(void)
{
    s_ui.ambient = lv_obj_create(NULL);
    prepare_screen(s_ui.ambient, SHIYU_COLOR_AMBIENT);
    s_ui.ambient_image = lv_image_create(s_ui.ambient);
    lv_obj_set_pos(s_ui.ambient_image, 0, 0);
    lv_obj_add_flag(s_ui.ambient_image, LV_OBJ_FLAG_HIDDEN);
    s_ui.ambient_title = make_label(s_ui.ambient, "让家里安静一会儿", 48, 420,
                                    SHIYU_COLOR_WHITE, UI_FONT_TITLE);
    s_ui.ambient_hint = make_label(s_ui.ambient, "轻触任意位置打开食愈", 48, 478,
                                   0xDCE8DE, UI_FONT_BODY);
    make_label(s_ui.ambient, "环境模式", 48, 42, SHIYU_COLOR_WHITE, UI_FONT_BODY);

    lv_obj_t *touch_layer = lv_obj_create(s_ui.ambient);
    lv_obj_set_size(touch_layer, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(touch_layer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(touch_layer, 0, 0);
    lv_obj_clear_flag(touch_layer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(touch_layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(touch_layer, on_ambient, LV_EVENT_CLICKED, NULL);
}

static void create_network(void)
{
    s_ui.network = lv_obj_create(NULL);
    prepare_screen(s_ui.network, SHIYU_COLOR_BACKGROUND);
    show_network_scan_view("正在扫描附近网络");
    ESP_LOGI(TAG, "Created transient NETWORK");
    log_memory("network-created");
}

static void show_network_scan_view(const char *message)
{
    if (s_ui.network == NULL) return;
    lv_obj_clean(s_ui.network);
    s_ui.network_textarea = NULL;
    s_ui.network_ssid = NULL;
    s_ui.network_password = NULL;
    lv_obj_t *content = lv_obj_create(s_ui.network);
    lv_obj_set_size(content, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_radius(content, 0, 0);
    lv_obj_set_style_pad_left(content, 48, 0);
    lv_obj_set_style_pad_right(content, 48, 0);
    lv_obj_set_style_pad_top(content, 18, 0);
    lv_obj_set_style_pad_bottom(content, 24, 0);
    lv_obj_set_style_pad_row(content, 10, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header = lv_obj_create(content);
    lv_obj_set_size(header, LV_PCT(100), 66);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_style_pad_column(header, 12, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = make_button(header, 0, 0, 52, 52,
                                 SHIYU_COLOR_SURFACE, LV_SYMBOL_LEFT);
    lv_obj_add_event_cb(back, on_network_close, LV_EVENT_CLICKED, NULL);
    make_label(header, "选择网络", 0, 0,
               SHIYU_COLOR_PRIMARY, UI_FONT_TITLE);

    s_ui.network_status = make_label(content,
                                     message != NULL ? message : "请选择 2.4GHz 网络",
                                     0, 0, SHIYU_COLOR_SECONDARY, UI_FONT_BODY);

    lv_obj_t *list = lv_obj_create(content);
    lv_obj_set_size(list, LV_PCT(100), 1);
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_pad_row(list, 8, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    for (size_t index = 0; index < s_ui.network_choice_count; ++index) {
        const wifi_manager_scan_item_t *choice = &s_ui.network_choices[index];
        lv_obj_t *row = make_button(list, 0, 0, 900, 56,
                                    choice->auth == WIFI_MANAGER_AUTH_UNSUPPORTED
                                        ? SHIYU_COLOR_SURFACE
                                        : SHIYU_COLOR_ACCENT,
                                    choice->display_ssid);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_flex_grow(row, 0);
        lv_obj_add_event_cb(row, on_network_choice, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)index);
    }
    if (s_ui.network_choice_count == 0) {
        make_label(list, "暂未发现网络", 20, 20,
                   SHIYU_COLOR_SECONDARY, UI_FONT_BODY);
    }

    log_memory("network-list-created");
}

static void show_network_password_view(void)
{
    if (s_ui.selected_network >= s_ui.network_choice_count) return;
    const wifi_manager_scan_item_t *choice =
        &s_ui.network_choices[s_ui.selected_network];
    log_memory("before-network-list-release");
    lv_obj_clean(s_ui.network);
    s_ui.network_status = NULL;
    log_memory("after-network-list-release");

    if (choice->auth == WIFI_MANAGER_AUTH_OPEN) {
        make_label(s_ui.network, "连接网络", 48, 18,
                   SHIYU_COLOR_PRIMARY, UI_FONT_TITLE);
        make_label(s_ui.network, choice->display_ssid, 48, 56,
                   SHIYU_COLOR_SECONDARY, UI_FONT_BODY);
        make_label(s_ui.network, "这是开放网络，连接不需要密码。", 48, 160,
                   SHIYU_COLOR_SECONDARY, UI_FONT_BODY);
        make_label(s_ui.network, "需要网页登录的网络暂不支持。", 48, 204,
                   SHIYU_COLOR_SECONDARY, UI_FONT_BODY);
        lv_obj_t *back = make_button(s_ui.network, 520, 420, 210, 62,
                                     SHIYU_COLOR_SURFACE, "返回列表");
        lv_obj_add_event_cb(back, on_network_refresh, LV_EVENT_CLICKED, NULL);
        lv_obj_t *connect = make_button(s_ui.network, 754, 420, 222, 62,
                                        SHIYU_COLOR_ACCENT, "确认连接");
        lv_obj_add_event_cb(connect, on_open_network_connect, LV_EVENT_CLICKED, NULL);
        return;
    }

    lv_obj_clear_flag(s_ui.network, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *content = lv_obj_create(s_ui.network);
    lv_obj_set_pos(content, 0, 0);
    lv_obj_set_size(content, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_radius(content, 0, 0);
    lv_obj_set_style_pad_left(content, 48, 0);
    lv_obj_set_style_pad_right(content, 48, 0);
    lv_obj_set_style_pad_top(content, 18, 0);
    lv_obj_set_style_pad_bottom(content, 24, 0);
    lv_obj_set_style_pad_row(content, 10, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *header = lv_obj_create(content);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, 52);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_style_pad_column(header, 12, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = make_button(header, 0, 0, 52, 52,
                                 SHIYU_COLOR_SURFACE, LV_SYMBOL_LEFT);
    lv_obj_add_event_cb(back, on_password_back, LV_EVENT_CLICKED, NULL);

    lv_obj_t *title_label = make_label(header, "连接网络", 0, 0,
                                       SHIYU_COLOR_PRIMARY, UI_FONT_TITLE);
    lv_obj_t *ssid_label = make_label(header, choice->display_ssid, 0, 0,
                                      SHIYU_COLOR_SECONDARY, UI_FONT_BODY);
    lv_obj_set_width(ssid_label, 1);
    lv_obj_set_flex_grow(ssid_label, 1);
    lv_label_set_long_mode(ssid_label, LV_LABEL_LONG_MODE_DOTS);

    lv_obj_t *input_row = lv_obj_create(content);
    lv_obj_set_size(input_row, LV_PCT(100), 54);
    lv_obj_set_style_bg_opa(input_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(input_row, 0, 0);
    lv_obj_set_style_pad_all(input_row, 0, 0);
    lv_obj_set_style_pad_column(input_row, 10, 0);
    lv_obj_set_flex_flow(input_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(input_row, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(input_row, LV_OBJ_FLAG_SCROLLABLE);

    s_ui.network_textarea = lv_textarea_create(input_row);
    lv_obj_set_size(s_ui.network_textarea, 1, LV_PCT(100));
    lv_obj_set_flex_grow(s_ui.network_textarea, 1);
    lv_textarea_set_one_line(s_ui.network_textarea, true);
    lv_textarea_set_password_mode(s_ui.network_textarea, true);
    lv_textarea_set_password_show_time(s_ui.network_textarea, 0);
    lv_textarea_set_max_length(s_ui.network_textarea, 63);
    lv_textarea_set_placeholder_text(s_ui.network_textarea, "请输入 Wi-Fi 密码");
    lv_obj_set_style_text_font(s_ui.network_textarea, UI_FONT_BODY, 0);

    lv_obj_t *eye = make_button(input_row, 0, 0, 54, 54,
                                SHIYU_COLOR_SURFACE, LV_SYMBOL_EYE_OPEN);
    lv_obj_add_event_cb(eye, on_password_eye, LV_EVENT_CLICKED, NULL);

    lv_obj_t *keyboard = lv_keyboard_create(content);
    lv_obj_set_width(keyboard, LV_PCT(100));
    lv_obj_set_height(keyboard, 1);
    lv_obj_set_flex_grow(keyboard, 1);
    lv_obj_set_style_text_font(keyboard, UI_FONT_BODY, LV_PART_ITEMS);
    lv_obj_set_style_pad_all(keyboard, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_row(keyboard, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_column(keyboard, 6, LV_PART_MAIN);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER,
                        SHIYU_KB_MAP_LOWER, SHIYU_KB_CTRL_TEXT);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_TEXT_UPPER,
                        SHIYU_KB_MAP_UPPER, SHIYU_KB_CTRL_TEXT);
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_SPECIAL,
                        SHIYU_KB_MAP_SPECIAL, SHIYU_KB_CTRL_SPECIAL);
    lv_keyboard_set_textarea(keyboard, s_ui.network_textarea);
    lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_obj_remove_event_cb(keyboard, lv_keyboard_def_event_cb);
    lv_obj_add_event_cb(keyboard, on_password_keyboard_value_changed,
                        LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(keyboard, on_password_keyboard, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(keyboard, on_password_keyboard, LV_EVENT_CANCEL, NULL);

    lv_obj_update_layout(s_ui.network);
    lv_display_t *display = lv_obj_get_display(s_ui.network);
    ESP_LOGI(TAG, "network-password-layout-v5 display w=%ld h=%ld",
             (long)lv_display_get_horizontal_resolution(display),
             (long)lv_display_get_vertical_resolution(display));
    log_layout_geometry("root", s_ui.network);
    log_layout_geometry("content", content);
    log_layout_geometry("header", header);
    log_layout_geometry("back", back);
    log_layout_geometry("title", title_label);
    log_layout_geometry("ssid", ssid_label);
    log_layout_geometry("input-row", input_row);
    log_layout_geometry("textarea", s_ui.network_textarea);
    log_layout_geometry("eye", eye);
    log_layout_geometry("keyboard", keyboard);
    lv_area_t root_area;
    lv_area_t content_area;
    lv_area_t header_area;
    lv_area_t back_area;
    lv_area_t title_area;
    lv_area_t ssid_area;
    lv_area_t input_row_area;
    lv_area_t textarea_area;
    lv_area_t eye_area;
    lv_area_t keyboard_area;
    lv_obj_get_coords(s_ui.network, &root_area);
    lv_obj_get_coords(content, &content_area);
    lv_obj_get_coords(header, &header_area);
    lv_obj_get_coords(back, &back_area);
    lv_obj_get_coords(title_label, &title_area);
    lv_obj_get_coords(ssid_label, &ssid_area);
    lv_obj_get_coords(input_row, &input_row_area);
    lv_obj_get_coords(s_ui.network_textarea, &textarea_area);
    lv_obj_get_coords(eye, &eye_area);
    lv_obj_get_coords(keyboard, &keyboard_area);
    bool invalid_size = lv_area_get_width(&back_area) <= 0 ||
                        lv_area_get_height(&back_area) <= 0 ||
                        lv_area_get_width(&title_area) <= 0 ||
                        lv_area_get_width(&ssid_area) <= 0 ||
                        lv_area_get_width(&textarea_area) <= 0 ||
                        lv_area_get_width(&eye_area) <= 0 ||
                        lv_area_get_width(&keyboard_area) <= 0 ||
                        lv_area_get_height(&keyboard_area) <= 0;
    bool outside_root = content_area.x1 < root_area.x1 || content_area.y1 < root_area.y1 ||
                        content_area.x2 > root_area.x2 || content_area.y2 > root_area.y2 ||
                        back_area.x1 < root_area.x1 || back_area.y1 < root_area.y1 ||
                        back_area.x2 > root_area.x2 || back_area.y2 > root_area.y2 ||
                        eye_area.x1 < root_area.x1 || eye_area.y1 < root_area.y1 ||
                        eye_area.x2 > root_area.x2 || eye_area.y2 > root_area.y2 ||
                        keyboard_area.x1 < root_area.x1 || keyboard_area.y1 < root_area.y1 ||
                        keyboard_area.x2 > root_area.x2 || keyboard_area.y2 > root_area.y2;
    bool invalid_order = back_area.y1 < header_area.y1 || back_area.y2 > header_area.y2 ||
                         title_area.y1 < header_area.y1 || title_area.y2 > header_area.y2 ||
                         ssid_area.x1 <= title_area.x2 ||
                         input_row_area.y1 <= header_area.y2 ||
                         eye_area.x1 <= textarea_area.x2 ||
                         keyboard_area.y1 <= input_row_area.y2;
    if (invalid_size || outside_root || invalid_order) {
        ESP_LOGE(TAG,
                 "network-password-layout-v5 invalid size=%d outside=%d order=%d root_bottom=%ld keyboard_bottom=%ld",
                 invalid_size, outside_root, invalid_order,
                 (long)root_area.y2, (long)keyboard_area.y2);
    }
    log_memory("network-keyboard-created");
}

static void show_network_connecting_view(const char *message)
{
    if (s_ui.network == NULL) return;
    reset_password_field();
    lv_obj_clean(s_ui.network);
    s_ui.network_textarea = NULL;
    make_label(s_ui.network, "网络", 48, 48,
               SHIYU_COLOR_PRIMARY, UI_FONT_TITLE);
    s_ui.network_status = make_label(s_ui.network,
                                     message != NULL ? message : "正在连接网络",
                                     48, 130, SHIYU_COLOR_SECONDARY, UI_FONT_BODY);
    lv_obj_t *back = make_button(s_ui.network, 760, 470, 216, 62,
                                 SHIYU_COLOR_SURFACE, "返回设置");
    lv_obj_add_event_cb(back, on_network_close, LV_EVENT_CLICKED, NULL);
    log_memory("network-connecting-created");
}

void shiyu_ui_create(shiyu_ui_action_cb_t start_network_scan,
                     shiyu_ui_action_cb_t start_network_setup,
                     shiyu_ui_action_cb_t stop_network_setup,
                     shiyu_ui_action_cb_t retry_network,
                     shiyu_ui_action_cb_t forget_network,
                     shiyu_ui_connect_cb_t connect_network)
{
    initialize_ui_fonts();
    s_ui.start_network_scan = start_network_scan;
    s_ui.start_network_setup = start_network_setup;
    s_ui.stop_network_setup = stop_network_setup;
    s_ui.retry_network = retry_network;
    s_ui.forget_network = forget_network;
    s_ui.connect_network = connect_network;
    create_home();
    create_settings();
    create_ambient();
    refresh_inventory_ui(true);
    lv_timer_create(on_state_timer, SHIYU_STATE_CHECK_PERIOD_MS, NULL);
    show_active(SCREEN_HOME);
    log_memory("resident-ui-created");
}

void shiyu_ui_show_scan_results(const wifi_manager_scan_list_t *results)
{
    if (results == NULL || s_ui.network == NULL) return;
    log_memory("before-scan-list-copy");
    s_ui.network_choice_count = results->count > 10 ? 10 : results->count;
    memcpy(s_ui.network_choices, results->items,
           s_ui.network_choice_count * sizeof(s_ui.network_choices[0]));
    show_network_scan_view(s_ui.network_choice_count > 0
                               ? "请选择 2.4GHz 网络"
                               : "未发现可见网络，请返回后重新进入");
    log_memory("after-scan-list-copy");
}

void shiyu_ui_set_network_state(const char *status, const char *ssid)
{
    if (status == NULL) return;
    char text[96];
    if (ssid != NULL && ssid[0] != '\0') {
        snprintf(text, sizeof(text), "%s：%s", status, ssid);
    } else {
        strlcpy(text, status, sizeof(text));
    }
    if (s_ui.settings_network_status != NULL) {
        lv_label_set_text(s_ui.settings_network_status, text);
    }
    if (s_ui.network_status != NULL) lv_label_set_text(s_ui.network_status, status);
}

void shiyu_ui_set_network_setup_info(const char *ssid, const char *password)
{
    if (ssid != NULL && s_ui.network_ssid != NULL) {
        lv_label_set_text(s_ui.network_ssid, ssid);
    }
    if (password != NULL && s_ui.network_password != NULL) {
        lv_label_set_text(s_ui.network_password, password);
    }
}

void shiyu_ui_set_ambient_photo(const char *lvgl_path)
{
    if (!lvgl_path || !s_ui.ambient_image) return;
    /* One reusable photo resource, independent of the two RGB panel FBs. */
    static lv_image_dsc_t frame = {
        .header = {.magic=LV_IMAGE_HEADER_MAGIC, .cf=LV_COLOR_FORMAT_RGB565,
                   .w=1024, .h=600, .stride=2048},
        .data_size = 1024U * 600U * 2U,
    };
    if (!frame.data) {
        frame.data = heap_caps_aligned_alloc(128, frame.data_size,
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!frame.data) {
            ESP_LOGE(TAG, "Photo resource allocation failed");
            return;
        }
    }
    /* Drain pending visible refresh tasks before changing their shared pixels.
       lv_refr_now waits for SW layer tasks; skip initial hidden HOME image. */
    if (s_ui.mode == MODE_AMBIENT) lv_refr_now(NULL);
    lv_draw_wait_for_finish();
    lv_image_cache_drop(&frame);
    bool dirty = false;
    int64_t started = esp_timer_get_time();
    if (!photo_storage_decode_frame(lvgl_path, (void *)frame.data, frame.data_size, &dirty)) {
        ESP_LOGW(TAG, "Photo resource decode failed (output touched=%d)", dirty);
        if (dirty) {
            lv_obj_add_flag(s_ui.ambient_image, LV_OBJ_FLAG_HIDDEN);
            lv_image_set_src(s_ui.ambient_image, NULL);
            lv_obj_remove_flag(s_ui.ambient_title, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(s_ui.ambient_hint, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    lv_image_set_src(s_ui.ambient_image, &frame);
    lv_obj_remove_flag(s_ui.ambient_image, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ui.ambient_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ui.ambient_hint, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(s_ui.ambient_image);
    ESP_LOGI(TAG, "Photo resource updated in %lldms", (long long)((esp_timer_get_time()-started)/1000));
}
