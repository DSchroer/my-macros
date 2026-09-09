#include <pebble.h>

#define GOAL_PROTEIN 200
#define GOAL_CARBS   560
#define GOAL_FAT     100
// Tenths of a liter: 4.2 L ~= 115 oz baseline (half body weight at 230 lb) + training losses
#define GOAL_WATER   42
#define GOAL_KCAL    ((GOAL_PROTEIN + GOAL_CARBS) * 4 + GOAL_FAT * 9)

enum { MACRO_PROTEIN = 0, MACRO_CARBS, MACRO_FAT, MACRO_WATER, MACRO_COUNT };

enum {
  KEY_PROTEIN = 1,
  KEY_CARBS   = 2,
  KEY_FAT     = 3,
  KEY_DAY     = 4,
  KEY_WATER   = 5,
};

static const char *MACRO_NAMES[MACRO_COUNT] = {"Protein", "Carbs", "Fat", "Water"};
static const char *MACRO_UNITS[MACRO_COUNT] = {"g", "g", "g", "L"};
static const int MACRO_GOALS[MACRO_COUNT] = {GOAL_PROTEIN, GOAL_CARBS, GOAL_FAT, GOAL_WATER};
static const int MACRO_STEPS[MACRO_COUNT] = {5, 10, 5, 5};
static const int MACRO_DEFAULT_ADD[MACRO_COUNT] = {25, 50, 10, 5};

// Water is stored in tenths of a liter; everything else in whole grams.
static void format_qty(int m, int val, char *buf, size_t len) {
  if (m == MACRO_WATER) {
    snprintf(buf, len, "%d.%d", val / 10, val % 10);
  } else {
    snprintf(buf, len, "%d", val);
  }
}

static int s_totals[MACRO_COUNT];
static int s_last_macro = -1;
static int s_last_amount = 0;

static Window *s_main_window;
static Layer *s_dash_layer;

static Window *s_menu_window;
static SimpleMenuLayer *s_menu_layer;
static SimpleMenuItem s_menu_items[MACRO_COUNT];
static SimpleMenuSection s_menu_section;
static char s_menu_subtitles[MACRO_COUNT][24];

static Window *s_amount_window;
static TextLayer *s_amount_title_layer;
static TextLayer *s_amount_value_layer;
static TextLayer *s_amount_hint_layer;
static char s_amount_text[16];
static char s_amount_hint_text[64];
static int s_sel_macro;
static int s_amount;
static bool s_fine_mode;

static GColor macro_color(int m) {
  switch (m) {
    case MACRO_PROTEIN: return PBL_IF_COLOR_ELSE(GColorRed, GColorBlack);
    case MACRO_CARBS:   return PBL_IF_COLOR_ELSE(GColorBlueMoon, GColorBlack);
    case MACRO_WATER:   return PBL_IF_COLOR_ELSE(GColorVividCerulean, GColorBlack);
    default:            return PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorBlack);
  }
}

static int total_kcal(void) {
  return (s_totals[MACRO_PROTEIN] + s_totals[MACRO_CARBS]) * 4
       + s_totals[MACRO_FAT] * 9;
}

static int today_key(void) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  return (t->tm_year + 1900) * 10000 + (t->tm_mon + 1) * 100 + t->tm_mday;
}

static void save_state(void) {
  persist_write_int(KEY_PROTEIN, s_totals[MACRO_PROTEIN]);
  persist_write_int(KEY_CARBS, s_totals[MACRO_CARBS]);
  persist_write_int(KEY_FAT, s_totals[MACRO_FAT]);
  persist_write_int(KEY_WATER, s_totals[MACRO_WATER]);
  persist_write_int(KEY_DAY, today_key());
}

static void load_state(void) {
  s_totals[MACRO_PROTEIN] = persist_read_int(KEY_PROTEIN);
  s_totals[MACRO_CARBS] = persist_read_int(KEY_CARBS);
  s_totals[MACRO_FAT] = persist_read_int(KEY_FAT);
  s_totals[MACRO_WATER] = persist_read_int(KEY_WATER);
}

static void check_day_rollover(void) {
  if (persist_exists(KEY_DAY) && persist_read_int(KEY_DAY) != today_key()) {
    for (int m = 0; m < MACRO_COUNT; m++) {
      s_totals[m] = 0;
    }
    s_last_macro = -1;
    save_state();
  }
}

// ---- Main dashboard window ----

static void dash_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  const int pad = 8;
  const int row_h = (bounds.size.h - 26) / (MACRO_COUNT + 1);

  for (int row = 0; row < MACRO_COUNT + 1; row++) {
    const char *name;
    int val, goal;
    GColor color;
    char value[24];

    if (row == 0) {
      name = "Calories";
      val = total_kcal();
      goal = GOAL_KCAL;
      color = PBL_IF_COLOR_ELSE(GColorIslamicGreen, GColorBlack);
      snprintf(value, sizeof(value), "%d/%d", val, goal);
    } else {
      int m = row - 1;
      name = MACRO_NAMES[m];
      val = s_totals[m];
      goal = MACRO_GOALS[m];
      color = macro_color(m);
      char v[12], g[12];
      format_qty(m, val, v, sizeof(v));
      format_qty(m, goal, g, sizeof(g));
      snprintf(value, sizeof(value), "%s/%s %s", v, g, MACRO_UNITS[m]);
    }

    int y = 2 + row * row_h;
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, name, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                       GRect(pad, y, 80, 22), GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentLeft, NULL);
    graphics_draw_text(ctx, value, fonts_get_system_font(FONT_KEY_GOTHIC_18),
                       GRect(80, y, bounds.size.w - 80 - pad, 22),
                       GTextOverflowModeTrailingEllipsis, GTextAlignmentRight, NULL);

    GRect bar = GRect(pad, y + row_h - 16, bounds.size.w - 2 * pad, 10);
    graphics_context_set_fill_color(ctx, PBL_IF_COLOR_ELSE(GColorLightGray, GColorLightGray));
    graphics_fill_rect(ctx, bar, 5, GCornersAll);

    int fill_w = goal > 0 ? (bar.size.w * val) / goal : 0;
    bool over = fill_w > bar.size.w;
    if (over) {
      fill_w = bar.size.w;
      color = PBL_IF_COLOR_ELSE(GColorDarkCandyAppleRed, GColorBlack);
    }
    if (fill_w >= 5) {
      graphics_context_set_fill_color(ctx, color);
      graphics_fill_rect(ctx, GRect(bar.origin.x, bar.origin.y, fill_w, bar.size.h),
                         5, GCornersAll);
    }
  }

  graphics_context_set_text_color(ctx, PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack));
  graphics_draw_text(ctx, "SELECT: add   hold: undo",
                     fonts_get_system_font(FONT_KEY_GOTHIC_14),
                     GRect(0, bounds.size.h - 24, bounds.size.w, 20),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

static void main_select_handler(ClickRecognizerRef recognizer, void *context) {
  window_stack_push(s_menu_window, true);
}

static void main_undo_handler(ClickRecognizerRef recognizer, void *context) {
  if (s_last_macro < 0) {
    return;
  }
  s_totals[s_last_macro] -= s_last_amount;
  if (s_totals[s_last_macro] < 0) {
    s_totals[s_last_macro] = 0;
  }
  s_last_macro = -1;
  save_state();
  layer_mark_dirty(s_dash_layer);
  vibes_short_pulse();
}

static void main_click_config(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, main_select_handler);
  window_long_click_subscribe(BUTTON_ID_SELECT, 500, main_undo_handler, NULL);
}

static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  s_dash_layer = layer_create(layer_get_bounds(window_layer));
  layer_set_update_proc(s_dash_layer, dash_update_proc);
  layer_add_child(window_layer, s_dash_layer);
}

static void main_window_appear(Window *window) {
  check_day_rollover();
  layer_mark_dirty(s_dash_layer);
}

static void main_window_unload(Window *window) {
  layer_destroy(s_dash_layer);
}

// ---- Macro picker menu ----

static void menu_refresh(void) {
  for (int m = 0; m < MACRO_COUNT; m++) {
    char v[12], g[12];
    format_qty(m, s_totals[m], v, sizeof(v));
    format_qty(m, MACRO_GOALS[m], g, sizeof(g));
    snprintf(s_menu_subtitles[m], sizeof(s_menu_subtitles[m]), "%s / %s %s",
             v, g, MACRO_UNITS[m]);
  }
  if (s_menu_layer) {
    layer_mark_dirty(simple_menu_layer_get_layer(s_menu_layer));
  }
}

static void menu_select_callback(int index, void *context) {
  s_sel_macro = index;
  s_amount = MACRO_DEFAULT_ADD[index];
  window_stack_push(s_amount_window, true);
}

static void menu_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  for (int m = 0; m < MACRO_COUNT; m++) {
    s_menu_items[m] = (SimpleMenuItem) {
      .title = MACRO_NAMES[m],
      .subtitle = s_menu_subtitles[m],
      .callback = menu_select_callback,
    };
  }
  s_menu_section = (SimpleMenuSection) {
    .title = "Add macro",
    .items = s_menu_items,
    .num_items = MACRO_COUNT,
  };
  menu_refresh();
  s_menu_layer = simple_menu_layer_create(bounds, window, &s_menu_section, 1, NULL);
  layer_add_child(window_layer, simple_menu_layer_get_layer(s_menu_layer));
}

static void menu_window_appear(Window *window) {
  menu_refresh();
}

static void menu_window_unload(Window *window) {
  simple_menu_layer_destroy(s_menu_layer);
  s_menu_layer = NULL;
}

// ---- Amount picker ----

static int amount_step(void) {
  return s_fine_mode ? 1 : MACRO_STEPS[s_sel_macro];
}

static void amount_update_text(void) {
  const char *unit = MACRO_UNITS[s_sel_macro];
  char amt[12], step[12], alt_step[12];
  format_qty(s_sel_macro, s_amount, amt, sizeof(amt));
  format_qty(s_sel_macro, amount_step(), step, sizeof(step));
  format_qty(s_sel_macro, s_fine_mode ? MACRO_STEPS[s_sel_macro] : 1,
             alt_step, sizeof(alt_step));
  snprintf(s_amount_text, sizeof(s_amount_text), "+%s %s", amt, unit);
  text_layer_set_text(s_amount_value_layer, s_amount_text);
  snprintf(s_amount_hint_text, sizeof(s_amount_hint_text),
           "UP/DOWN \xC2\xB1%s%s   SELECT log\nhold SELECT for \xC2\xB1%s%s",
           step, unit, alt_step, unit);
  text_layer_set_text(s_amount_hint_layer, s_amount_hint_text);
}

static void amount_up_handler(ClickRecognizerRef recognizer, void *context) {
  s_amount += amount_step();
  amount_update_text();
}

static void amount_down_handler(ClickRecognizerRef recognizer, void *context) {
  int step = amount_step();
  if (s_amount - step >= 1) {
    s_amount -= step;
  }
  amount_update_text();
}

static void amount_toggle_fine_handler(ClickRecognizerRef recognizer, void *context) {
  s_fine_mode = !s_fine_mode;
  amount_update_text();
}

static void amount_select_handler(ClickRecognizerRef recognizer, void *context) {
  check_day_rollover();
  s_totals[s_sel_macro] += s_amount;
  s_last_macro = s_sel_macro;
  s_last_amount = s_amount;
  save_state();
  layer_mark_dirty(s_dash_layer);
  vibes_short_pulse();
  // Return straight to the dashboard
  window_stack_remove(s_menu_window, false);
  window_stack_pop(true);
}

static void amount_click_config(void *context) {
  window_single_repeating_click_subscribe(BUTTON_ID_UP, 150, amount_up_handler);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 150, amount_down_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, amount_select_handler);
  window_long_click_subscribe(BUTTON_ID_SELECT, 500, amount_toggle_fine_handler, NULL);
}

static void amount_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  s_amount_title_layer = text_layer_create(GRect(0, 18, bounds.size.w, 34));
  text_layer_set_font(s_amount_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_alignment(s_amount_title_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_amount_title_layer));

  s_amount_value_layer = text_layer_create(GRect(0, 80, bounds.size.w, 48));
  text_layer_set_font(s_amount_value_layer, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD));
  text_layer_set_text_alignment(s_amount_value_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_amount_value_layer));

  s_amount_hint_layer = text_layer_create(GRect(0, bounds.size.h - 46, bounds.size.w, 40));
  text_layer_set_font(s_amount_hint_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_amount_hint_layer, GTextAlignmentCenter);
  layer_add_child(window_layer, text_layer_get_layer(s_amount_hint_layer));
}

static void amount_window_appear(Window *window) {
  s_fine_mode = false;
  text_layer_set_text(s_amount_title_layer, MACRO_NAMES[s_sel_macro]);
  text_layer_set_text_color(s_amount_title_layer, macro_color(s_sel_macro));
  amount_update_text();
}

static void amount_window_unload(Window *window) {
  text_layer_destroy(s_amount_title_layer);
  text_layer_destroy(s_amount_value_layer);
  text_layer_destroy(s_amount_hint_layer);
}

// ---- App lifecycle ----

static void init(void) {
  load_state();
  check_day_rollover();

  s_main_window = window_create();
  window_set_click_config_provider(s_main_window, main_click_config);
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load = main_window_load,
    .appear = main_window_appear,
    .unload = main_window_unload,
  });

  s_menu_window = window_create();
  window_set_window_handlers(s_menu_window, (WindowHandlers) {
    .load = menu_window_load,
    .appear = menu_window_appear,
    .unload = menu_window_unload,
  });

  s_amount_window = window_create();
  window_set_click_config_provider(s_amount_window, amount_click_config);
  window_set_window_handlers(s_amount_window, (WindowHandlers) {
    .load = amount_window_load,
    .appear = amount_window_appear,
    .unload = amount_window_unload,
  });

  window_stack_push(s_main_window, true);
}

static void deinit(void) {
  save_state();
  window_destroy(s_amount_window);
  window_destroy(s_menu_window);
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
