# legere

legere is a minimal Pebble watchface built around two ideas:

1. **Minimal battery drain.** It redraws once a minute, shows only the time
   and date, and has no complications or animation.
2. **What you need to know differs at night.**

Works on **Pebble Time 2** and **Pebble Round 2**.

## Day and night

**By day, the minutes matter.** Huge overlapping numerals put the exact
minute in bold white, with the hour behind it in dark grey. A glance tells
you whether you're running late for your next meeting.

**At night, the hour matters.** With Night colour on (optional), the face
flips: hours turn solid red and minutes become a hollow red outline. Waking
in the dark, you only need a rough sense of the hour to decide whether you
can stay in bed. Red also cuts blue and white light, so it's easy on your
eyes.

## Intentionally limited

If you want weather, steps or heart rate on your face, this isn't it. If
you want a calm, readable clock that barely touches your battery, it might
be.

## Settings

Open the watchface settings in the Pebble app:

- **Night colour** — on or off (off by default).
- **Start / end hour** — when Night colour begins and ends, on a 24-hour clock.

Settings are saved on the watch, so they survive a reconnect.

## Date language

The date follows your watch's language setting (English, French, German and
other Latin-script languages).

## Battery

legere itself uses almost no power. Your battery life is mostly decided by watch
settings, so for the best results:

1. Turn off motion-activated backlight (the biggest single saving).
2. Set heart-rate background sampling to every 30 minutes or hourly.
3. Keep notifications modest.
4. Keep Bluetooth connected and stable.
5. Run reasonably current PebbleOS.

## Support

Feedback and ideas welcome: <https://ko-fi.com/gordonbazeley>

## Credits

By ModusApps. Date text uses [Quantico](https://fonts.google.com/specimen/Quantico)
and the digits are drawn from [Alfa Slab One](https://fonts.google.com/specimen/Alfa+Slab+One),
both under the SIL Open Font License.

For developers: see `src/dev_docs/`.
