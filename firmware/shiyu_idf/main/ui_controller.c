#include "ui_controller.h"
#include "cloud_sync.h"
#include "photo_storage.h"
#include "carousel_settings.h"
#include "inventory_model.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

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
static lv_obj_t *s_stock_count, *s_stock_list, *s_stock_page_label, *s_stock_previous, *s_stock_next;
static inventory_row_t *s_stock_rows;
static inventory_row_t *s_detail_rows;
static unsigned s_stock_page, s_stock_rows_count;
static lv_obj_t *s_home_names[4], *s_home_quantities[4], *s_home_status, *s_home_clock, *s_home_date;
#define STOCK_PAGE_SIZE 6U
static char s_stock_selected_name[121], s_stock_selected_unit[25];
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
            s_stock_list = NULL; s_stock_page_label = NULL; s_stock_previous=NULL; s_stock_next=NULL;
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
    strlcpy(s_stock_selected_name,s_stock_rows[index].name,sizeof(s_stock_selected_name));
    strlcpy(s_stock_selected_unit,s_stock_rows[index].unit,sizeof(s_stock_selected_unit));
    ensure_active_screen(SCREEN_FOOD_DETAIL);
    refresh_inventory_ui(true);
    show_active(SCREEN_FOOD_DETAIL);
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
    static time_t shown_minute=-1;
    time_t now=time(NULL);
    if(s_home_clock && now>=1700000000 && now/60!=shown_minute) {
        shown_minute=now/60;
        time_t china_time=now+8*3600; struct tm local; gmtime_r(&china_time,&local);
        char text[48];strftime(text,sizeof(text),"%H:%M",&local);lv_label_set_text(s_home_clock,text);
        snprintf(text,sizeof(text),"%d月%d日",local.tm_mon+1,local.tm_mday);lv_label_set_text(s_home_date,text);
    }
    if(s_carousel_status && s_carousel_save_requested){int result=carousel_settings_result();lv_label_set_text(s_carousel_status,result==2?"正在保存":result==1?"已保存":result<0?"保存失败，保留原设置":"请输入 5–300 秒，点击保存生效");}
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
    make_label(s_ui.home, "食愈", 36, 33, SHIYU_COLOR_PRIMARY, UI_FONT_TITLE);
    s_home_status=make_label(s_ui.home,"等待同步",640,44,SHIYU_COLOR_SECONDARY,UI_FONT_BODY);
    lv_obj_t *button=make_button(s_ui.home,928,24,60,60,SHIYU_COLOR_SURFACE,LV_SYMBOL_SETTINGS);
    lv_obj_add_event_cb(button,on_settings,LV_EVENT_CLICKED,NULL);
    lv_obj_t *stock=make_card(s_ui.home,36,102,608,434,SHIYU_COLOR_SURFACE);
    make_label(stock,"厨房库存",24,22,SHIYU_COLOR_PRIMARY,UI_FONT_BODY);
    s_stock_count=make_label(stock,"—",420,22,SHIYU_COLOR_SECONDARY,UI_FONT_BODY);
    for(unsigned i=0;i<4;++i) {
        s_home_names[i]=make_label(stock,"",24,86+i*65,SHIYU_COLOR_PRIMARY,UI_FONT_BODY);
        lv_obj_set_width(s_home_names[i],220); lv_label_set_long_mode(s_home_names[i],LV_LABEL_LONG_DOT);
        s_home_quantities[i]=make_label(stock,"",246,86+i*65,SHIYU_COLOR_SECONDARY,UI_FONT_BODY);
        lv_obj_set_size(s_home_quantities[i],338,60);lv_obj_set_style_text_align(s_home_quantities[i],LV_TEXT_ALIGN_RIGHT,0);
        lv_label_set_long_mode(s_home_quantities[i],LV_LABEL_LONG_DOT);
    }
    button=make_button(stock,16,356,576,62,SHIYU_COLOR_ACCENT,"查看全部  >");
    lv_obj_add_event_cb(button,on_inventory,LV_EVENT_CLICKED,NULL);
    lv_obj_t *clock=make_card(s_ui.home,668,102,320,206,0xE9EDDF);
    s_home_clock=make_label(clock,"--:--",32,36,SHIYU_COLOR_PRIMARY,&lv_font_montserrat_48);
    s_home_date=make_label(clock,"等待网络校时",24,132,SHIYU_COLOR_SECONDARY,UI_FONT_BODY);
    lv_obj_t *hint=make_card(s_ui.home,668,328,320,208,SHIYU_COLOR_ACCENT);
    make_label(hint,"手机记录，家人共享",24,30,SHIYU_COLOR_PRIMARY,UI_FONT_BODY);
    make_label(hint,"添加食材、调整数量\n都在手机完成",24,94,SHIYU_COLOR_SECONDARY,UI_FONT_BODY);
    make_label(s_ui.home,"轻触查看库存 · 片刻后回到照片",36,556,SHIYU_COLOR_SECONDARY,UI_FONT_BODY);
}

static void stock_page_change(lv_event_t *event)
{
    int step=(int)(intptr_t)lv_event_get_user_data(event);
    unsigned count, serial; bool online;
    inventory_model_meta(&count,&online,&serial);
    unsigned pages=(count+STOCK_PAGE_SIZE-1)/STOCK_PAGE_SIZE;
    if(step<0 && s_stock_page) --s_stock_page;
    if(step>0 && s_stock_page+1<pages) ++s_stock_page;
    refresh_inventory_ui(true);
}

static void stock_quantity(char *out, size_t capacity, const inventory_row_t *row)
{
    char number[sizeof(row->quantity)]; strlcpy(number,row->quantity,sizeof(number));
    if(strchr(number,'.')) {
        size_t n=strlen(number);
        while(n && number[n-1]=='0') number[--n]=0;
        if(n && number[n-1]=='.') number[--n]=0;
    }
    snprintf(out,capacity,"%s %s",number,row->unit);
}

static const char *stock_expiry_status(const char *date,time_t day)
{
    if(!*date) return "未填日期";
    if(day<0) return "";
    int y=atoi(date),m=atoi(date+5),d=atoi(date+8);
    y-=m<=2;int era=y/400;unsigned yoe=(unsigned)(y-era*400);
    unsigned doy=(153*(m+(m>2?-3:9))+2)/5+d-1;
    int64_t target=(int64_t)era*146097+yoe*365+yoe/4-yoe/100+doy-719468;
    int64_t remaining=target-day;
    return remaining<0?"已过填写日期":remaining==0?"今天到期":remaining<=3?"临近到期":"";
}
static void stock_date_line(char *out,size_t capacity,const inventory_row_t *row,time_t day)
{
    const char *status=stock_expiry_status(row->expires_on,day);
    snprintf(out,capacity,"%s%s%s",*row->expires_on?row->expires_on:"",*row->expires_on&&*status?" · ":"",status);
}

static void refresh_inventory_ui(bool force)
{
    static unsigned previous=~0U; static bool was_online, was_known;
    static time_t previous_day=-1;
    time_t now=time(NULL);
    time_t day=now>=1704067200?(now+8*3600)/86400:-1;
    unsigned count, serial; bool online;
    bool known=inventory_model_meta(&count,&online,&serial);
    if(!force && previous==serial && was_online==online && was_known==known && previous_day==day) return;
    previous_day=day;
    previous=serial; was_online=online; was_known=known;
    if(s_stock_count) {
        if(known) lv_label_set_text_fmt(s_stock_count,"%u 种",count);
        else lv_label_set_text(s_stock_count,"—");
        lv_label_set_text(s_home_status,!known?"等待库存同步":online?"已同步":"离线 · 上次库存");
        inventory_row_t preview; char quantity[160];
        for(unsigned i=0;i<4;++i) {
            bool found=known && inventory_model_row(i,&preview);
            lv_label_set_text(s_home_names[i],found?preview.name:i==0?(known?"暂无食材，请在手机添加":"等待厨房库存同步"):"");
            if(found) {stock_quantity(quantity,sizeof(quantity),&preview);
                if(preview.batches>1) {size_t used=strlen(quantity);snprintf(quantity+used,sizeof(quantity)-used," · %u 批",preview.batches);}}
            if(found) {size_t used=strlen(quantity);quantity[used++]='\n';stock_date_line(quantity+used,sizeof(quantity)-used,&preview,day);}
            lv_label_set_text(s_home_quantities[i],found?quantity:"");
        }
    }
    if(s_ui.inventory && s_stock_list && s_stock_rows) {
        lv_label_set_text(s_ui.inventory_context,!known?"尚未同步，请先联网并绑定厨房":
                          online?"已同步 · 请在手机管理食材":"离线缓存 · 请在手机管理食材");
        if(s_stock_page*STOCK_PAGE_SIZE>=count) s_stock_page=0;
        s_stock_rows_count=inventory_model_page(s_stock_page*STOCK_PAGE_SIZE,s_stock_rows,STOCK_PAGE_SIZE,&count);
        lv_obj_clean(s_stock_list);
        if(!s_stock_rows_count) make_label(s_stock_list,known?"暂无食材，请在手机添加":"等待厨房库存同步",20,32,SHIYU_COLOR_SECONDARY,UI_FONT_BODY);
        for(unsigned i=0;i<s_stock_rows_count;++i) {
            lv_obj_t *card=make_card(s_stock_list,0,i*66,952,62,SHIYU_COLOR_SURFACE);
            lv_obj_add_flag(card,LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_event_cb(card,on_food_detail,LV_EVENT_CLICKED,(void *)(uintptr_t)i);
            lv_obj_t *name=make_label(card,s_stock_rows[i].name,24,18,SHIYU_COLOR_PRIMARY,UI_FONT_BODY);
            if(s_stock_rows[i].batches>1) lv_label_set_text_fmt(name,"%s · %u 批",s_stock_rows[i].name,s_stock_rows[i].batches);
            lv_obj_set_width(name,430); lv_label_set_long_mode(name,LV_LABEL_LONG_DOT);
            char qty[160]; stock_quantity(qty,sizeof(qty),&s_stock_rows[i]);
            size_t used=strlen(qty);qty[used++]='\n';stock_date_line(qty+used,sizeof(qty)-used,&s_stock_rows[i],day);
            lv_obj_t *value=make_label(card,qty,474,2,SHIYU_COLOR_SECONDARY,UI_FONT_BODY);
            lv_obj_set_size(value,454,58);lv_obj_set_style_text_align(value,LV_TEXT_ALIGN_RIGHT,0);lv_label_set_long_mode(value,LV_LABEL_LONG_DOT);
        }
        if(s_stock_page==0) lv_obj_add_state(s_stock_previous,LV_STATE_DISABLED); else lv_obj_remove_state(s_stock_previous,LV_STATE_DISABLED);
        if((s_stock_page+1)*STOCK_PAGE_SIZE>=count) lv_obj_add_state(s_stock_next,LV_STATE_DISABLED); else lv_obj_remove_state(s_stock_next,LV_STATE_DISABLED);
        lv_label_set_text_fmt(s_stock_page_label,"%u / %u",s_stock_page+1,count?(count+STOCK_PAGE_SIZE-1)/STOCK_PAGE_SIZE:1);
    }
    if(s_ui.food_detail) {
        if(!s_detail_rows) s_detail_rows=heap_caps_calloc(INVENTORY_LIMIT,sizeof(*s_detail_rows),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        unsigned batches=s_detail_rows?inventory_model_group_batches(s_stock_selected_name,s_stock_selected_unit,s_detail_rows,INVENTORY_LIMIT):0;
        if(batches || !s_detail_rows) {
            lv_label_set_text(s_ui.food_detail_name,batches?s_detail_rows[0].name:s_stock_selected_name);
            size_t capacity=(size_t)batches*140+80;
            char *body=heap_caps_calloc(capacity,1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
            if(body) {
                size_t used=0;
                for(unsigned i=0;i<batches;i++) {
                    inventory_row_t *batch=&s_detail_rows[i];char qty[64];stock_quantity(qty,sizeof(qty),batch);
                    const char *status=stock_expiry_status(batch->expires_on,day);
                    int written=snprintf(body+used,capacity-used,"第 %u 批 · %s\n%s%s%s\n\n",i+1,qty,*batch->expires_on?batch->expires_on:"",*batch->expires_on&&*status?" · ":"",status);
                    if(written<0||(size_t)written>=capacity-used) break;
                    used+=(size_t)written;
                }
                if(!batches) strlcpy(body,"暂时无法载入批次",capacity);
                lv_label_set_text(s_ui.food_detail_quantity,body);free(body);
            } else lv_label_set_text(s_ui.food_detail_quantity,"内存不足，请返回重试");
            lv_label_set_text(s_ui.food_detail_status,online?"已同步 · 按填写日期提示":"离线缓存 · 按填写日期提示");
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
    lv_obj_t *back=make_button(s_ui.inventory,36,24,60,60,SHIYU_COLOR_SURFACE,LV_SYMBOL_LEFT);
    lv_obj_add_event_cb(back,on_back_home,LV_EVENT_CLICKED,NULL);
    make_label(s_ui.inventory,"厨房库存",116,40,SHIYU_COLOR_PRIMARY,UI_FONT_BODY);
    s_ui.inventory_context=make_label(s_ui.inventory,"等待同步",390,40,SHIYU_COLOR_SECONDARY,UI_FONT_BODY);
    s_stock_list=lv_obj_create(s_ui.inventory);
    lv_obj_set_pos(s_stock_list,36,102); lv_obj_set_size(s_stock_list,952,396);
    lv_obj_set_style_pad_all(s_stock_list,0,0); lv_obj_set_style_border_width(s_stock_list,0,0);
    lv_obj_set_style_bg_opa(s_stock_list,LV_OPA_TRANSP,0); lv_obj_remove_flag(s_stock_list,LV_OBJ_FLAG_SCROLLABLE);
    if(!s_stock_rows) s_stock_rows=heap_caps_calloc(STOCK_PAGE_SIZE,sizeof(*s_stock_rows),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    s_stock_previous=make_button(s_ui.inventory,36,520,170,56,SHIYU_COLOR_SURFACE,"<  上一页");
    lv_obj_add_event_cb(s_stock_previous,stock_page_change,LV_EVENT_CLICKED,(void *)(intptr_t)-1);
    s_stock_page_label=make_label(s_ui.inventory,"1 / 1",460,534,SHIYU_COLOR_SECONDARY,UI_FONT_BODY);
    s_stock_next=make_button(s_ui.inventory,818,520,170,56,SHIYU_COLOR_SURFACE,"下一页  >");
    lv_obj_add_event_cb(s_stock_next,stock_page_change,LV_EVENT_CLICKED,(void *)(intptr_t)1);
    refresh_inventory_ui(true);
}

static void create_food_detail(void)
{
    s_ui.food_detail=lv_obj_create(NULL);
    prepare_screen(s_ui.food_detail,SHIYU_COLOR_BACKGROUND);
    lv_obj_t *back=make_button(s_ui.food_detail,36,24,60,60,SHIYU_COLOR_SURFACE,LV_SYMBOL_LEFT);
    lv_obj_add_event_cb(back,on_back_inventory,LV_EVENT_CLICKED,NULL);
    make_label(s_ui.food_detail,"食材详情",116,40,SHIYU_COLOR_PRIMARY,UI_FONT_BODY);
    lv_obj_t *detail=make_card(s_ui.food_detail,36,102,952,366,SHIYU_COLOR_SURFACE);
    s_ui.food_detail_name=make_label(detail,"",36,32,SHIYU_COLOR_PRIMARY,UI_FONT_BODY);
    lv_obj_set_size(s_ui.food_detail_name,880,44);lv_label_set_long_mode(s_ui.food_detail_name,LV_LABEL_LONG_DOT);
    lv_obj_t *batch_scroll=lv_obj_create(detail);
    lv_obj_set_pos(batch_scroll,20,90);lv_obj_set_size(batch_scroll,910,210);
    lv_obj_set_style_bg_opa(batch_scroll,LV_OPA_TRANSP,0);lv_obj_set_style_border_width(batch_scroll,0,0);
    lv_obj_set_scroll_dir(batch_scroll,LV_DIR_VER);
    lv_obj_set_style_pad_all(batch_scroll,0,0);
    s_ui.food_detail_quantity=make_label(batch_scroll,"",8,8,SHIYU_COLOR_PRIMARY,UI_FONT_BODY);
    lv_obj_set_width(s_ui.food_detail_quantity,880);
    s_ui.food_detail_status=make_label(detail,"",36,318,SHIYU_COLOR_SECONDARY,UI_FONT_BODY);
    make_label(s_ui.food_detail,"需要调整数量？请在手机厨房中编辑。",60,514,SHIYU_COLOR_SECONDARY,UI_FONT_BODY);
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
static void carousel_back(lv_event_t *event)
{
    (void)event;
    lv_screen_load(s_ui.settings);
    lv_obj_delete(s_carousel_screen);
    s_carousel_screen=NULL; s_carousel_status=NULL; s_carousel_value=NULL;
    s_carousel_save_requested=false;
}

static void carousel_input_changed(lv_event_t *event)
{
    (void)event;
    s_carousel_save_requested=false;
    if(s_carousel_status) lv_label_set_text(s_carousel_status,"请输入 5–300 秒，点击保存生效");
}

static bool carousel_read_value(void)
{
    const char *text=lv_textarea_get_text(s_carousel_value);
    unsigned value=0;
    if(!text[0]) return false;
    for(const char *p=text;*p;++p) {
        if(*p<'0'||*p>'9') return false;
        value=value*10+(unsigned)(*p-'0');
        if(value>300) return false;
    }
    if(value<5) return false;
    s_carousel_draft=value;
    return true;
}

static void carousel_adjust(lv_event_t *event)
{
    if(!carousel_read_value()) {
        lv_label_set_text(s_carousel_status,"请输入 5–300 秒");
        return;
    }
    int value=(int)s_carousel_draft+(int)(intptr_t)lv_event_get_user_data(event);
    if(value<5||value>300) return;
    char text[8];snprintf(text,sizeof(text),"%d",value);
    lv_textarea_set_text(s_carousel_value,text);
}

static void carousel_save(lv_event_t *event)
{
    (void)event;
    s_carousel_save_requested=false;
    if(!carousel_read_value()) {
        lv_label_set_text(s_carousel_status,"请输入 5–300 秒");
        return;
    }
    if(carousel_settings_save(s_carousel_draft)) s_carousel_save_requested=true;
    else lv_label_set_text(s_carousel_status,"暂时无法保存，请重试");
}

static void carousel_open(lv_event_t *event)
{
    (void)event;
    if(s_carousel_screen) return;
    s_carousel_save_requested=false;
    s_carousel_draft=carousel_settings_seconds();
    s_carousel_screen=lv_obj_create(NULL);
    prepare_screen(s_carousel_screen,SHIYU_COLOR_BACKGROUND);
    lv_obj_t *button=make_button(s_carousel_screen,36,24,60,60,SHIYU_COLOR_SURFACE,LV_SYMBOL_LEFT);
    lv_obj_add_event_cb(button,carousel_back,LV_EVENT_CLICKED,NULL);
    make_label(s_carousel_screen,"照片轮播",116,40,SHIYU_COLOR_PRIMARY,UI_FONT_BODY);
    make_label(s_carousel_screen,"每张照片展示的秒数",36,112,SHIYU_COLOR_SECONDARY,UI_FONT_BODY);
    s_carousel_value=lv_textarea_create(s_carousel_screen);
    lv_obj_set_pos(s_carousel_value,380,154);lv_obj_set_size(s_carousel_value,220,64);
    lv_textarea_set_one_line(s_carousel_value,true);
    lv_textarea_set_accepted_chars(s_carousel_value,"0123456789");
    lv_textarea_set_max_length(s_carousel_value,3);
    lv_obj_set_style_text_font(s_carousel_value,UI_FONT_BODY,0);
    lv_obj_set_style_text_align(s_carousel_value,LV_TEXT_ALIGN_CENTER,0);
    char text[8];snprintf(text,sizeof(text),"%u",s_carousel_draft);
    lv_textarea_set_text(s_carousel_value,text);
    lv_obj_add_event_cb(s_carousel_value,carousel_input_changed,LV_EVENT_VALUE_CHANGED,NULL);
    button=make_button(s_carousel_screen,270,154,80,64,SHIYU_COLOR_SURFACE,"-");
    lv_obj_add_event_cb(button,carousel_adjust,LV_EVENT_CLICKED,(void *)(intptr_t)-1);
    button=make_button(s_carousel_screen,630,154,80,64,SHIYU_COLOR_SURFACE,"+");
    lv_obj_add_event_cb(button,carousel_adjust,LV_EVENT_CLICKED,(void *)(intptr_t)1);
    s_carousel_status=make_label(s_carousel_screen,"请输入 5–300 秒，点击保存生效",36,266,SHIYU_COLOR_SECONDARY,UI_FONT_BODY);
    button=make_button(s_carousel_screen,778,246,210,60,SHIYU_COLOR_ACCENT,"保存");
    lv_obj_add_event_cb(button,carousel_save,LV_EVENT_CLICKED,NULL);
    lv_obj_t *keyboard=lv_keyboard_create(s_carousel_screen);
    static const char *const number_map[]={"1","2","3","\n","4","5","6","\n","7","8","9","\n",LV_SYMBOL_BACKSPACE,"0",LV_SYMBOL_OK,""};
    static const lv_buttonmatrix_ctrl_t number_controls[]={1,1,1,1,1,1,1,1,1,1,1,1};
    lv_keyboard_set_map(keyboard,LV_KEYBOARD_MODE_NUMBER,number_map,number_controls);
    lv_keyboard_set_mode(keyboard,LV_KEYBOARD_MODE_NUMBER);
    lv_obj_add_event_cb(keyboard,carousel_save,LV_EVENT_READY,NULL);
    lv_keyboard_set_textarea(keyboard,s_carousel_value);
    lv_obj_set_size(keyboard,LV_PCT(100),250);
    lv_obj_align(keyboard,LV_ALIGN_BOTTOM_MID,0,0);
    lv_obj_set_style_text_font(keyboard,UI_FONT_BODY,LV_PART_ITEMS);
    lv_screen_load(s_carousel_screen);
}

static void create_settings(void)
{
    s_ui.settings=lv_obj_create(NULL);
    prepare_screen(s_ui.settings,SHIYU_COLOR_BACKGROUND);
    lv_obj_t *back=make_button(s_ui.settings,36,24,60,60,SHIYU_COLOR_SURFACE,LV_SYMBOL_LEFT);
    lv_obj_add_event_cb(back,on_back_home,LV_EVENT_CLICKED,NULL);
    make_label(s_ui.settings,"设置",116,40,SHIYU_COLOR_PRIMARY,UI_FONT_BODY);
    lv_obj_t *network=make_card(s_ui.settings,36,102,952,128,SHIYU_COLOR_SURFACE);
    make_label(network,"网络连接",24,20,SHIYU_COLOR_PRIMARY,UI_FONT_BODY);
    s_ui.settings_network_status=make_label(network,"正在检查网络",24,68,SHIYU_COLOR_SECONDARY,UI_FONT_BODY);
    lv_obj_set_width(s_ui.settings_network_status,640);lv_label_set_long_mode(s_ui.settings_network_status,LV_LABEL_LONG_DOT);
    lv_obj_t *button=make_button(network,730,34,194,60,SHIYU_COLOR_ACCENT,"配置网络  >");
    lv_obj_add_event_cb(button,on_network_setup,LV_EVENT_CLICKED,NULL);
    lv_obj_t *carousel=make_card(s_ui.settings,36,250,952,128,SHIYU_COLOR_SURFACE);
    make_label(carousel,"照片轮播",24,20,SHIYU_COLOR_PRIMARY,UI_FONT_BODY);
    make_label(carousel,"60 秒无操作后展示照片",24,68,SHIYU_COLOR_SECONDARY,UI_FONT_BODY);
    button=make_button(carousel,730,34,194,60,SHIYU_COLOR_ACCENT,"设置间隔  >");
    lv_obj_add_event_cb(button,carousel_open,LV_EVENT_CLICKED,NULL);
    lv_obj_t *cloud=make_card(s_ui.settings,36,398,952,128,SHIYU_COLOR_SURFACE);
    make_label(cloud,"设备与厨房",24,20,SHIYU_COLOR_PRIMARY,UI_FONT_BODY);
    make_label(cloud,"查看云端连接与设备绑定",24,68,SHIYU_COLOR_SECONDARY,UI_FONT_BODY);
    button=make_button(cloud,730,34,194,60,SHIYU_COLOR_ACCENT,"查看状态  >");
    lv_obj_add_event_cb(button,on_cloud_open,LV_EVENT_CLICKED,NULL);
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
