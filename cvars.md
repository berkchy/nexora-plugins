# Nexora Client CVARs

This file lists the custom cvars added to the CS 1.6 client.

## Scoreboard

| Cvar | Default | Values | Description |
| --- | --- | --- | --- |
| `cl_scoreboard_style` | `2` | `0`, `1`, `2` | `2` uses the CS:GO-style scoreboard. Values below `2` use the classic scoreboard. |
| `cl_scoreboard_alpha` | `220` | `170-235` | Scoreboard background opacity. |
| `cl_scoreboard_show_money` | `1` | `0`, `1` | Show player money. |
| `cl_scoreboard_show_hp` | `1` | `0`, `1` | Show player health. |
| `cl_scoreboard_show_ping` | `1` | `0`, `1` | Show player ping. |
| `cl_scoreboard_show_flags` | `1` | `0`, `1` | Show country/flag information. |
| `cl_scoreboard_high_contrast` | `0` | `0`, `1` | Use high-contrast text colors. |
| `cl_scoreboard_fadein` | `0.15` | seconds | Scoreboard fade-in duration. |
| `cl_scoreboard_fadeout` | `0.18` | seconds | Scoreboard fade-out duration. |

## HUD positions

`hudpos` repositions HUD elements on the classic HUD. It works with the stock layout when every axis is left at `auto`.

Grammar for every `hud_<element>_x` / `hud_<element>_y` cvar:

| Value | Meaning |
| --- | --- |
| `auto` (or empty) | Keep the default position (this is the initial state). |
| `-99999` | Hide the element entirely. |
| positive number | Absolute position in HUD pixels from the left (`_x`) / top (`_y`) edge. |
| negative number | Offset from the opposite edge: `_x` counts leftwards from the right edge, `_y` counts upwards from the bottom edge. E.g. `hud_timer_y "-20"` puts the timer 20px above the bottom edge. |
| percent, e.g. `57.5%` | Position as a percentage of that axis, so it scales with any resolution/`hud_scale`. Negative percents anchor to the opposite edge just like negative pixels (e.g. `hud_timer_y "-8%"` ≈ 8% above the bottom edge). |

| Cvar | Element |
| --- | --- |
| `hud_ammo_x`, `hud_ammo_y` | Ammo counter (classic and CS:GO HUD). The ammo secondary counter moves with it. |
| `hud_timer_x`, `hud_timer_y` | Round timer. |
| `hud_health_x`, `hud_health_y` | Health cross + value. |
| `hud_armor_x`, `hud_armor_y` | Armor shield + value (classic HUD `battery` element). |
| `hud_money_x`, `hud_money_y` | Money counter. |
| `hud_radar_x`, `hud_radar_y` | Classic HUD radar (the CS:GO HUD has no radar panel; use classic mode). |

## Killfeed

Killfeed is independent; it replaces the classic death notice entirely unless `hud_killfeed_enable 0` is set. The fade settings below control the killfeed rows themselves; use `hud_killfade` for the screen color effect.

| Cvar | Default | Values | Description |
| --- | --- | --- | --- |
| `hud_killfeed_enable` | `1` | `0`, `1` | Enable/disable the killfeed plugin entirely (classic death notice returns when `0`). |
| `hud_killfeed_fade` | `1` | `0`, `1` | Enable/disable killfeed row fading. |
| `hud_killfeed_fadein` | `1` | `0`, `1` | Enable/disable row fade-in. |
| `hud_killfeed_fadeout` | `1` | `0`, `1` | Enable/disable row fade-out. |
| `hud_killfeed_fxtime` | `0.25` | seconds | Fade effect duration. |
| `hud_killfeed_holdtime` | `5` | seconds | How long each row stays on screen. |
| `hud_killfeed_color` | `""` | `"r g b"` | Text color. Empty uses team colors. |
| `hud_killfeed_alpha` | `235` | `0-255` | Text brightness/opacity. |
| `hud_killfeed_bgcolor` | `"0 0 0"` | `"r g b"` | Row background color. |
| `hud_killfeed_bgalpha` | `110` | `0-255` | Row background opacity. |
| `hud_killfeed_rowspacing` | `4` | `0-32` | Extra vertical gap between rows, in 480p HUD units. |

## Centerprint

Replaces the classic center-screen messages ("Terrorist Win", "Bomb Planted", ...) with a smooth fade-in/fade-out animation. It is independent. When `hud_centerprint 0` is set, the classic center print returns.

| Cvar | Default | Values | Description |
| --- | --- | --- | --- |
| `hud_centerprint` | `1` | `0`, `1` | Enable/disable the centerprint plugin entirely (classic center print returns when `0`). |
| `hud_centerprint_fadein` | `0.25` | seconds | Fade-in duration. |
| `hud_centerprint_fadeout` | `0.5` | seconds | Fade-out duration. |
| `hud_centerprint_holdtime` | `4` | seconds | How long the message stays fully visible. |
| `hud_centerprint_color` | `"255 214 0"` | `"r g b"` | Text color. |
| `hud_centerprint_alpha` | `255` | `0-255` | Text brightness/opacity. |
| `hud_centerprint_yfract` | `0.333` | `0-1` | Vertical position as a fraction of screen height. |
| `hud_centerprint_bg` | `0` | `0`, `1` | Draw a background panel behind the text. |
| `hud_centerprint_bgcolor` | `"0 0 0"` | `"r g b"` | Background panel color. |
| `hud_centerprint_bgalpha` | `140` | `0-255` | Background panel opacity. |

## Killfade

A `ScreenFade`-based screen color effect. It only triggers on local player kills; it does not trigger on suicide, teamkill, or non-player kills. It works independently.

| Cvar | Default | Values | Description |
| --- | --- | --- | --- |
| `hud_killfade` | `1` | `0`, `1` | Enable/disable the killfade screen effect. |
| `hud_killfade_fadein` | `1` | `0`, `1` | Enable/disable the fade-in stage. |
| `hud_killfade_fadeout` | `1` | `0`, `1` | Enable/disable the fade-out stage. |
| `hud_killfade_fxtime` | `0.25` | seconds | Fade-in and fade-out duration. |
| `hud_killfade_holdtime` | `0.12` | seconds | How long the effect stays at full color. |
| `hud_killfade_color` | `"255 255 255"` | `"r g b"` | Screen fade color. |
| `hud_killfade_alpha` | `70` | `0-255` | Screen fade opacity. |

## Example

```cfg
cl_scoreboard_style 2
cl_scoreboard_alpha 220
cl_scoreboard_show_money 1
cl_scoreboard_show_hp 1
cl_scoreboard_show_ping 1
cl_scoreboard_show_flags 1
cl_scoreboard_high_contrast 0
cl_scoreboard_fadein 0.15
cl_scoreboard_fadeout 0.18

hud_killfeed_fade 1
hud_killfeed_fadein 1
hud_killfeed_fadeout 1
hud_killfeed_fxtime 0.25
hud_killfeed_holdtime 5
hud_killfeed_color ""
hud_killfeed_alpha 235
hud_killfeed_bgcolor "0 0 0"
hud_killfeed_bgalpha 110
hud_killfeed_rowspacing 4

hud_killfade 1
hud_killfade_fadein 1
hud_killfade_fadeout 1
hud_killfade_fxtime 0.25
hud_killfade_holdtime 0.12
hud_killfade_color "255 255 255"
hud_killfade_alpha 70

hud_centerprint 1
hud_centerprint_fadein 0.25
hud_centerprint_fadeout 0.5
hud_centerprint_holdtime 4
hud_centerprint_color "255 214 0"
hud_centerprint_alpha 255
hud_centerprint_yfract 0.333
hud_centerprint_bg 0
hud_centerprint_bgcolor "0 0 0"
hud_centerprint_bgalpha 140
```
