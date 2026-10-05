# legere

A calm Pebble watchface. Big, bold, overlapping numerals show the exact time,
with the date underneath. Nothing moves, nothing flashes, nothing to fiddle with.

Works on **Pebble Time 2** and **Pebble Round 2**.

## Day and night

By day, the minutes are the bold white digits and the hours sit behind them in
dark grey. When you glance at your wrist during the day the question is usually
"am I running late?", which needs the exact minute.

Night colour (optional) flips this. Hours turn solid red and minutes become a
hollow red outline. Waking at night, the question is "should I go back to
sleep?", which only needs a rough sense of the hour. The red also cuts blue and
white light so it is easier on your eyes in the dark.

## Settings

Open the watchface settings in the Pebble app:

- **Night colour** — on or off (off by default).
- **Start / end hour** — when Night colour begins and ends, on a 24-hour clock.

Settings are saved on the watch, so they survive a reconnect.

## Date language

The date follows your watch's language setting (English, French, German and
other Latin-script languages).

## Battery

legere uses almost no power itself. Your battery life is mostly decided by watch
settings, so for the best results:

1. Turn off motion-activated backlight (the biggest single saving).
2. Set heart-rate background sampling to every 30 minutes or hourly.
3. Keep notifications modest.
4. Keep Bluetooth connected and stable.
5. Run reasonably current PebbleOS.

## Support

Questions or feedback: <https://ko-fi.com/gordonbazeley>

## Credits

By ModusApps. Date text uses [Quantico](https://fonts.google.com/specimen/Quantico)
and the digits are drawn from [Alfa Slab One](https://fonts.google.com/specimen/Alfa+Slab+One),
both under the SIL Open Font License.

For developers: see `src/dev_docs/`.
