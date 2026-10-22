# K6: battery and charging (research 2026-10-23; K6a built, not tested)

Sources: Samsung universal7880 kernel (hybris-17.1):
`exynos7880-a5y17lte-common-battery.dtsi`, `exynos7880-a5y17lte_common.dtsi`,
`drivers/battery_v2/s2mu004_fuelgauge.c`, `s2mu004_charger.c`,
`include/linux/mfd/samsung/s2mu004-private.h`; Linux v7.2.

## Chips on the A5 2017
| Part | Bus / address | Interrupt | Mainline v7.2 |
|---|---|---|---|
| S2MU004 fuel gauge | I2C4 `i2c@13830000` (gpc4-0 SDA, gpc4-1 SCL), 0x3b | gpa0-3 | **Yes**: `s2mu005-battery` (same registers and scaling) |
| S2MU004 charger + MUIC + flash LED (MFD) | I2C0 `i2c@13870000` (gpc1-0/1), 0x3d | gpa2-7 | **No**: mainline has S2MU005 (`samsung,s2mu005-pmic`, `s2m-charger`, extcon `s2mu005-muic`), but the S2MU004 register map differs |
| S2MU004 USB-PD (Type-C) | I2C4, 0x3c | gpa1-3 | No |
| AP thermistor (battery/AP temperature) | ADC `adc@10650000` channel 0 | - | ADC: `exynos-adc` exists; the 7880 ADC block is not described yet |

## Fuel gauge: S2MU004 = S2MU005 (checked)
Samsung `s2mu004_fuelgauge.c` vs mainline `s2mu005-battery.c`:
- Registers: STATUS 0x00, IRQ 0x02, RVBAT 0x04, RCUR_CC 0x06, RSOC 0x08,
  MONOUT 0x0a, MONOUT_SEL 0x0c, RBATCAP 0x0e, RZADJ 0x12, RBATZ0/1 0x16/0x18,
  IRQ_LVL 0x1a, START 0x1e: identical.
- Voltage: Samsung `mV = raw * 1000 >> 13`, mainline `uV = raw * 10^6 >> 13`.
  Average voltage `>> 12` in both. Current: both `>> 12`, two's complement,
  negative register = charging. Same.
- Mainline's driver only reads (and selects MONOUT), so it is safe to try.
- Samsung's driver also writes a battery model table (`battery_params`,
  4.35/4.33/4.31 V tables) and does SOC correction. Mainline doesn't. The
  gauge keeps its table while the battery stays connected (Samsung's kernel,
  i.e. TWRP or Sailfish, loaded it), so the percentage should be close; it may
  drift after a full battery disconnect.

**K6a (branch android-k6):** kernel patch 0010 (binding: `samsung,s2mu004-fuel-gauge`
with the S2MU005 fallback), DT node on a GPIO I2C bus, `CONFIG_POWER_SUPPLY`,
`CONFIG_BATTERY_S2MU005`. Result: `/sys/class/power_supply/<fg>/capacity`,
`voltage_now`, `current_now`, `status`, which Phosh/upower and Android's
health HAL read.

## Charger: what works without a driver
S-Boot configures the S2MU004 charger; it charges on its own with those
settings (the phone charges in TWRP/Odin modes the same way). Without a driver
Linux can't: report "charging/full/USB type", change the input current
(fast-charge AFC is Samsung's own), or stop at 100 %. Watch `current_now`
with a charger plugged in: positive = charging.

## Charger + MUIC driver (later, real work)
S2MU004 vs S2MU005 register maps differ (S2MU004: SC_* charger block from
0x00, MUIC ADC 0x61, CTRL1 0xc7, SW_CTRL 0xca, REV_ID 0x82; S2MU005: MUIC
CTRL1 0xb2, ID 0x73, ...). The way in: add an S2MU004 device type to mainline's
`sec` MFD core (it already handles S2MU005), with its own regmap and IRQ
tables, then S2MU004 variants of `s2m-charger` and the extcon MUIC driver
(this also replaces `a5-diag muic`, which switches the USB lines by hand
today). Samsung's `s2mu004_charger.c` and `s2mu004-muic.c` give the register
fields. Estimate: several sessions, each testable (MUIC first: USB detection).

## Order
1. K6a fuel gauge (built): percentage/voltage/current.
2. MUIC driver (extcon): cable detection, USB switch in the kernel.
3. Charger driver: status, input current, charge limits.
4. Thermistor (ADC) for battery temperature.
