# Legere

Legere is a minimal Pebble watchface built around two ideas:

1. **Minimal battery drain.** It redraws once a minute, shows only the time
   and date, and has no complications or animation.
2. **What you need to know differs at night**

Works on **Pebble Time 2** and **Pebble Round 2**.

## Day and night

**By day, the minutes matter.** Huge overlapping numerals put the exact
minute in bold white, with the hour behind it in dark grey. A glance tells
you whether you're running late for your next meeting.

**At night, the hour matters.** From 21:00 to 07:00 by default (adjustable,
or switch it off), the face flips: hours turn solid red and minutes become a
hollow red outline. Waking in the dark, you only need a rough sense of the
hour to decide whether you can stay in bed. Red also cuts blue and white
light, so it's easy on your eyes.

## Intentionally limited

If you want weather, steps or heart rate on your face, this isn't it. If
you want a calm, readable clock that barely touches your battery, it might
be.

## Settings

Open the watchface settings in the Pebble app:

- **Night colour** — on or off (on by default).
- **Start / end hour** — when Night colour begins and ends, on a 24-hour clock
  (21:00 to 07:00 by default).

Settings are saved on the watch, so they survive a reconnect.

## Date language

The date follows your watch's language setting (English, French, German and
other Latin-script languages).

## Battery

Legere itself uses almost no power. Your battery life is mostly decided by watch
settings, so for the best results:

1. Turn off motion-activated backlight (the biggest single saving).
2. Set heart-rate background sampling to every 30 minutes or hourly.
3. Keep notifications modest.
4. Keep Bluetooth connected and stable.
5. Run reasonably current PebbleOS.

## Support

Feedback and ideas are welcome. 

Ruckpebble is free and always will be. If it's earned a spot on your watch, you can say thanks with a coffee. No pressure, entirely optional, and hugely appreciated.

Doctor's orders: one coffee a day. So it had better be a good one :-)

[![Ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/gordonbazeley)

[![Sponsor](https://img.shields.io/badge/Sponsor-%E2%9D%A4-db61a2.png?logo=githubsponsors&logoColor=white)](https://github.com/sponsors/gordonbazeley)

## Thanks

Date text uses [Quantico](https://fonts.google.com/specimen/Quantico)
and the digits are drawn from [Alfa Slab One](https://fonts.google.com/specimen/Alfa+Slab+One),
both under the SIL Open Font License.

## Development
* Developers: see `src/dev_docs/`.
