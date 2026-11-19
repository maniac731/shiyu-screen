#include "ui_controller.h"

#include <stdbool.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "lvgl.h"
#include "misc/cache/instance/lv_image_cache.h"
#include "fonts/shiyu_fonts.h"
#include "ui_theme.h"

#define UI_FONT_BODY (&shiyu_font_24)
#define UI_FONT_TITLE (&shiyu_font_28)

typedef enum { MODE_ACTIVE, MODE_AMBIENT } app_mode_t;
typedef enum {
    SCREEN_HOME,
    SCREEN_INVENTORY,
    SCREEN_FOOD_DETAIL,
    SCREEN_MEAL,
    SCREEN_SETTINGS,
    SCREEN_UPLOAD,
} active_screen_t;
typedef enum { FOOD_TOMATO, FOOD_MILK } food_item_t;

static const food_item_t FOOD_ITEM_TOMATO = FOOD_TOMATO;
static const food_item_t FOOD_ITEM_MILK = FOOD_MILK;

typedef struct {
    app_mode_t mode;
    active_screen_t active_screen;
    lv_obj_t *home;
    lv_obj_t *inventory;
    lv_obj_t *food_detail;
    lv_obj_t *meal;
    lv_obj_t *settings;
    lv_obj_t *ambient;
    lv_obj_t *upload;
    lv_obj_t *inventory_context;
    lv_obj_t *inventory_milk;
    lv_obj_t *food_detail_name;
    lv_obj_t *food_detail_quantity;
    lv_obj_t *food_detail_status;
    lv_obj_t *ambient_image;
    lv_obj_t *ambient_title;
    lv_obj_t *ambient_hint;
    lv_obj_t *settings_upload_status;
    lv_obj_t *upload_status;
    bool inventory_expiry_only;
    shiyu_ui_action_cb_t start_upload;
    shiyu_ui_action_cb_t stop_upload;
} ui_state_t;

static const char *TAG = "shiyu_ui";
static ui_state_t s_ui;

static void create_inventory(void);
static void create_food_detail(void);
static void create_meal(void);
static void create_upload(void);
static void show_inventory(bool expiry_only);
static const char *get_active_screen_name(active_screen_t screen);

static void log_memory(const char *stage)
{
    ESP_LOGI(TAG, "Memory %s: internal=%u largest=%u psram=%u", stage,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
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
        case SCREEN_UPLOAD:
            return s_ui.upload;
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
        case SCREEN_UPLOAD:
            if (s_ui.upload == NULL) create_upload();
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
        case SCREEN_UPLOAD:
            object = s_ui.upload;
            s_ui.upload = NULL;
            s_ui.upload_status = NULL;
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
        case SCREEN_UPLOAD:
            return "UPLOAD";
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
    s_ui.mode = MODE_AMBIENT;
    lv_screen_load(s_ui.ambient);
    ESP_LOGI(TAG, "State AMBIENT");
    if (previous_mode == MODE_ACTIVE) release_transient_screen(previous_screen);
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
    s_ui.inventory_expiry_only = expiry_only;
    ensure_active_screen(SCREEN_INVENTORY);
    if (s_ui.inventory_context != NULL) {
        lv_label_set_text(s_ui.inventory_context,
                          expiry_only ? "查看临期食材 [DEMO]" : "查看家里的食材 [DEMO]");
    }
    if (s_ui.inventory_milk != NULL) {
        if (expiry_only) {
            lv_obj_add_flag(s_ui.inventory_milk, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(s_ui.inventory_milk, LV_OBJ_FLAG_HIDDEN);
        }
    }
    show_active(SCREEN_INVENTORY);
}

static void on_inventory(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) show_inventory(false);
}

static void on_expiry(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) show_inventory(true);
}

static void on_food_detail(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    ensure_active_screen(SCREEN_FOOD_DETAIL);
    const food_item_t *item = lv_event_get_user_data(event);
    if (item != NULL && *item == FOOD_MILK) {
        lv_label_set_text(s_ui.food_detail_name, "牛奶");
        lv_label_set_text(s_ui.food_detail_quantity, "已有 1");
        lv_label_set_text(s_ui.food_detail_status, "已保存");
    } else {
        lv_label_set_text(s_ui.food_detail_name, "番茄");
        lv_label_set_text(s_ui.food_detail_quantity, "已有 3");
        lv_label_set_text(s_ui.food_detail_status, "临期提醒");
    }
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

static void on_upload_placeholder(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    ensure_active_screen(SCREEN_UPLOAD);
    lv_label_set_text(s_ui.upload_status, "正在开启 Wi-Fi");
    show_active(SCREEN_UPLOAD);
    if (s_ui.start_upload != NULL) s_ui.start_upload();
}

static void on_upload_close(lv_event_t *event)
{
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    if (s_ui.stop_upload != NULL) s_ui.stop_upload();
    lv_label_set_text(s_ui.settings_upload_status, "需要时才开启 Wi-Fi");
    show_active(SCREEN_SETTINGS);
}

static void on_state_timer(lv_timer_t *timer)
{
    LV_UNUSED(timer);
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
    make_label(s_ui.home, "[DEMO] 此页面", 680, 79,
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
    make_label(meal, "家中已有 3 种所需食材", 32, 198, SHIYU_COLOR_SECONDARY, UI_FONT_BODY);
    lv_obj_t *meal_button = make_button(meal, 32, 252, 220, 46,
                                        SHIYU_COLOR_ACCENT, "查看建议");
    lv_obj_add_event_cb(meal_button, on_meal, LV_EVENT_CLICKED, NULL);

    lv_obj_t *expiry = make_card(s_ui.home, 650, 150, 326, 214, SHIYU_COLOR_WARNING);
    lv_obj_add_flag(expiry, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(expiry, on_expiry, LV_EVENT_CLICKED, NULL);
    make_label(expiry, "临期提醒", 28, 26, SHIYU_COLOR_SECONDARY, UI_FONT_BODY);
    make_label(expiry, "2 种食材", 28, 76, SHIYU_COLOR_PRIMARY, UI_FONT_TITLE);
    make_label(expiry, "牛奶、番茄", 28, 138, SHIYU_COLOR_PRIMARY, UI_FONT_BODY);

    lv_obj_t *inventory = make_card(s_ui.home, 650, 388, 326, 92, SHIYU_COLOR_ACCENT);
    lv_obj_add_flag(inventory, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(inventory, on_inventory, LV_EVENT_CLICKED, NULL);
    make_label(inventory, "家中共有 18 种食材", 24, 32, SHIYU_COLOR_PRIMARY, UI_FONT_BODY);
    make_label(s_ui.home, "60 秒无操作后进入环境模式", 48, 542,
               SHIYU_COLOR_SECONDARY, UI_FONT_BODY);
}

static void create_inventory(void)
{
    s_ui.inventory = lv_obj_create(NULL);
    prepare_screen(s_ui.inventory, SHIYU_COLOR_BACKGROUND);
    lv_obj_t *back = make_button(s_ui.inventory, 40, 32, 132, 56,
                                 SHIYU_COLOR_SURFACE, "<  返回");
    lv_obj_add_event_cb(back, on_back_home, LV_EVENT_CLICKED, NULL);
    make_label(s_ui.inventory, "家里的食材", 48, 124,
               SHIYU_COLOR_PRIMARY, UI_FONT_TITLE);
    s_ui.inventory_context = make_label(s_ui.inventory, "查看家里的食材 [DEMO]", 48, 174,
                                        SHIYU_COLOR_SECONDARY, UI_FONT_BODY);

    lv_obj_t *tomato = make_card(s_ui.inventory, 48, 228, 440, 148,
                                 SHIYU_COLOR_WARNING);
    lv_obj_add_flag(tomato, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(tomato, on_food_detail, LV_EVENT_CLICKED,
                        (void *)&FOOD_ITEM_TOMATO);
    make_label(tomato, "番茄", 28, 24, SHIYU_COLOR_PRIMARY, UI_FONT_TITLE);
    make_label(tomato, "临期提醒", 28, 76, SHIYU_COLOR_SECONDARY, UI_FONT_BODY);
    make_label(tomato, "查看食材", 284, 76, SHIYU_COLOR_PRIMARY, UI_FONT_BODY);

    s_ui.inventory_milk = make_card(s_ui.inventory, 512, 228, 464, 148,
                                    SHIYU_COLOR_SURFACE);
    lv_obj_add_flag(s_ui.inventory_milk, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_ui.inventory_milk, on_food_detail, LV_EVENT_CLICKED,
                        (void *)&FOOD_ITEM_MILK);
    make_label(s_ui.inventory_milk, "牛奶", 28, 24,
               SHIYU_COLOR_PRIMARY, UI_FONT_TITLE);
    make_label(s_ui.inventory_milk, "已有", 28, 76,
               SHIYU_COLOR_SECONDARY, UI_FONT_BODY);
    make_label(s_ui.inventory_milk, "查看食材", 308, 76,
               SHIYU_COLOR_PRIMARY, UI_FONT_BODY);

    make_label(s_ui.inventory, "[DEMO]  手机管理食材", 48, 430,
               SHIYU_COLOR_SECONDARY, UI_FONT_BODY);
    make_label(s_ui.inventory, "60 秒无操作后进入环境模式", 48, 542,
               SHIYU_COLOR_SECONDARY, UI_FONT_BODY);
    ESP_LOGI(TAG, "Created transient INVENTORY");
    log_memory("inventory-created");
}

static void create_food_detail(void)
{
    s_ui.food_detail = lv_obj_create(NULL);
    prepare_screen(s_ui.food_detail, SHIYU_COLOR_BACKGROUND);
    lv_obj_t *back = make_button(s_ui.food_detail, 40, 32, 132, 56,
                                 SHIYU_COLOR_SURFACE, "<  返回");
    lv_obj_add_event_cb(back, on_back_inventory, LV_EVENT_CLICKED, NULL);
    make_label(s_ui.food_detail, "查看食材 [DEMO]", 48, 124,
               SHIYU_COLOR_PRIMARY, UI_FONT_TITLE);

    lv_obj_t *detail = make_card(s_ui.food_detail, 48, 194, 928, 240,
                                 SHIYU_COLOR_SURFACE);
    s_ui.food_detail_name = make_label(detail, "番茄", 32, 28,
                                       SHIYU_COLOR_PRIMARY, UI_FONT_TITLE);
    s_ui.food_detail_quantity = make_label(detail, "已有 3", 32, 84,
                                           SHIYU_COLOR_SECONDARY, UI_FONT_BODY);
    s_ui.food_detail_status = make_label(detail, "临期提醒", 32, 138,
                                         SHIYU_COLOR_PRIMARY, UI_FONT_BODY);
    make_label(detail, "手机管理食材", 636, 138, SHIYU_COLOR_SECONDARY, UI_FONT_BODY);

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
    make_label(recommendation, "家中已有 3 种所需食材", 32, 86,
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

static void create_settings(void)
{
    s_ui.settings = lv_obj_create(NULL);
    prepare_screen(s_ui.settings, SHIYU_COLOR_BACKGROUND);
    lv_obj_t *back = make_button(s_ui.settings, 40, 32, 132, 56,
                                 SHIYU_COLOR_SURFACE, "<  返回");
    lv_obj_add_event_cb(back, on_back_home, LV_EVENT_CLICKED, NULL);
    make_label(s_ui.settings, "设置", 48, 124, SHIYU_COLOR_PRIMARY, UI_FONT_TITLE);
    make_label(s_ui.settings, "手机与屏幕", 48, 174, SHIYU_COLOR_SECONDARY, UI_FONT_BODY);

    lv_obj_t *upload = make_card(s_ui.settings, 48, 210, 928, 120, SHIYU_COLOR_ACCENT);
    make_label(upload, "手机照片管理", 28, 22, SHIYU_COLOR_PRIMARY, UI_FONT_TITLE);
    s_ui.settings_upload_status = make_label(upload, "需要时才开启 Wi-Fi", 28, 64,
                                             SHIYU_COLOR_PRIMARY, UI_FONT_BODY);
    lv_obj_t *upload_button = make_button(upload, 700, 30, 180, 58,
                                          SHIYU_COLOR_SURFACE, "开始传图");
    lv_obj_add_event_cb(upload_button, on_upload_placeholder, LV_EVENT_CLICKED, NULL);

    lv_obj_t *ambient = make_card(s_ui.settings, 48, 350, 450, 120, SHIYU_COLOR_SURFACE);
    make_label(ambient, "环境模式", 28, 20, SHIYU_COLOR_PRIMARY, UI_FONT_TITLE);
    make_label(ambient, "60 秒无操作后进入", 28, 62, SHIYU_COLOR_SECONDARY, UI_FONT_BODY);
    make_label(ambient, "已开启", 354, 45, SHIYU_COLOR_PRIMARY, UI_FONT_BODY);

    lv_obj_t *night = make_card(s_ui.settings, 522, 350, 454, 120, SHIYU_COLOR_SURFACE);
    make_label(night, "夜间息屏 [PLANNED]", 28, 20, SHIYU_COLOR_PRIMARY, UI_FONT_TITLE);
    make_label(night, "23:00  -  07:00", 28, 62,
               SHIYU_COLOR_SECONDARY, UI_FONT_BODY);
    make_label(night, "下一项", 354, 45, SHIYU_COLOR_SECONDARY, UI_FONT_BODY);
    make_label(s_ui.settings, "此页面不会自动进入环境模式", 48, 542,
               SHIYU_COLOR_SECONDARY, UI_FONT_BODY);
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

static void create_upload(void)
{
    s_ui.upload = lv_obj_create(NULL);
    prepare_screen(s_ui.upload, SHIYU_COLOR_BACKGROUND);
    make_label(s_ui.upload, "手机照片上传", 48, 42, SHIYU_COLOR_SECONDARY, UI_FONT_TITLE);
    make_label(s_ui.upload, "连接手机", 48, 96, SHIYU_COLOR_PRIMARY, UI_FONT_BODY);
    make_label(s_ui.upload, "Wi-Fi", 48, 180, SHIYU_COLOR_SECONDARY, UI_FONT_BODY);
    make_label(s_ui.upload, "ShiYu-Screen-A02", 48, 218,
               SHIYU_COLOR_PRIMARY, UI_FONT_BODY);
    make_label(s_ui.upload, "密码", 48, 276, SHIYU_COLOR_SECONDARY, UI_FONT_BODY);
    make_label(s_ui.upload, "shiyu2026", 48, 314,
               SHIYU_COLOR_PRIMARY, UI_FONT_BODY);
    make_label(s_ui.upload, "浏览器打开  http://192.168.4.1", 48, 386,
               SHIYU_COLOR_PRIMARY, UI_FONT_BODY);
    s_ui.upload_status = make_label(s_ui.upload, "正在开启 Wi-Fi", 560, 210,
                                    SHIYU_COLOR_PRIMARY, UI_FONT_BODY);
    lv_obj_t *close = make_button(s_ui.upload, 760, 500, 216, 62,
                                  SHIYU_COLOR_SURFACE, "关闭传图");
    lv_obj_add_event_cb(close, on_upload_close, LV_EVENT_CLICKED, NULL);
    ESP_LOGI(TAG, "Created transient UPLOAD");
    log_memory("upload-created");
}

void shiyu_ui_create(shiyu_ui_action_cb_t start_upload, shiyu_ui_action_cb_t stop_upload)
{
    s_ui.start_upload = start_upload;
    s_ui.stop_upload = stop_upload;
    create_home();
    create_settings();
    create_ambient();
    lv_timer_create(on_state_timer, SHIYU_STATE_CHECK_PERIOD_MS, NULL);
    show_active(SCREEN_HOME);
    log_memory("resident-ui-created");
}

void shiyu_ui_set_portal_available(bool available)
{
    if (s_ui.settings_upload_status != NULL) {
        lv_label_set_text(s_ui.settings_upload_status,
                          available ? "需要时才开启 Wi-Fi" : "传图暂不可用");
    }
}

void shiyu_ui_set_upload_state(const char *status)
{
    if (status == NULL) return;
    if (s_ui.upload_status != NULL) lv_label_set_text(s_ui.upload_status, status);
}

void shiyu_ui_set_ambient_photo(const char *lvgl_path)
{
    if (lvgl_path == NULL || s_ui.ambient_image == NULL) return;
    lv_image_cache_drop(lvgl_path);
    lv_image_set_src(s_ui.ambient_image, lvgl_path);
    lv_obj_remove_flag(s_ui.ambient_image, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ui.ambient_title, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ui.ambient_hint, LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(s_ui.ambient_image);
}
