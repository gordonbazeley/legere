#include <pebble.h>
#include <ctype.h>
#include <string.h>

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
#define DIGIT_GAP PBL_IF_ROUND_ELSE(4, 5)   // px between the hour and minute rows
#define DIGIT_BAND_BOT_GAP PBL_IF_ROUND_ELSE(4, 5)  // px between the minute row and the date row

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
// ponytail: DAYS_KEPT days ring-buffered in persist storage (32B/day, well
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
    persist_read_data(PERSIST_KEY_DAY_BASE + s_day_cursor, &s_today, sizeof(s_today));
  }
  int year = t->tm_year + 1900, mon = t->tm_mon + 1, mday = t->tm_mday;
  if (s_today.year != year || s_today.mon != mon || s_today.mday != mday) {
    s_day_cursor = (s_day_cursor + 1) % DAYS_KEPT;
    persist_write_int(PERSIST_KEY_DAY_CURSOR, s_day_cursor);
    memset(&s_today, 0, sizeof(s_today));
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
    persist_read_data(PERSIST_KEY_DAY_BASE + i, &rec, sizeof(rec));
    if (rec.year == 0) continue;  // slot never written

    DictionaryIterator *iter;
    if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;  // will retry on next tap/reopen
    dict_write_int32(iter, MESSAGE_KEY_Year, rec.year);
    dict_write_int32(iter, MESSAGE_KEY_Mon, rec.mon);
    dict_write_int32(iter, MESSAGE_KEY_Mday, rec.mday);
    dict_write_data(iter, MESSAGE_KEY_Shakes, rec.shakes, sizeof(rec.shakes));
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

// Refresh to the exact minute (and flag it red) unless that's already on screen.
static void prv_refresh_to_exact(void) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  if (s_exact && prv_display_hour(t) == s_drawn_hour && t->tm_min == s_drawn_min) {
    return;  // nothing would change — don't spend a repaint
  }
  prv_log_trigger(t);
  s_exact = true;
  layer_mark_dirty(s_canvas_layer);
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

#define DATE_BOLD_PX 1   // faux-bold smear for the date row (Michroma has one weight)

static void prv_draw_cell(GContext *ctx, GRect box, const char *text, GFont font,
                          GTextAlignment align, GColor color) {
  graphics_context_set_text_color(ctx, color);
  for (int dx = 0; dx <= DATE_BOLD_PX; dx++) {
    GRect b = box;
    b.origin.x += dx;
    graphics_draw_text(ctx, text, font, b, GTextOverflowModeFill, align, NULL);
  }
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

  // Hour and minute stacked: two digits per row, the pair centred as a unit so a
  // narrow "1" doesn't shove the block sideways. Rows packed tight (only
  // DIGIT_GAP between them) and the whole block centred in the space above the
  // date. On the round display the grid is pulled in from both edges so its
  // corners clear the bezel — same proportions as the rectangular face.
  if (s_sheet) {
    int grid_w = PBL_IF_ROUND_ELSE(132, s_usable_w);
    int grid_x = s_pad + (s_usable_w - grid_w) / 2;
    int block_h = 2 * s_slot_h + DIGIT_GAP;
    int block_top = s_digit_band_top + PBL_IF_ROUND_ELSE(2, 0) +
                    (s_date_top - DIGIT_BAND_BOT_GAP - s_digit_band_top - block_h) / 2;
    int dv[4] = { hour / 10, hour % 10, disp_min / 10, disp_min % 10 };
    bool blank_hour_tens = !h24 && hour < 10;

    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    for (int row = 0; row < 2; row++) {
      int n = (row == 0 && blank_hour_tens) ? 1 : 2;  // digits shown on this row
      int row_x = grid_x + (grid_w - n * s_slot_w) / 2;
      int y = block_top + row * (s_slot_h + DIGIT_GAP);
      for (int k = 0; k < n; k++) {
        int idx = row * 2 + (n == 1 ? 1 : k);
        prv_set_ink(s_digit[dv[idx]], row == 0 ? GColorLightGray : GColorWhite);
        graphics_draw_bitmap_in_rect(ctx, s_digit[dv[idx]],
                                     GRect(row_x + k * s_slot_w, y, s_slot_w, s_slot_h));
      }
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
  // Bright blue: darker blues are unreadable on the transflective LCD unlit.
  GColor mon_color = s_exact ? GColorRed : GColorElectricBlue;
  int date_w = PBL_IF_ROUND_ELSE(132, s_usable_w);
  GRect date_box = GRect(s_pad + (s_usable_w - date_w) / 2, s_date_top, date_w, s_date_h);
  prv_draw_cell(ctx, date_box, dow, s_date_font, GTextAlignmentLeft, GColorWhite);
  prv_draw_cell(ctx, date_box, dom, s_date_font, GTextAlignmentCenter, GColorWhite);
  prv_draw_cell(ctx, date_box, mon, s_date_font, GTextAlignmentRight, mon_color);

  s_drawn_hour = hour;
  s_drawn_min = disp_min;
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
    APP_LOG(APP_LOG_LEVEL_INFO, "row %02d/%02d/%04d,%02d,%s,%d",
            s_today.mday, s_today.mon, s_today.year, s_logged_hour,
            was_quiet ? "yes" : "no", s_today.shakes[s_logged_hour]);
  }

  // Every-minute tick doubles as the sampler for the current hour's Quiet
  // Time flag — last sample in the hour wins, which converges to the right
  // answer since Quiet Time windows don't flap minute to minute.
  prv_ensure_today(tick_time);
  bool quiet = quiet_time_is_active();
  uint32_t bit = 1UL << tick_time->tm_hour;
  uint32_t new_mask = quiet ? (s_today.quiet_mask | bit) : (s_today.quiet_mask & ~bit);
  if (new_mask != s_today.quiet_mask) {
    s_today.quiet_mask = new_mask;
    prv_persist_today();
  }
  s_logged_hour = tick_time->tm_hour;
  s_logged_day_key = prv_day_key();

  // The OS wakes the app every minute for its own clock; we only repaint on the
  // 5-minute grid — and just hourly while the user's Quiet Time is on (asleep or
  // in a meeting). :00 and midnight are multiples of both, so hour and date
  // rollover stay covered.
  int step = quiet ? 60 : 5;
  if (tick_time->tm_min % step == 0) {
    s_exact = false;
    s_sched_hour = tick_time->tm_hour;
    s_sched_min = tick_time->tm_min;
    layer_mark_dirty(s_canvas_layer);
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
  int top_margin = PBL_IF_ROUND_ELSE(16, 5);
  int bot_margin = PBL_IF_ROUND_ELSE(22, 2);

#if defined(PBL_PLATFORM_EMERY)
  s_date_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DATE_21));
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
  s_date_top = bounds.size.h - bot_margin - s_date_h;
#endif

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
