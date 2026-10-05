#include <pebble.h>
#include <ctype.h>
#include <locale.h>
#include <string.h>

static Window *s_window;
// Two disjoint sibling layers so a repaint of one never re-runs the other's
// update proc: the time digits change on every minute tick; the date row
// only on a date rollover or Night colour boundary.
static Layer *s_digits_layer;
static Layer *s_date_layer;

// User setting (phone settings page): dims the face to a low-luminance red
// during a wall-clock hour window, to cut blue/white light hitting the eyes
// at night (red wavelengths suppress melatonin least). Off by default —
// nothing changes unless the user opts in.
static bool s_night_enabled = false;
static int s_night_start = 22;  // hour, 0-23, inclusive
static int s_night_end = 7;     // hour, 0-23, exclusive; start > end wraps midnight
#define PERSIST_KEY_NIGHT_ENABLED 196
#define PERSIST_KEY_NIGHT_START 197
#define PERSIST_KEY_NIGHT_END 198

// Dim red/amber ink, used in place of the white/gray palette during the
// night window. Bumped one step up Pebble's quantized red ramp from the
// original DarkCandyAppleRed/BulgarianRose pair — that was too dark to read
// comfortably.
#define NIGHT_INK GColorRed
#define NIGHT_INK_DIM GColorDarkCandyAppleRed

static int prv_clamp_hour(int h) {
  if (h < 0) return 0;
  if (h > 23) return 23;
  return h;
}

static bool prv_is_night(int hour) {
  if (!s_night_enabled) return false;
  if (s_night_start == s_night_end) return false;  // degenerate window = never
  if (s_night_start < s_night_end) return hour >= s_night_start && hour < s_night_end;
  return hour >= s_night_start || hour < s_night_end;  // wraps past midnight
}

// Date line: Quantico Bold (20px on gabbro, 24/20px on emery). Glyph subset in package.json covers A-Z, digits,
// space, '.', and the Latin-1 accented capitals À-Þ excluding × (+ ß) for FR/DE/ES/IT/PT/NL —
// Quantico's cmap has no gaps in that set (checked against the full Latin-1
// accented block, not just what these locales use).
static GFont s_date_font;
static bool s_date_font_custom;

// The hour/minute digits are pre-rendered bitmaps (a font can't be rasterised
// big enough on-watch). One sprite sheet of 10 fixed-width slots, sliced into
// per-digit sub-bitmaps at load. See tools/gen-digits.sh.
static GBitmap *s_sheet;
static GBitmap *s_digit[10];
static GBitmap *s_outline_sheet;
static GBitmap *s_outline_digit[10];
static int s_slot_w;
static int s_slot_h;
static int s_outline_slot_w;
static int s_outline_slot_h;

static int s_digit_band_top; // top y of the digit grid
static int s_date_top;       // y of the date row
static int s_date_h;
static int s_pad;
static int s_usable_w;
static int s_block_top;      // top y of the digit block, computed once from the above

// Latest wall-clock time, refreshed once per minute by prv_tick_handler.
// Both update procs read this instead of calling time()/localtime() again on
// every repaint — a repaint never needs finer than minute resolution anyway.
static struct tm s_now;

#define PAD PBL_IF_ROUND_ELSE(20, 6)
// px between the hour and minute rows; negative on emery = the minute row
// deliberately overlaps the hour row (dense stacked effect, hour drawn dark);
// gabbro overlaps by the same ~19% of the slot height.
#define DIGIT_GAP PBL_IF_ROUND_ELSE(-14, -20)
#define DIGIT_BAND_BOT_GAP PBL_IF_ROUND_ELSE(6, 5)  // px between the minute row and the date row

static GSize prv_measure(const char *text, GFont font) {
  return graphics_text_layout_get_content_size(text, font, GRect(0, 0, 400, 300),
                                                GTextOverflowModeFill, GTextAlignmentLeft);
}

// Current wall-clock hour, in the 12/24h form the face shows.
static int prv_display_hour(const struct tm *t) {
  int h = t->tm_hour;
  if (!clock_is_24h_style()) {
    h = h % 12;
    if (h == 0) h = 12;
  }
  return h;
}

static void prv_inbox_received_handler(DictionaryIterator *iterator, void *context) {
  // "Night colour" setting: dims the face to red during a hour window.
  // NightEnabled is a plain flag; NightStart/NightEnd only arrive alongside
  // it (settings.html always sends all three together), so reading them here
  // rather than gating on presence keeps this in one block.
  Tuple *night_enabled_tuple = dict_find(iterator, MESSAGE_KEY_NightEnabled);
  if (night_enabled_tuple) {
    bool changed = false;
    bool night_enabled = night_enabled_tuple->value->int32 != 0;
    if (s_night_enabled != night_enabled) {
      s_night_enabled = night_enabled;
      persist_write_bool(PERSIST_KEY_NIGHT_ENABLED, s_night_enabled);
      changed = true;
    }

    Tuple *start_tuple = dict_find(iterator, MESSAGE_KEY_NightStart);
    if (start_tuple) {
      int night_start = prv_clamp_hour(start_tuple->value->int32);
      if (s_night_start != night_start) {
        s_night_start = night_start;
        persist_write_int(PERSIST_KEY_NIGHT_START, s_night_start);
        changed = true;
      }
    }
    Tuple *end_tuple = dict_find(iterator, MESSAGE_KEY_NightEnd);
    if (end_tuple) {
      int night_end = prv_clamp_hour(end_tuple->value->int32);
      if (s_night_end != night_end) {
        s_night_end = night_end;
        persist_write_int(PERSIST_KEY_NIGHT_END, s_night_end);
        changed = true;
      }
    }
    if (changed) {
      layer_mark_dirty(s_digits_layer);
      layer_mark_dirty(s_date_layer);
    }
  }
}

// Recolour a digit by poking the sheet's palette — tint every visible palette
// entry to `c`. No second bitmap; sub-bitmaps share the parent's palette, so
// call this once before drawing a row of the same colour.
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

// Cached date-row strings: strftime + the uppercase pass only re-run on a date
// change, not on every passive repaint. -1 = not built yet.
static char s_dow[12], s_dom[4], s_mon[12];
static int s_str_mday = -1;

// Uppercase ASCII + the Latin-1 supplement (UTF-8 0xC3 0xA0..0xBE, minus ÷),
// which covers every accented letter FR/DE/ES/IT/PT/NL put in a weekday/month
// abbreviation. Plain toupper() only touches ASCII and would leave "mär" ->
// "mÄr". ß (0xC3 0x9F) has no single-char uppercase, so it's left alone.
static void prv_utf8_upper(char *s) {
  for (unsigned char *c = (unsigned char *)s; *c; c++) {
    if (*c < 0x80) {
      *c = toupper(*c);
    } else if (*c == 0xC3 && c[1] >= 0xA0 && c[1] <= 0xBE && c[1] != 0xB7) {
      c[1] -= 0x20;  // lowercase -> uppercase within the Latin-1 block
      c++;
    }
  }
}

#if defined(PBL_PLATFORM_EMERY)
#define DATE_CELL_GAP 6   // min px wanted between the weekday / day / month cells

// The date row wants 24px, but the widest localised rows (FR/ES: "SEPT." plus an
// accented weekday) overrun s_usable_w at 24 and the three L/C/R cells collide.
// Rather than hardcode which locales are wide, measure every weekday + month
// abbreviation this locale actually produces and only drop to 20px if 24 won't
// fit. One-time at window load — not on any repaint path.
static GFont prv_pick_date_font(void) {
  GFont big = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DATE_24));
  if (!big) return fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DATE_20));

  struct tm probe;
  memset(&probe, 0, sizeof probe);
  char buf[12];
  int wide_dow = 0, wide_mon = 0;
  for (int i = 0; i < 12; i++) {
    probe.tm_mon = i;                       // %b reads tm_mon directly
    strftime(buf, sizeof buf, "%b", &probe);
    prv_utf8_upper(buf);
    int w = prv_measure(buf, big).w;
    if (w > wide_mon) wide_mon = w;
  }
  for (int i = 0; i < 7; i++) {
    probe.tm_wday = i;                      // %a reads tm_wday directly
    strftime(buf, sizeof buf, "%a", &probe);
    prv_utf8_upper(buf);
    int w = prv_measure(buf, big).w;
    if (w > wide_dow) wide_dow = w;
  }
  int need = wide_dow + prv_measure("88", big).w + wide_mon + 2 * DATE_CELL_GAP;
  if (need <= s_usable_w) return big;

  fonts_unload_custom_font(big);
  return fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DATE_20));
}
#endif

static void prv_digits_update_proc(Layer *layer, GContext *ctx) {
  struct tm *t = &s_now;

  bool h24 = clock_is_24h_style();
  int hour = prv_display_hour(t);
  int disp_min = t->tm_min;

  // Hour and minute stacked: two digits per row, the pair centred as a unit so a
  // narrow "1" doesn't shove the block sideways. On emery DIGIT_GAP is negative,
  // so the minute row overlaps and paints over the foot of the dark-grey hour
  // row — a deliberate dense stack that buys bigger digits. The whole block is
  // centred in the space above the date. On gabbro the block sits at the
  // vertical mid-screen (the widest part of the circle), so it spans the full
  // usable width and the rows keep a normal positive gap.
  //
  // At night the minute row switches to a pre-rendered hollow red outline
  // instead of the solid fill used by day. The hour is always exact and never
  // flickers, so it stays solid (dark grey by day, red at night) either way.
  bool night = prv_is_night(t->tm_hour);
  GColor hour_ink = night ? NIGHT_INK : GColorDarkGray;
  GColor min_ink = night ? NIGHT_INK : GColorWhite;
  if (s_sheet) {
    int dv[4] = { hour / 10, hour % 10, disp_min / 10, disp_min % 10 };
    bool blank_hour_tens = !h24 && hour < 10;

    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    for (int row = 0; row < 2; row++) {
      int n = (row == 0 && blank_hour_tens) ? 1 : 2;  // digits shown on this row
      int row_x = s_pad + (s_usable_w - n * s_slot_w) / 2;
      int y = s_block_top + row * (s_slot_h + DIGIT_GAP);
      bool outline_min = night && row == 1 && s_outline_sheet;
      prv_set_ink(outline_min ? s_outline_digit[0] : s_digit[0],
                  row == 0 ? hour_ink : min_ink);
      for (int k = 0; k < n; k++) {
        int idx = row * 2 + (n == 1 ? 1 : k);
        GRect digit_rect = GRect(row_x + k * s_slot_w, y, s_slot_w, s_slot_h);
        if (outline_min) {
          graphics_draw_bitmap_in_rect(ctx, s_outline_digit[dv[idx]],
              GRect(digit_rect.origin.x - 4, digit_rect.origin.y - 4,
                    s_outline_slot_w, s_outline_slot_h));
        } else {
          graphics_draw_bitmap_in_rect(ctx, s_digit[dv[idx]], digit_rect);
        }
      }
    }
  } else {
    // Resource-load failure only: fall back to plain text.
    char hour_str[4], min_str[4];
    snprintf(hour_str, sizeof hour_str, h24 ? "%02d" : "%d", hour);
    snprintf(min_str, sizeof min_str, "%02d", disp_min);
    GFont f = fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);
    prv_draw_cell(ctx, GRect(s_pad, s_digit_band_top, s_usable_w, 48),
                  hour_str, f, GTextAlignmentRight, GColorWhite);
    prv_draw_cell(ctx, GRect(s_pad, s_digit_band_top + 50, s_usable_w, 48),
                  min_str, f, GTextAlignmentRight, GColorWhite);
  }
}

// Whole date line is drawn in this one colour. White reads cleanly on
// the unlit transflective LCD without adding a second signal colour; swapped
// for NIGHT_INK during the night window (see prv_is_night).
static GColor prv_date_color(int hour) { return prv_is_night(hour) ? NIGHT_INK : GColorWhite; }

// Date row: weekday left, day-of-month centre, month right — three L/C/R strings
// over one shared box (the font is too wide to force equal thirds and stay
// legible). Whole row is one colour — white, or NIGHT_INK during the night
// window — no freshness signal here. The box is layer-relative (y = 0):
// s_date_layer is framed at s_date_top.
static void prv_date_update_proc(Layer *layer, GContext *ctx) {
  struct tm *t = &s_now;

  if (t->tm_mday != s_str_mday) {
    strftime(s_dow, sizeof s_dow, "%a", t);
    strftime(s_dom, sizeof s_dom, "%d", t);
    strftime(s_mon, sizeof s_mon, "%b", t);
    prv_utf8_upper(s_dow);
    prv_utf8_upper(s_mon);
    s_str_mday = t->tm_mday;
  }

  GColor color = prv_date_color(t->tm_hour);
  int date_w = PBL_IF_ROUND_ELSE(160, s_usable_w);  // round: narrower than usable so the row clears the arc
  GRect date_box = GRect(s_pad + (s_usable_w - date_w) / 2, 0, date_w, s_date_h);
  prv_draw_cell(ctx, date_box, s_dow, s_date_font, GTextAlignmentLeft, color);
  prv_draw_cell(ctx, date_box, s_dom, s_date_font, GTextAlignmentCenter, color);
  prv_draw_cell(ctx, date_box, s_mon, s_date_font, GTextAlignmentRight, color);
}

static void prv_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  bool night_changed = prv_is_night(s_now.tm_hour) != prv_is_night(tick_time->tm_hour);
  s_now = *tick_time;
  layer_mark_dirty(s_digits_layer);
  if (tick_time->tm_mday != s_str_mday || night_changed) {
    layer_mark_dirty(s_date_layer);
  }
}

static void prv_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  window_set_background_color(window, GColorBlack);

  s_night_enabled = persist_exists(PERSIST_KEY_NIGHT_ENABLED) &&
                    persist_read_bool(PERSIST_KEY_NIGHT_ENABLED);
  if (persist_exists(PERSIST_KEY_NIGHT_START)) {
    s_night_start = prv_clamp_hour(persist_read_int(PERSIST_KEY_NIGHT_START));
  }
  if (persist_exists(PERSIST_KEY_NIGHT_END)) {
    s_night_end = prv_clamp_hour(persist_read_int(PERSIST_KEY_NIGHT_END));
  }

  s_pad = PAD;
  s_usable_w = bounds.size.w - 2 * s_pad;

  // Round screens clip their corners — keep content well off the top/bottom.
  // Rect (Emery) top_margin doubles as the literal margin above the digit
  // block (see s_date_top below) — round keeps its bot_margin-anchored,
  // bezel-tuned layout untouched.
  int top_margin = PBL_IF_ROUND_ELSE(28, 5);

#if defined(PBL_PLATFORM_EMERY)
  s_date_font = prv_pick_date_font();   // 24px, or 20px where the locale is too wide
#else
  s_date_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DATE_20));
#endif
  s_date_font_custom = (s_date_font != NULL);
  if (!s_date_font) s_date_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  s_date_h = prv_measure("WO", s_date_font).h + 2;

#if defined(PBL_PLATFORM_EMERY)
  s_sheet = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_DIGITS_LG);   // 200x228 rect
  s_outline_sheet = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_DIGITS_LG_OUTLINE);
#else
  s_sheet = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_DIGITS);      // gabbro, 180 round
  s_outline_sheet = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_DIGITS_OUTLINE);
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
  if (s_outline_sheet) {
    GSize ss = gbitmap_get_bounds(s_outline_sheet).size;
    s_outline_slot_w = ss.w / 10;
    s_outline_slot_h = ss.h;
    for (int i = 0; i < 10; i++) {
      s_outline_digit[i] = gbitmap_create_as_sub_bitmap(
          s_outline_sheet, GRect(i * s_outline_slot_w, 0, s_outline_slot_w, s_outline_slot_h));
    }
  }

#if defined(PBL_PLATFORM_EMERY)
  // Stack top-down with the 3 fixed 5px gaps, rather than centering the
  // block in whatever space bot_margin leaves — so all 3 gaps actually are
  // 5px instead of splitting leftover slack between them.
  s_date_top = top_margin + 2 * s_slot_h + DIGIT_GAP + DIGIT_BAND_BOT_GAP;
#else
  int bot_margin = PBL_IF_ROUND_ELSE(38, 2);  // round only; bezel-tuned
  s_date_top = bounds.size.h - bot_margin - s_date_h;
#endif

  s_digit_band_top = top_margin;

  {
    int block_h = 2 * s_slot_h + DIGIT_GAP;
    s_block_top = s_digit_band_top + PBL_IF_ROUND_ELSE(2, 0) +
                  (s_date_top - DIGIT_BAND_BOT_GAP - s_digit_band_top - block_h) / 2;
  }

  time_t now = time(NULL);
  s_now = *localtime(&now);

  // Digits above, date below, split at s_date_top so the two dirty regions never
  // intersect — marking one never re-runs the other's update proc.
  s_digits_layer = layer_create(GRect(0, 0, bounds.size.w, s_date_top));
  layer_set_update_proc(s_digits_layer, prv_digits_update_proc);
  layer_add_child(window_layer, s_digits_layer);

  s_date_layer = layer_create(GRect(0, s_date_top, bounds.size.w, s_date_h));
  layer_set_update_proc(s_date_layer, prv_date_update_proc);
  layer_add_child(window_layer, s_date_layer);
}

static void prv_window_unload(Window *window) {
  layer_destroy(s_digits_layer);
  layer_destroy(s_date_layer);
  for (int i = 0; i < 10; i++) {
    if (s_digit[i]) gbitmap_destroy(s_digit[i]);
    if (s_outline_digit[i]) gbitmap_destroy(s_outline_digit[i]);
  }
  if (s_sheet) gbitmap_destroy(s_sheet);
  if (s_outline_sheet) gbitmap_destroy(s_outline_sheet);
  if (s_date_font_custom) fonts_unload_custom_font(s_date_font);
}

static void prv_init(void) {
  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_window_load,
    .unload = prv_window_unload,
  });
  window_stack_push(s_window, true);

  // Latin-script locales only (FR/DE/ES/IT/PT/NL). Makes strftime %a/%b return
  // localised weekday/month abbreviations; prv_utf8_upper handles the casing.
  setlocale(LC_ALL, i18n_get_system_locale());

  tick_timer_service_subscribe(MINUTE_UNIT, prv_tick_handler);

  // Inbox only — the phone settings page pushes the night-colour settings;
  // the watch never sends anything back, so the outbox is 0.
  app_message_register_inbox_received(prv_inbox_received_handler);
  app_message_open(app_message_inbox_size_maximum(), 0);
}

static void prv_deinit(void) {
  app_message_deregister_callbacks();
  tick_timer_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
