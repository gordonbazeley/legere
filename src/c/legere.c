#include <pebble.h>
#include <ctype.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>

static Window *s_window;
// Two disjoint sibling layers so a repaint of one never re-runs the other's
// update proc: the time digits change on every passive/forced repaint, the date
// row only on a date rollover.
static Layer *s_digits_layer;
static Layer *s_date_layer;

// true  = a wrist shake forced an exact reading (minute digits clean)
// false = the minute is floored to a multiple of 5 (passive, minute digits snow)
static bool s_exact = false;

// Date line: Michroma. Glyph subset in package.json covers A-Z, digits, space,
// '.', and the Latin-1 accented capitals (+ ß) for FR/DE/ES/IT/PT/NL.
static GFont s_date_font;
static bool s_date_font_custom;

// Whole date line is drawn in this one colour (no freshness signal on the date
// row any more — only the minute static carries that). Hardcoded white: reads
// cleanly on the unlit transflective LCD without adding a second signal colour.
#define DATE_COLOR GColorWhite

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

// Passive minutes are drawn as TV-static "snow" (see prv_staticify). A shake
// resolves them to a clean signal — but first runs a short "locking on" ramp:
// SHIMMER_FRAMES redraws SHIMMER_MS apart, each snowing a smaller fraction of
// the minute ink than the last, so the digits surface out of the noise like a
// tuner pulling a station in. The frame that lands on 0 is the clean render.
// The same ramp plays in reverse when the clock ticks past the locked minute
// (s_shimmer_out): the minute decays back into static instead of cutting out.
// HW-tune this (feel of the lock-on) alongside the values themselves.
#define SHIMMER_FRAMES 8
#define SHIMMER_MS 40
static int s_shimmer_left = 0;         // frames remaining in the ramp (SHIMMER_FRAMES..0)
static bool s_shimmer_out = false;     // true = ramp running snow-ward (lock lost), false = lock-on
static AppTimer *s_shimmer_timer = NULL;

// Ceiling on passive snow density, in permille of the minute-ink pixels — dialed
// back from a full 1000 (every pixel) so the passive face reads as static
// without being quite so agitated.
#define PASSIVE_SNOW_PERMILLE 350

// Snow density for the current frame, in permille of the minute-ink pixels:
// PASSIVE_SNOW_PERMILLE = passive / no signal, 0 = clean. A ramp in flight wins
// over s_exact: lock-on steps PASSIVE_SNOW_PERMILLE->0, lock-out steps the reverse.
static int prv_snow_permille(void) {
  if (s_shimmer_left > 0) {
    int snowed = s_shimmer_out ? SHIMMER_FRAMES - s_shimmer_left : s_shimmer_left;
    return snowed * PASSIVE_SNOW_PERMILLE / SHIMMER_FRAMES;
  }
  return s_exact ? 0 : PASSIVE_SNOW_PERMILLE;
}

static int s_digit_band_top; // top y of the digit grid
static int s_date_top;       // y of the date row
static int s_date_h;
static int s_pad;
static int s_usable_w;

#define PAD PBL_IF_ROUND_ELSE(20, 6)
// px between the hour and minute rows; negative on emery = the minute row
// deliberately overlaps the hour row (dense stacked effect, hour drawn dark).
#define DIGIT_GAP PBL_IF_ROUND_ELSE(6, -20)
#define DIGIT_BAND_BOT_GAP PBL_IF_ROUND_ELSE(6, 5)  // px between the minute row and the date row
#define MINUTE_ROW_SHIFT_X 15  // px the minute row is pulled left of the hour row's centred position

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

// Shake/tap-triggered-refresh log, kept per calendar day so it can be pulled
// to the phone and exported — instrumentation to check whether the 5-minute
// grid is worth keeping vs. just redrawing every minute.
//
// ponytail: DAYS_KEPT days ring-buffered in persist storage (56B/day, well
// under the persist quota). Bump it if more history is needed.
#define DAYS_KEPT 14
#define PERSIST_KEY_DAY_CURSOR 190          // int: index of "today"'s slot
#define PERSIST_KEY_DAY_BASE 200            // blobs: 200..200+DAYS_KEPT-1

typedef struct __attribute__((__packed__)) {
  int16_t year;         // full year, e.g. 2026; 0 = empty slot
  uint8_t mon;           // 1-12
  uint8_t mday;          // 1-31
  uint8_t shakes[24];    // trigger count per hour-of-day, capped at 60
  uint32_t quiet_mask;   // bit h set = Quiet Time was on during hour h
  // battery must stay last: a pre-battery 32-byte blob then reads back
  // year..quiet_mask exactly and leaves battery[] at its init value.
  uint8_t battery[24];   // charge_percent sampled in hour h; 0xFF = no sample
} DayRecord;

static int s_day_cursor = -1;  // -1 = not loaded from persist yet this run
static DayRecord s_today;

static void prv_persist_today(void) {
  persist_write_data(PERSIST_KEY_DAY_BASE + s_day_cursor, &s_today, sizeof(s_today));
}

// Loads/advances the day-record ring so s_today always matches the wall-clock
// date. Cheap to call from every tick — it only does work on an actual
// day change (or the first call after launch).
static void prv_ensure_today(const struct tm *t) {
  if (s_day_cursor < 0) {
    s_day_cursor = persist_exists(PERSIST_KEY_DAY_CURSOR) ? persist_read_int(PERSIST_KEY_DAY_CURSOR) : 0;
    memset(&s_today, 0, sizeof(s_today));
    memset(s_today.battery, 0xFF, sizeof s_today.battery);
    persist_read_data(PERSIST_KEY_DAY_BASE + s_day_cursor, &s_today, sizeof(s_today));
  }
  int year = t->tm_year + 1900, mon = t->tm_mon + 1, mday = t->tm_mday;
  if (s_today.year != year || s_today.mon != mon || s_today.mday != mday) {
    s_day_cursor = (s_day_cursor + 1) % DAYS_KEPT;
    persist_write_int(PERSIST_KEY_DAY_CURSOR, s_day_cursor);
    memset(&s_today, 0, sizeof(s_today));
    memset(s_today.battery, 0xFF, sizeof s_today.battery);
    s_today.year = year;
    s_today.mon = mon;
    s_today.mday = mday;
    prv_persist_today();
  }
}

static void prv_log_trigger(const struct tm *t) {
  prv_ensure_today(t);
  if (s_today.shakes[t->tm_hour] < 60) s_today.shakes[t->tm_hour]++;
  prv_persist_today();
  APP_LOG(APP_LOG_LEVEL_INFO, "shake-wake hour=%02d count=%d", t->tm_hour, s_today.shakes[t->tm_hour]);
}

// --- Phone export ------------------------------------------------------
// The phone's config page asks for the log (MESSAGE_KEY_RequestLog); we
// stream it back one day-record per AppMessage, paced by outbox_sent, then
// send MESSAGE_KEY_Done. See src/pkjs/index.js for the receiving side.

static int s_export_slot = -1;  // index of the last slot sent; -1 = nothing sent yet

static void prv_export_finish(void) {
  s_export_slot = -1;
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    dict_write_int32(iter, MESSAGE_KEY_Done, 1);
    dict_write_end(iter);
    app_message_outbox_send();
  }
}

static void prv_export_send_next(void) {
  for (int i = s_export_slot + 1; i < DAYS_KEPT; i++) {
    DayRecord rec;
    memset(&rec, 0, sizeof(rec));
    memset(rec.battery, 0xFF, sizeof rec.battery);
    persist_read_data(PERSIST_KEY_DAY_BASE + i, &rec, sizeof(rec));
    if (rec.year == 0) continue;  // slot never written

    DictionaryIterator *iter;
    if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;  // will retry on next tap/reopen
    dict_write_int32(iter, MESSAGE_KEY_Year, rec.year);
    dict_write_int32(iter, MESSAGE_KEY_Mon, rec.mon);
    dict_write_int32(iter, MESSAGE_KEY_Mday, rec.mday);
    dict_write_data(iter, MESSAGE_KEY_Shakes, rec.shakes, sizeof(rec.shakes));
    dict_write_data(iter, MESSAGE_KEY_Battery, rec.battery, sizeof(rec.battery));
    dict_write_int32(iter, MESSAGE_KEY_QuietMask, (int32_t)rec.quiet_mask);
    dict_write_end(iter);
    app_message_outbox_send();
    s_export_slot = i;
    return;
  }
  prv_export_finish();
}

static void prv_outbox_sent_handler(DictionaryIterator *iterator, void *context) {
  if (s_export_slot >= 0) prv_export_send_next();
}

static void prv_outbox_failed_handler(DictionaryIterator *iterator, AppMessageResult reason,
                                      void *context) {
  // ponytail: drop the export on failure rather than retrying — the phone
  // just re-requests it (it re-sends RequestLog each time settings opens).
  s_export_slot = -1;
}

static void prv_inbox_received_handler(DictionaryIterator *iterator, void *context) {
  if (dict_find(iterator, MESSAGE_KEY_RequestLog)) {
    s_export_slot = -1;
    prv_export_send_next();
  }
}

// One frame of the lock-on ramp: count down and repaint. prv_snow_permille()
// reads s_shimmer_left, so each frame snows less of the minute ink than the
// last (freshly randomised); the frame that lands on 0 renders it clean.
static void prv_shimmer_tick(void *context) {
  s_shimmer_timer = NULL;
  if (s_shimmer_left > 0) s_shimmer_left--;
  layer_mark_dirty(s_digits_layer);  // snow only touches the minute row
  if (s_shimmer_left > 0) {
    s_shimmer_timer = app_timer_register(SHIMMER_MS, prv_shimmer_tick, NULL);
  } else if (s_shimmer_out) {
    s_exact = false;  // lock-out ramp finished — minute row is passive static again
  }
}

// Wall-clock minute a manual refresh locked onto — the exact reading is only
// truthful until the clock ticks past it (see prv_tick_handler). -1 = none.
static int s_exact_hour = -1;
static int s_exact_min = -1;

// Refresh the minute row to the exact time unless it's already on screen.
static void prv_refresh_to_exact(void) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  if (s_exact && !s_shimmer_out &&
      prv_display_hour(t) == s_drawn_hour && t->tm_min == s_drawn_min) {
    return;  // nothing would change — don't spend a repaint
  }
  prv_log_trigger(t);
  s_exact = true;
  s_exact_hour = t->tm_hour;
  s_exact_min = t->tm_min;
  s_shimmer_out = false;
  s_shimmer_left = SHIMMER_FRAMES;
  if (s_shimmer_timer) app_timer_cancel(s_shimmer_timer);
  s_shimmer_timer = app_timer_register(SHIMMER_MS, prv_shimmer_tick, NULL);
  layer_mark_dirty(s_digits_layer);  // minute row: snow -> clean (date row is unaffected now)
}

// Recolour a digit by poking the sheet's palette — tint every visible palette
// entry to `c`. No second bitmap; sub-bitmaps share the parent's palette, so
// call this immediately before each blit.
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

// Snow palette: each solid minute-ink pixel is replaced by a random entry.
// White/LightGray only (the old mix) barely registers on the emery reflective
// LCD unlit — too little contrast. This spread runs white down to black, so the
// passive minutes read as a genuinely noisy, half-lost signal. Tune by editing
// the table: more Black = more "dropout" holes, more White = brighter static.
// ponytail: shared across emery + gabbro; re-check the gabbro round slot if the
// mix changes (it clamps fine today).
static const uint8_t SNOW[4] = {
  GColorWhiteARGB8, GColorLightGrayARGB8, GColorDarkGrayARGB8, GColorBlackARGB8,
};

// Overwrite the solid minute-ink pixels inside `r` with random SNOW "snow" — a
// no-signal look for the passive (floored) minutes. Works at the framebuffer
// level so it needs no offscreen bitmap: only fully-opaque white pixels (the
// glyph interiors, drawn white just above) are touched, so the anti-aliased
// glyph edges survive as a clean outline around the noise.
//
// `permille` (1..1000) is the fraction of ink pixels replaced this frame; the
// rest stay clean white. The lock-on ramp steps it down so the digits emerge
// from the noise. ponytail: two rand() calls per snowed pixel (gate + palette)
// vs one before — only on the shake path, a one-shot ~320 ms burst, not hot.
static void prv_staticify(GContext *ctx, GRect r, int permille) {
  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (!fb) return;
  GRect b = gbitmap_get_bounds(fb);
  int y1 = r.origin.y + r.size.h;
  for (int y = r.origin.y; y < y1; y++) {
    if (y < b.origin.y || y >= b.origin.y + b.size.h) continue;
    GBitmapDataRowInfo ri = gbitmap_get_data_row_info(fb, y);
    int x0 = r.origin.x < ri.min_x ? ri.min_x : r.origin.x;
    int x1 = r.origin.x + r.size.w;
    if (x1 > ri.max_x + 1) x1 = ri.max_x + 1;
    for (int x = x0; x < x1; x++) {
      if (ri.data[x] == GColorWhiteARGB8 &&
          (permille >= 1000 || (rand() % 1000) < permille)) {
        ri.data[x] = SNOW[rand() & 3];
      }
    }
  }
  graphics_release_frame_buffer(ctx, fb);
}

#define DATE_BOLD_PX 1   // faux-bold smear for the date row, in px (Michroma has one weight)

// Smears the glyph both ways so vertical and horizontal strokes thicken by the
// same amount — an x-only smear widens vertical strokes only, leaving
// horizontal ones at their original weight and looking uneven.
static void prv_draw_cell(GContext *ctx, GRect box, const char *text, GFont font,
                          GTextAlignment align, GColor color) {
  graphics_context_set_text_color(ctx, color);
  graphics_draw_text(ctx, text, font, box, GTextOverflowModeFill, align, NULL);
  for (int i = 1; i <= DATE_BOLD_PX; i++) {
    GRect bx = box;
    bx.origin.x += i;
    graphics_draw_text(ctx, text, font, bx, GTextOverflowModeFill, align, NULL);
    GRect by = box;
    by.origin.y += i;
    graphics_draw_text(ctx, text, font, by, GTextOverflowModeFill, align, NULL);
  }
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

// The date row wants 21px, but the widest localised rows (FR/ES: "SEPT." plus an
// accented weekday) overrun s_usable_w at 21 and the three L/C/R cells collide.
// Rather than hardcode which locales are wide, measure every weekday + month
// abbreviation this locale actually produces and only drop to 18px if 21 won't
// fit. One-time at window load — not on any repaint path.
static GFont prv_pick_date_font(void) {
  GFont big = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DATE_21));
  if (!big) return fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DATE_18));

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
  return fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DATE_18));
}
#endif

static void prv_digits_update_proc(Layer *layer, GContext *ctx) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  bool h24 = clock_is_24h_style();
  int hour = prv_display_hour(t);
  int disp_min = s_exact ? t->tm_min : prv_floor5(t->tm_min);

  // Hour and minute stacked: two digits per row, the pair centred as a unit so a
  // narrow "1" doesn't shove the block sideways. On emery DIGIT_GAP is negative,
  // so the minute row overlaps and paints over the foot of the dark-grey hour
  // row — a deliberate dense stack that buys bigger digits. The whole block is
  // centred in the space above the date. On gabbro the block sits at the
  // vertical mid-screen (the widest part of the circle), so it spans the full
  // usable width and the rows keep a normal positive gap.
  //
  // The minute row is drawn solid white, then (when the reading is passive, or
  // mid lock-on ramp) prv_staticify() replaces that white ink with snow — all of
  // it when passive, a shrinking fraction over the ramp. The hour is always
  // exact, so it stays a solid dark grey and never flickers.
  int snow_permille = prv_snow_permille();
  if (s_sheet) {
    int grid_w = s_usable_w;
    int grid_x = s_pad;
    int block_h = 2 * s_slot_h + DIGIT_GAP;
    int block_top = s_digit_band_top + PBL_IF_ROUND_ELSE(2, 0) +
                    (s_date_top - DIGIT_BAND_BOT_GAP - s_digit_band_top - block_h) / 2;
    int dv[4] = { hour / 10, hour % 10, disp_min / 10, disp_min % 10 };
    bool blank_hour_tens = !h24 && hour < 10;
    GRect min_rect = GRectZero;

    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    for (int row = 0; row < 2; row++) {
      int n = (row == 0 && blank_hour_tens) ? 1 : 2;  // digits shown on this row
      int row_x = grid_x + (grid_w - n * s_slot_w) / 2;
      if (row == 1) row_x -= MINUTE_ROW_SHIFT_X;  // slight offset from the hour row above
      int y = block_top + row * (s_slot_h + DIGIT_GAP);
      if (row == 1) min_rect = GRect(row_x, y, n * s_slot_w, s_slot_h);
      for (int k = 0; k < n; k++) {
        int idx = row * 2 + (n == 1 ? 1 : k);
        prv_set_ink(s_digit[dv[idx]], row == 0 ? GColorDarkGray : GColorWhite);
        graphics_draw_bitmap_in_rect(ctx, s_digit[dv[idx]],
                                     GRect(row_x + k * s_slot_w, y, s_slot_w, s_slot_h));
      }
    }
    if (snow_permille > 0) prv_staticify(ctx, min_rect, snow_permille);
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

  s_drawn_hour = hour;
  s_drawn_min = disp_min;
}

// Date row: weekday left, day-of-month centre, month right — three L/C/R strings
// over one shared box (Michroma is too wide to force equal thirds and stay
// legible). Whole row is DATE_COLOR — a constant mid blue, no freshness signal
// here. The box is layer-relative (y = 0): s_date_layer is framed at s_date_top.
static void prv_date_update_proc(Layer *layer, GContext *ctx) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  if (t->tm_mday != s_str_mday) {
    strftime(s_dow, sizeof s_dow, "%a", t);
    strftime(s_dom, sizeof s_dom, "%d", t);
    strftime(s_mon, sizeof s_mon, "%b", t);
    prv_utf8_upper(s_dow);
    prv_utf8_upper(s_mon);
    s_str_mday = t->tm_mday;
  }

  int date_w = PBL_IF_ROUND_ELSE(174, s_usable_w);  // round: narrower than usable so the row clears the arc
  GRect date_box = GRect(s_pad + (s_usable_w - date_w) / 2, 0, date_w, s_date_h);
  prv_draw_cell(ctx, date_box, s_dow, s_date_font, GTextAlignmentLeft, DATE_COLOR);
  prv_draw_cell(ctx, date_box, s_dom, s_date_font, GTextAlignmentCenter, DATE_COLOR);
  prv_draw_cell(ctx, date_box, s_mon, s_date_font, GTextAlignmentRight, DATE_COLOR);
}

// ponytail stopgap: the official Pebble app doesn't surface a Settings
// webview for sideloaded apps yet, so the AppMessage export above has no way
// to fire. Until it does, log each finished hour's row directly — same
// shape as the CSV — so `pebble logs` is a working export path today:
//   pebble logs --phone <ip> | tee watch.log
//   python3 tools/pebble-log-to-csv.py watch.log > shake-log.csv
static int s_logged_hour = -1;
static int s_logged_day_key = -1;  // year*10000 + mon*100 + mday

// Minute of the last passive (schedule-driven) repaint, so a button press in
// Quiet Time that lands in that same minute can be recognised as redundant.
static int s_sched_hour = -1;
static int s_sched_min = -1;

// year*10000 + mon*100 + mday for s_today — a comparable key for "did the
// date change", used both to detect rollover and to remember what was logged.
static int prv_day_key(void) {
  return s_today.year * 10000 + s_today.mon * 100 + s_today.mday;
}

static void prv_tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  // Log the hour that just finished, using s_today as it stood *before*
  // today's rollover below (so the last hour of a day still logs against
  // the correct outgoing date).
  int day_key = prv_day_key();
  if (s_logged_hour >= 0 && day_key != 0 &&
      (s_logged_hour != tick_time->tm_hour || s_logged_day_key != day_key)) {
    bool was_quiet = (s_today.quiet_mask & (1UL << s_logged_hour)) != 0;
    APP_LOG(APP_LOG_LEVEL_INFO, "row %02d/%02d/%04d,%02d,%s,%d,%d",
            s_today.mday, s_today.mon, s_today.year, s_logged_hour,
            was_quiet ? "yes" : "no", s_today.shakes[s_logged_hour],
            s_today.battery[s_logged_hour]);
  }

  // Every-minute tick doubles as the sampler for the current hour's Quiet
  // Time flag and battery level — last sample in the hour wins, which converges
  // to the right answer since neither flaps minute to minute.
  prv_ensure_today(tick_time);
  bool quiet = quiet_time_is_active();
  uint32_t bit = 1UL << tick_time->tm_hour;
  uint32_t new_mask = quiet ? (s_today.quiet_mask | bit) : (s_today.quiet_mask & ~bit);
  uint8_t new_bat = battery_state_service_peek().charge_percent;
  if (new_mask != s_today.quiet_mask ||
      s_today.battery[tick_time->tm_hour] != new_bat) {
    s_today.quiet_mask = new_mask;
    s_today.battery[tick_time->tm_hour] = new_bat;
    prv_persist_today();
  }
  s_logged_hour = tick_time->tm_hour;
  s_logged_day_key = prv_day_key();

  // A manual refresh shows the exact minute — but only that minute is true. Once
  // the clock ticks past it, play the lock-on ramp in reverse: the minute decays
  // back into static over ~320 ms rather than cutting out in one frame. The ramp
  // ends by clearing s_exact (prv_shimmer_tick). (Down to a second if you shook
  // at :59; a near-full minute if you shook at :00 — which is the point.)
  if (s_exact && !s_shimmer_out &&
      (tick_time->tm_hour != s_exact_hour || tick_time->tm_min != s_exact_min)) {
    s_shimmer_out = true;
    s_shimmer_left = SHIMMER_FRAMES;
    if (s_shimmer_timer) app_timer_cancel(s_shimmer_timer);
    s_shimmer_timer = app_timer_register(SHIMMER_MS, prv_shimmer_tick, NULL);
    layer_mark_dirty(s_digits_layer);  // minute row: clean -> ramp -> snow
  }

  // The OS wakes the app every minute for its own clock; we only repaint on the
  // 5-minute grid — and just hourly while the user's Quiet Time is on (asleep or
  // in a meeting). :00 and midnight are multiples of both, so hour and date
  // rollover stay covered.
  int step = quiet ? 60 : 5;
  if (tick_time->tm_min % step == 0) {
    s_exact = false;
    s_sched_hour = tick_time->tm_hour;
    s_sched_min = tick_time->tm_min;
    layer_mark_dirty(s_digits_layer);
    // Date row only moves on a date rollover — its colour is constant now.
    if (tick_time->tm_mday != s_str_mday) {
      layer_mark_dirty(s_date_layer);
    }
  }
}

static void prv_backlight_handler(bool on) {
  // Backlight on = the user lit the screen to look (button in the dark, or
  // shake/flick-to-light). Passive listener on OS behaviour — costs nothing.
  if (!on) return;

  // In Quiet Time, a button press (e.g. Back, exiting some other screen back
  // to the face) still wants the exact time — but if the passive hourly
  // repaint already landed in this same minute, forcing an exact/red redraw
  // now would just flash the screen for no visible change. Skip it.
  if (quiet_time_is_active()) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    if (t->tm_hour == s_sched_hour && t->tm_min == s_sched_min) return;
  }
  prv_refresh_to_exact();
}

static void prv_tap_handler(AccelAxisType axis, int32_t direction) {
  // Daylight refresh: a wrist flick / tap, when the backlight wouldn't fire
  // because it's bright out. The guard in prv_refresh_to_exact() keeps a walk
  // from repainting the face on every stride.
  //
  // Suppressed during Quiet Time — a sleeping wrist shouldn't relight the
  // face. The backlight/button path (prv_backlight_handler) stays live: a
  // deliberate button press in the dark still wants the exact time.
  if (quiet_time_is_active()) return;
  prv_refresh_to_exact();
}

static void prv_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);
  window_set_background_color(window, GColorBlack);

  s_pad = PAD;
  s_usable_w = bounds.size.w - 2 * s_pad;

  // Round screens clip their corners — keep content well off the top/bottom.
  // Rect (Emery) top_margin doubles as the literal margin above the digit
  // block (see s_date_top below) — round keeps its bot_margin-anchored,
  // bezel-tuned layout untouched.
  int top_margin = PBL_IF_ROUND_ELSE(28, 5);

#if defined(PBL_PLATFORM_EMERY)
  s_date_font = prv_pick_date_font();   // 21px, or 18px where the locale is too wide
#else
  s_date_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DATE_14));
#endif
  s_date_font_custom = (s_date_font != NULL);
  if (!s_date_font) s_date_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  s_date_h = prv_measure("WO", s_date_font).h + 2;

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

#if defined(PBL_PLATFORM_EMERY)
  // Stack top-down with the 3 fixed 5px gaps, rather than centering the
  // block in whatever space bot_margin leaves — so all 3 gaps actually are
  // 5px instead of splitting leftover slack between them.
  s_date_top = top_margin + 2 * s_slot_h + DIGIT_GAP + DIGIT_BAND_BOT_GAP;
#else
  int bot_margin = PBL_IF_ROUND_ELSE(32, 2);  // round only; bezel-tuned
  s_date_top = bounds.size.h - bot_margin - s_date_h;
#endif

  s_digit_band_top = top_margin;

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
  if (s_shimmer_timer) app_timer_cancel(s_shimmer_timer);
  layer_destroy(s_digits_layer);
  layer_destroy(s_date_layer);
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

  // Latin-script locales only (FR/DE/ES/IT/PT/NL). Makes strftime %a/%b return
  // localised weekday/month abbreviations; prv_utf8_upper handles the casing.
  setlocale(LC_ALL, i18n_get_system_locale());

  srand(time(NULL));  // seeds the minute-snow noise

  tick_timer_service_subscribe(MINUTE_UNIT, prv_tick_handler);
  backlight_service_subscribe(prv_backlight_handler);
  accel_tap_service_subscribe(prv_tap_handler);

  app_message_register_inbox_received(prv_inbox_received_handler);
  app_message_register_outbox_sent(prv_outbox_sent_handler);
  app_message_register_outbox_failed(prv_outbox_failed_handler);
  app_message_open(app_message_inbox_size_maximum(), app_message_outbox_size_maximum());
}

static void prv_deinit(void) {
  app_message_deregister_callbacks();
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
