#include <pebble.h>
#include <ctype.h>

#define STEP_GOAL 12500
#define OVERLAY_DURATION_MS 5000

static Window *s_window;
static Layer *s_canvas_layer;
static bool s_show_overlay = false;
static AppTimer *s_hide_timer;

// Largest-to-smallest; picked at draw time to fill each container.
static const char *const LECO_FONTS[] = {
  FONT_KEY_LECO_42_NUMBERS,
  FONT_KEY_LECO_38_BOLD_NUMBERS,
  FONT_KEY_LECO_36_BOLD_NUMBERS,
  FONT_KEY_LECO_32_BOLD_NUMBERS,
  FONT_KEY_LECO_28_LIGHT_NUMBERS,
  FONT_KEY_LECO_26_BOLD_NUMBERS_AM_PM,
  FONT_KEY_LECO_20_BOLD_NUMBERS,
};
#define LECO_FONTS_COUNT (int)(sizeof(LECO_FONTS) / sizeof(LECO_FONTS[0]))

// LECO has no letter glyphs, so weekday/month use this bold family instead.
static const char *const GOTHIC_FONTS[] = {
  FONT_KEY_GOTHIC_28_BOLD,
  FONT_KEY_GOTHIC_24_BOLD,
  FONT_KEY_GOTHIC_18_BOLD,
  FONT_KEY_GOTHIC_14_BOLD,
};
#define GOTHIC_FONTS_COUNT (int)(sizeof(GOTHIC_FONTS) / sizeof(GOTHIC_FONTS[0]))

static int prv_days_in_month(int month /* 1-12 */, int year /* full year */) {
  static const int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
  return (month == 2 && leap) ? 29 : days[month - 1];
}

static void prv_draw_bar(GContext *ctx, GRect col, float pct, GColor8 bar_color) {
  int fill_h = (int)(col.size.h * pct / 100.0f);
  GRect fill = GRect(col.origin.x, col.origin.y + col.size.h - fill_h, col.size.w, fill_h);
  graphics_context_set_fill_color(ctx, bar_color);
  graphics_fill_rect(ctx, fill, 0, GCornerNone);
}

static GSize prv_measure(const char *text, GFont font) {
  return graphics_text_layout_get_content_size(text, font, GRect(0, 0, 400, 100),
                                                GTextOverflowModeFill, GTextAlignmentCenter);
}

static const char DIGIT_CHARSET[] = "0123456789";
static const char ALNUM_CHARSET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

// Picks the largest font from `keys` whose glyphs — for every character in
// `charset` — fit within `avail_w`, and whose height times `max_lines` fits
// within `avail_h_total`. Sizing against the full charset (not the string
// currently on screen) keeps the result stable: it doesn't change size
// minute to minute as different digits/letters appear, and columns sharing
// a font end up pixel-identical in size. Writes the chosen font's tight
// glyph height to `*out_char_h`, for use as an exact (no dead-space) line
// pitch by the caller.
static GFont prv_fit_font_for_charset(int avail_w, int avail_h_total, int max_lines,
                                       const char *charset, const char *const *keys,
                                       int n_keys, int *out_char_h) {
  GFont chosen = fonts_get_system_font(keys[n_keys - 1]);
  int chosen_h = 0;
  for (int i = 0; i < n_keys; i++) {
    GFont f = fonts_get_system_font(keys[i]);
    int max_w = 0, max_h = 0;
    for (const char *c = charset; *c; c++) {
      char buf[2] = { *c, '\0' };
      GSize sz = prv_measure(buf, f);
      if (sz.w > max_w) max_w = sz.w;
      if (sz.h > max_h) max_h = sz.h;
    }
    chosen = f;
    chosen_h = max_h;
    if (max_w <= avail_w && max_h * max_lines <= avail_h_total) {
      break;
    }
  }
  *out_char_h = chosen_h;
  return chosen;
}

static void prv_draw_centered(GContext *ctx, GRect cell, const char *text, GFont font) {
  GSize sz = prv_measure(text, font);
  GRect r = GRect(cell.origin.x, cell.origin.y + (cell.size.h - sz.h) / 2, cell.size.w, sz.h);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, text, font, r, GTextOverflowModeFill, GTextAlignmentCenter, NULL);
}

// One character per line, packed tight from the top of the column (no
// inter-line gap), using a font already chosen by prv_fit_font_for_charset.
static void prv_draw_stacked_chars(GContext *ctx, GRect col, const char *str, int len,
                                    int row_h, GFont font) {
  graphics_context_set_text_color(ctx, GColorWhite);
  int y = col.origin.y + 1;
  for (int i = 0; i < len; i++) {
    char buf[2] = { str[i], '\0' };
    GRect line = GRect(col.origin.x, y, col.size.w, row_h);
    graphics_draw_text(ctx, buf, font, line, GTextOverflowModeFill, GTextAlignmentCenter, NULL);
    y += row_h;
  }
}

static void prv_canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);

  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  int iso_weekday = ((t->tm_wday + 6) % 7) + 1;  // Mon=1..Sun=7
  int day_of_month = t->tm_mday;
  int days_in_month = prv_days_in_month(t->tm_mon + 1, t->tm_year + 1900);
  int month = t->tm_mon + 1;
  int hour = t->tm_hour;
  int minute = t->tm_min;
  int steps = PBL_IF_HEALTH_ELSE((int)health_service_sum_today(HealthMetricStepCount), 0);
  if (steps > 99999) steps = 99999;

  float weekday_pct = (iso_weekday / 7.0f) * 100.0f;
  float dom_pct = (day_of_month / (float)days_in_month) * 100.0f;
  float month_pct = (month / 12.0f) * 100.0f;
  float hour_pct = (hour / 24.0f) * 100.0f;
  float minute_pct = (minute / 60.0f) * 100.0f;
  float steps_pct = (steps / (float)STEP_GOAL) * 100.0f;
  if (steps_pct > 100.0f) steps_pct = 100.0f;

  // grid-template-columns: 1fr 1fr 1fr 6fr 1fr over 10 units
  int unit = bounds.size.w / 10;
  int x0 = 0;
  int x1 = x0 + unit;
  int x2 = x1 + unit;
  int x3 = x2 + unit;
  int x4 = x3 + unit * 6;
  int last_w = bounds.size.w - x4;
  int half_h = bounds.size.h / 2;

  GRect weekday_col = GRect(x0, 0, unit, bounds.size.h);
  GRect dom_col = GRect(x1, 0, unit, bounds.size.h);
  GRect month_col = GRect(x2, 0, unit, bounds.size.h);
  GRect hour_cell = GRect(x3, 0, unit * 6, half_h);
  GRect minute_cell = GRect(x3, half_h, unit * 6, bounds.size.h - half_h);
  GRect steps_col = GRect(x4, 0, last_w, bounds.size.h);

  prv_draw_bar(ctx, weekday_col, weekday_pct, GColorBulgarianRose);
  prv_draw_bar(ctx, dom_col, dom_pct, GColorDarkGreen);
  prv_draw_bar(ctx, month_col, month_pct, GColorOxfordBlue);
  prv_draw_bar(ctx, hour_cell, hour_pct, GColorArmyGreen);
  prv_draw_bar(ctx, minute_cell, minute_pct, GColorImperialPurple);
  prv_draw_bar(ctx, steps_col, steps_pct, GColorMidnightGreen);

  if (s_show_overlay) {
    char weekday_str[4], month_str[4], dom_str[3], hour_str[3], minute_str[3], steps_str[6];
    strftime(weekday_str, sizeof(weekday_str), "%a", t);
    strftime(month_str, sizeof(month_str), "%b", t);
    for (char *c = weekday_str; *c; c++) *c = toupper((unsigned char)*c);
    for (char *c = month_str; *c; c++) *c = toupper((unsigned char)*c);
    snprintf(dom_str, sizeof(dom_str), "%02d", day_of_month);
    snprintf(hour_str, sizeof(hour_str), "%02d", hour);
    snprintf(minute_str, sizeof(minute_str), "%02d", minute);
    snprintf(steps_str, sizeof(steps_str), "%05d", steps);

    // One shared Gothic font/size for all four narrow columns (driven by
    // the tightest fit: steps' 5 lines against the full A-Z0-9 charset), so
    // they're pixel-identical. Line pitch is the font's own tight glyph
    // height — no artificial slot padding, no dead space between lines.
    int narrow_avail_w = unit - 2;
    int char_h;
    GFont narrow_font = prv_fit_font_for_charset(narrow_avail_w, bounds.size.h - 2, 5,
                                                  ALNUM_CHARSET, GOTHIC_FONTS, GOTHIC_FONTS_COUNT,
                                                  &char_h);
    int big_char_h;
    GFont big_digit_font = prv_fit_font_for_charset(hour_cell.size.w - 2, hour_cell.size.h - 2, 1,
                                                     DIGIT_CHARSET, LECO_FONTS, LECO_FONTS_COUNT,
                                                     &big_char_h);

    prv_draw_stacked_chars(ctx, weekday_col, weekday_str, 3, char_h, narrow_font);
    prv_draw_stacked_chars(ctx, dom_col, dom_str, 2, char_h, narrow_font);
    prv_draw_stacked_chars(ctx, month_col, month_str, 3, char_h, narrow_font);
    prv_draw_stacked_chars(ctx, steps_col, steps_str, 5, char_h, narrow_font);
    prv_draw_centered(ctx, hour_cell, hour_str, big_digit_font);
    prv_draw_centered(ctx, minute_cell, minute_str, big_digit_font);
  }
}

static void prv_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  layer_mark_dirty(s_canvas_layer);
}

static void prv_hide_overlay_callback(void *context) {
  s_hide_timer = NULL;
  s_show_overlay = false;
  layer_mark_dirty(s_canvas_layer);
}

static void prv_tap_handler(AccelAxisType axis, int32_t direction) {
  s_show_overlay = true;
  layer_mark_dirty(s_canvas_layer);
  if (s_hide_timer) {
    app_timer_reschedule(s_hide_timer, OVERLAY_DURATION_MS);
  } else {
    s_hide_timer = app_timer_register(OVERLAY_DURATION_MS, prv_hide_overlay_callback, NULL);
  }
}

static void prv_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  window_set_background_color(window, GColorBlack);

  s_canvas_layer = layer_create(bounds);
  layer_set_update_proc(s_canvas_layer, prv_canvas_update_proc);
  layer_add_child(window_layer, s_canvas_layer);
}

static void prv_window_unload(Window *window) {
  layer_destroy(s_canvas_layer);
}

static void prv_init(void) {
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload,
  });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, prv_tick_handler);
  accel_tap_service_subscribe(prv_tap_handler);
}

static void prv_deinit(void) {
  if (s_hide_timer) {
    app_timer_cancel(s_hide_timer);
  }
  accel_tap_service_unsubscribe();
  tick_timer_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
