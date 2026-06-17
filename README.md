# pim447 — ZMK module for the Pimoroni PIM447 Trackball

Zephyr/ZMK driver and behavior for the [Pimoroni Trackball (PIM447)](https://shop.pimoroni.com/products/trackball-breakout)
breakout. Exposes the trackball as a ZMK mouse/scroll input device and adds a
behavior to tune speed, smoothing, hue and mode at runtime from the keymap.

## Features

- I2C driver reading movement (X/Y), the integrated switch and the interrupt pin.
- Mouse mode (relative X/Y) and scroll mode (wheel/hwheel), toggleable at runtime.
- Exponential speed scaling + per-axis EMA smoothing, all tunable live.
- HSV → RGBW LED colour cycling driven by movement speed.
- Optional "automouse" layer that activates while the ball is moved and
  deactivates after a configurable timeout.
- Sleep/wake tied to ZMK's activity state (idle puts the sensor to sleep).
- Behavior bindings to increment/decrement every parameter, toggle mode and
  force sleep/wake.

## Hardware wiring

| PIM447 pin | MCU pin (example) |
|------------|-------------------|
| VDD        | 3V3               |
| GND        | GND               |
| SCL        | I2C SCL           |
| SDA        | I2C SDA           |
| INT        | any GPIO (active-low, pull-up) |
| SW         | handled via the breakout's INT/switch register |

The PIM447 default I2C address is `0x0a`.

## Devicetree overlay

Add a node compatible with `zmk,pimoroni-pim447` on the I2C bus and wire the
interrupt GPIO:

```dts
&i2c0 {
    status = "okay";
    clock-frequency = <I2C_BITRATE_STANDARD>;

    pimoroni_pim447: pimoroni_pim447@a {
        compatible = "zmk,pimoroni-pim447";
        reg = <0x0a>;
        int-gpios = <&gpio0 6 (GPIO_ACTIVE_LOW | GPIO_PULL_UP)>;
        /* Optional: layer index to activate while the ball moves. */
        automouse-layer = <2>;
    };
};
```

### Properties

| property          | type | default | description |
|-------------------|------|---------|-------------|
| `reg`             | int  | —       | I2C address (usually `0x0a`) |
| `int-gpios`       | phandle-array | — | Interrupt pin (active-low) |
| `automouse-layer` | int  | `-1` (disabled) | Keymap layer to activate on movement |

## Kconfig options

| option | default | description |
|--------|---------|-------------|
| `CONFIG_ZMK_PIMORONI_PIM447` | n | Enable the driver |
| `CONFIG_ZMK_PIM447_BEHAVIORS` | y (if driver on) | Enable the `&pim447` behavior |
| `CONFIG_ZMK_PIMORONI_PIM447_AUTOMOUSE_TIMEOUT_MS` | 400 | Time the automouse layer stays active after the last movement |
| `CONFIG_ZMK_PIMORONI_PIM447_LOG_LEVEL` | 3 | Zephyr log level for the driver |

`CONFIG_ZMK_PIMORONI_PIM447` selects `ZMK_POINTING`, `I2C` and `GPIO`.

## Behavior bindings

Include the dt-bindings header and the behavior devicetree include, then use `&pim447 <action>`:

```dts
#include <behaviors.dtsi>
#include <behaviors/behavior_pim447.dtsi>
#include <dt-bindings/behavior_pim447.h>

/ {
    keymap {
        compatible = "zmk,keymap";
        default_layer {
            bindings = <
                &pim447 PIM447_TOGGLE_MODE
                &pim447 PIM447_MOUSE_INC_MAX_SPEED
                &pim447 PIM447_MOUSE_DEC_MAX_SPEED
                &pim447 PIM447_SCROLL_INC_MAX_SPEED
            >;
        };
    };
};
```

| action | effect |
|--------|--------|
| `PIM447_MOUSE_INC_MAX_SPEED` / `PIM447_MOUSE_DEC_MAX_SPEED` | mouse max speed ±1 |
| `PIM447_SCROLL_INC_MAX_SPEED` / `PIM447_SCROLL_DEC_MAX_SPEED` | scroll max speed ±1 |
| `PIM447_MOUSE_INC_MAX_TIME` / `PIM447_MOUSE_DEC_MAX_TIME` | mouse max time ±1 |
| `PIM447_SCROLL_INC_MAX_TIME` / `PIM447_SCROLL_DEC_MAX_TIME` | scroll max time ±1 |
| `PIM447_MOUSE_INC_SMOOTHING_FACTOR` / `PIM447_MOUSE_DEC_SMOOTHING_FACTOR` | mouse smoothing ±0.1 |
| `PIM447_SCROLL_INC_SMOOTHING_FACTOR` / `PIM447_SCROLL_DEC_SMOOTHING_FACTOR` | scroll smoothing ±0.1 |
| `PIM447_INC_HUE_INCREMENT_FACTOR` / `PIM447_DEC_HUE_INCREMENT_FACTOR` | hue increment ±0.1 |
| `PIM447_TOGGLE_MODE` | toggle mouse ↔ scroll |
| `PIM447_ENABLE_SLEEP` / `PIM447_DISABLE_SLEEP` | force sensor sleep / wake |

## Build

This is a ZMK module. Point your keyboard's `west` manifest at this repo (or
drop it under `config/` as the included test setup does) and build with ZMK:

```sh
west build -s zmk/app -b <board>//zmk -- -DSHIELD=<your-shield> -DZMK_CONFIG=<config-dir>
```

The bundled `config/boards/shields/pim447_test` shield builds the driver against
`nice_nano//zmk` for CI; use it as a reference overlay.

## License

MIT. See SPDX headers in individual files.
