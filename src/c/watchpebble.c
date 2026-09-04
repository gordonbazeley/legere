#include <pebble.h>
#include <ctype.h>

static Window *s_window;
static Layer *s_canvas_layer;

// true  = a wrist shake forced an exact reading -> month drawn red
// false = the minute is floored to a multiple of 5 (passive) -> month drawn blue
static bool s_exact = false;

// Date line: Michroma, same family as the digits. Subset to [A-Z0-9 ].
static GFont s_date_font;
static bool s_date_font_custom;

// The hour/minute digits are pre-rendered bitmaps (a font can't be rasterised
// big enough on-watch). One sprite sheet of 10 fixed-width slots, sliced into
// per-digit sub-bitmaps at load. See tools/gen-digits.sh.
static GBitmap *s_sheet;
static GBitmap *s_digit[10];
static int s_slot_w;
static int s_slot_h;

// What the last repaint actually put on screen, so a refresh that would change
// nothing can skip the redraw entirely.
static int s_drawn_hour = -1;
static int s_drawn_min = -1;

static int s_digit_band_top; // top y of the digit grid
static int s_date_top;       // y of the date row
static int s_date_h;
static int s_pad;
static int s_usable_w;

#define PAD PBL_IF_ROUND_ELSE(18, 6)
#define DIGIT_GAP 4            // px between the hour and minute rows
#define DIGIT_BAND_BOT_GAP 4  // px between the minute row and the date row

static GSize prv_measure(const char *text, GFont font) {
  return graphics_text_layout_get_content_size(text, font, GRect(0, 0, 400, 300),
                                                GTextOverflowModeFill, GTextAlignmentLeft);
}

static int prv_floor5(int m) { return (m / 5) * 5; }

// Current wall-clock hour, in the 12/24h form the face shows.
static int prv_display_hour(const struct tm *t) {
  int h = t->tm_hour;
  if (!clock_is_24h_style()) {
    h = h % 12;
    if (h == 0) h = 12;
  }
  return h;
}

// Refresh to the exact minute (and flag it red) unless that's already on screen.
static void prv_refresh_to_exact(void) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  if (s_exact && prv_display_hour(t) == s_drawn_hour && t->tm_min == s_drawn_min) {
    return;  // nothing would change — don't spend a repaint
  }
  s_exact = true;
  layer_mark_dirty(s_canvas_layer);
}

// Recolour a digit by poking the sheet's palette — the digits are white ink over
// a graded alpha edge, so tint every visible entry to `c` while keeping its
// alpha. No second bitmap; sub-bitmaps share the parent's palette, so call this
// immediately before each blit.
static void prv_set_ink(GBitmap *b, GColor c) {
  int n;
  switch (gbitmap_get_format(b)) {
    case GBitmapFormat1BitPalette: n = 2; break;
    case GBitmapFormat2BitPalette: n = 4; break;
    case GBitmapFormat4BitPalette: n = 16; break;
    default: return;  // not palettised (8-bit) — leave it white
  }
  GColor *pal = gbitmap_get_palette(b);
  if (!pal) return;
  for (int i = 0; i < n; i++) {
    if (pal[i].a != 0) {
      pal[i].r = c.r;
      pal[i].g = c.g;
      pal[i].b = c.b;
    }
  }
}

static void prv_draw_cell(GContext *ctx, GRect box, const char *text, GFont font,
                          GTextAlignment align, GColor color) {
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, text, font, box, GTextOverflowModeFill, align, NULL);
}

static void prv_canvas_update_proc(Layer *layer, GContext *ctx) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  bool h24 = clock_is_24h_style();
  int hour = prv_display_hour(t);
  int disp_min = s_exact ? t->tm_min : prv_floor5(t->tm_min);

  char hour_str[4], min_str[4], dow[4], dom[4], mon[4];
  snprintf(hour_str, sizeof hour_str, h24 ? "%02d" : "%d", hour);
  snprintf(min_str, sizeof min_str, "%02d", disp_min);
  strftime(dow, sizeof dow, "%a", t);
  strftime(dom, sizeof dom, "%d", t);
  strftime(mon, sizeof mon, "%b", t);
  for (char *c = dow; *c; c++) *c = toupper((unsigned char)*c);
  for (char *c = mon; *c; c++) *c = toupper((unsigned char)*c);

  // Hour and minute as a 2x2 grid: one digit per quadrant, right-aligned in its
  // column, the two rows packed tight (only DIGIT_GAP between them) and the
  // whole block centred in the space above the date.
  if (s_sheet) {
    int col_w = s_usable_w / 2;
    int block_h = 2 * s_slot_h + DIGIT_GAP;
    int block_top = s_digit_band_top +
                    (s_date_top - DIGIT_BAND_BOT_GAP - s_digit_band_top - block_h) / 2;
    int dv[4] = { hour / 10, hour % 10, disp_min / 10, disp_min % 10 };
    bool blank_hour_tens = !h24 && hour < 10;

    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    for (int i = 0; i < 4; i++) {
      if (i == 0 && blank_hour_tens) continue;
      int col = i & 1, row = i >> 1;
      int x = s_pad + col * col_w + col_w - s_slot_w;  // right-aligned in column
      int y = block_top + row * (s_slot_h + DIGIT_GAP);
      prv_set_ink(s_digit[dv[i]], row == 0 ? GColorLightGray : GColorWhite);
      graphics_draw_bitmap_in_rect(ctx, s_digit[dv[i]], GRect(x, y, s_slot_w, s_slot_h));
    }
  } else {
    // Resource-load failure only: fall back to plain text.
    GFont f = fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);
    prv_draw_cell(ctx, GRect(s_pad, s_digit_band_top, s_usable_w, 48),
                  hour_str, f, GTextAlignmentRight, GColorWhite);
    prv_draw_cell(ctx, GRect(s_pad, s_digit_band_top + 50, s_usable_w, 48),
                  min_str, f, GTextAlignmentRight, GColorWhite);
  }

  // Date row: weekday left, day-of-month centre, month right — three L/C/R
  // strings over one shared box (Michroma is too wide to force equal thirds and
  // stay legible). Month colour signals freshness: red = exact (just refreshed),
  // blue = passive 5-minute reading.
  // Light blue: dark blue is unreadable on the transflective LCD without light.
  GColor mon_color = s_exact ? GColorRed : GColorPictonBlue;
  int date_w = PBL_IF_ROUND_ELSE(132, s_usable_w);
  GRect date_box = GRect(s_pad + (s_usable_w - date_w) / 2, s_date_top, date_w, s_date_h);
  prv_draw_cell(ctx, date_box, dow, s_date_font, GTextAlignmentLeft, GColorWhite);
  prv_draw_cell(ctx, date_box, dom, s_date_font, GTextAlignmentCenter, GColorWhite);
  prv_draw_cell(ctx, date_box, mon, s_date_font, GTextAlignmentRight, mon_color);

  s_drawn_hour = hour;
  s_drawn_min = disp_min;
}

static void prv_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  // The OS wakes the app every minute for its own clock; we only repaint on the
  // 5-minute grid — and just hourly while the user's Quiet Time is on (asleep or
  // in a meeting). :00 and midnight are multiples of both, so hour and date
  // rollover stay covered.
  int step = quiet_time_is_active() ? 60 : 5;
  if (tick_time->tm_min % step == 0) {
    s_exact = false;
    layer_mark_dirty(s_canvas_layer);
  }
}

static void prv_backlight_handler(bool on) {
  // Backlight on = the user lit the screen to look (button in the dark, or
  // shake/flick-to-light). Passive listener on OS behaviour — costs nothing.
  if (on) {
    prv_refresh_to_exact();
  }
}

static void prv_tap_handler(AccelAxisType axis, int32_t direction) {
  // Daylight refresh: a wrist flick / tap, when the backlight wouldn't fire
  // because it's bright out. The guard in prv_refresh_to_exact() keeps a walk
  // from repainting the face on every stride.
  prv_refresh_to_exact();
}

static void prv_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  window_set_background_color(window, GColorBlack);

  s_pad = PAD;
  s_usable_w = bounds.size.w - 2 * s_pad;

  // Round screens clip their corners — keep content well off the top/bottom.
  int top_margin = PBL_IF_ROUND_ELSE(14, 6);
  int bot_margin = PBL_IF_ROUND_ELSE(34, 2);

#if defined(PBL_PLATFORM_EMERY)
  s_date_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DATE_18));
#else
  s_date_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DATE_14));
#endif
  s_date_font_custom = (s_date_font != NULL);
  if (!s_date_font) s_date_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  s_date_h = prv_measure("WO", s_date_font).h + 2;
  s_date_top = bounds.size.h - bot_margin - s_date_h;

#if defined(PBL_PLATFORM_EMERY)
  s_sheet = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_DIGITS_LG);   // 200x228 rect
#else
  s_sheet = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_DIGITS);      // gabbro, 180 round
#endif
  if (s_sheet) {
    GSize ss = gbitmap_get_bounds(s_sheet).size;
    s_slot_w = ss.w / 10;
    s_slot_h = ss.h;
    for (int i = 0; i < 10; i++) {
      s_digit[i] = gbitmap_create_as_sub_bitmap(
          s_sheet, GRect(i * s_slot_w, 0, s_slot_w, s_slot_h));
    }
  }

  s_digit_band_top = top_margin;

  s_canvas_layer = layer_create(bounds);
  layer_set_update_proc(s_canvas_layer, prv_canvas_update_proc);
  layer_add_child(window_layer, s_canvas_layer);
}

static void prv_window_unload(Window *window) {
  layer_destroy(s_canvas_layer);
  for (int i = 0; i < 10; i++) {
    if (s_digit[i]) gbitmap_destroy(s_digit[i]);
  }
  if (s_sheet) gbitmap_destroy(s_sheet);
  if (s_date_font_custom) fonts_unload_custom_font(s_date_font);
}

static void prv_init(void) {
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload,
  });
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, prv_tick_handler);
  backlight_service_subscribe(prv_backlight_handler);
  accel_tap_service_subscribe(prv_tap_handler);
}

static void prv_deinit(void) {
  accel_tap_service_unsubscribe();
  backlight_service_unsubscribe();
  tick_timer_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
