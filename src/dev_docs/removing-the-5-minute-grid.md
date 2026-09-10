# Removing the 5-minute refresh logic — code and battery impact

Investigated 2026-09-10. Question: how much simpler would legere be without the
5-minute soft grid, and would it help battery? Answers: **much simpler** (two
readings below), **battery no**.

Nothing here is a decision to act — see `decisions.md` for why the grid is kept
("identity choice, not a power optimisation").

## Two readings of "remove the 5-minute refresh logic"

### Reading A — drop only the every-minute *setting*, keep the static face

Remove `s_every_minute`, `PERSIST_KEY_REDRAW_MODE`, `prv_inbox_received_handler`,
the AppMessage subsystem on the watch, the two `if (s_every_minute)` branches,
and the "Time refresh" section of the phone settings page.

- ~70 lines gone; `messageKeys` empties, the watch's AppMessage channel closes.
- Keeps the 5-minute grid, static, shimmer, shake-to-wake — the identity intact.
- This is the removal `decisions.md` already anticipates ("a stopgap, removed
  once touch is enabled for watchapps").

### Reading B — drop the grid + static/shake entirely, redraw every minute

Every-minute mode becomes the only behaviour. `legere.c` goes from ~577 to
~330 lines (~45% smaller).

| Chunk deleted | ~lines |
|---|---|
| `s_exact`, `s_passive_min`, `prv_floor5` + comments | 15 |
| shimmer ramp: `SHIMMER_*`, `s_shimmer_*`, `prv_shimmer_tick` | 25 |
| snow: `PASSIVE_SNOW_PERMILLE`, `prv_snow_permille`, `SNOW[]`, `prv_staticify` | 55 |
| `prv_refresh_to_exact` + `s_exact_hour/min` | 25 |
| `prv_tap_handler`, `prv_backlight_handler`, `s_sched_hour/min` | 40 |
| every-minute setting + `prv_inbox_received_handler` + app_message init/deinit | 35 |
| `prv_tick_handler` lock-out block + `step` → ~8-line body | 25 |
| top-of-file comment blocks (lines 14–33, 61–83) | 30 |

Conceptual wins, bigger than the line count:

- **One time source.** `disp_min = t->tm_min`. Today it is 3-way (passive /
  exact / mid-ramp) plus the "don't floor back past an expired exact reading"
  handling.
- **No state machine.** `s_exact` × `s_shimmer_out` × `s_shimmer_left` × timer
  lifecycle — the lock-on/lock-out ramps, the most intricate part of the file.
- **No framebuffer poking.** `prv_staticify` captures the framebuffer and
  per-pixel RNGs it. Gone.
- **Three input paths → one.** tick + tap + backlight collapse to tick.
- **No persisted settings, no phone↔watch messaging.**

pkjs side: `settings.html` loses ~35 of 131 lines, `index.js` drops to a stub
(~15 of 39), `open_config.js` loses `--redraw` (~5).

Cost: this is the whole point of the face ("the minute reads as static until you
shake"). Reading B ships a plain digital watchface — very simple, different
product.

## Battery: no measurable difference either way

Consistent with the existing research in `decisions.md` and `architecture.md`.

- **Code size → zero effect.** Flash footprint does not affect draw. Battery is
  work-per-wake, not binary bytes.
- **Runtime work removed → below telemetry noise.** The minute *wake* is
  ~10⁻⁵ mAh and the OS wakes the app every minute regardless (`MINUTE_UNIT`
  stays subscribed). The whole grid-vs-every-minute question is **~6–26 µA,
  "under one day over three weeks."** `prv_staticify` + shimmer timers are the
  "more expensive part of a redraw" and still not measurable.
- **Reading B is slightly *worse*, not better.** Redrawing every minute is 5×
  the redraws. Each redraw gets cheaper (no snow scan, no shimmer bursts) but
  the 5× frequency more than eats that. Net: the ~6–26 µA, wrong direction.
  Still a rounding error.
- **The one real lever is dropping `accel_tap_service`.** The accel peripheral
  sampling for tap detection is a standing draw — unlike the passive
  `backlight_service` callback (free) or the event-driven AppMessage inbox (free
  when idle). Not quantified in our docs; it is the only item here that might
  clear noise. Reading A keeps it (shake-to-wake). Reading B only drops it if
  shake is given up too.

The real drains are unchanged and not watchface-controlled: motion-activated
backlight, notifications/vibration, HR sampling interval, BLE reconnect churn.

**Conclusion:** simplify for simplicity if wanted; do not expect a battery gain.
