# K1: touchscreen (research notes, read-only, 2026-10-20; nothing built or tested)

Everything below comes from Samsung's universal7880 kernel (hybris-17.1),
`exynos7880-a5y17lte_common.dtsi` and `exynos7880.dtsi`, and from Linux v7.2.

## What the hardware is
- Controller: **Zinitix ZT7538** (Samsung compatible `zinitix,bt532_ts_device`),
  panel 1080x1920, I2C address **0x20**.
- Bus: hardware I2C2, `i2c@13890000`, SPI 430. Its pins are **gpc2-0 / gpc2-1**
  (function 2, pull-up), listed in Samsung's DT as `i2c_nfc_bus`. By the same
  pattern as the MUIC bus (gpc1-0 SDA, gpc1-1 SCL), gpc2-0 is probably SDA and
  gpc2-1 SCL; not confirmed.
- Interrupt: **gpa0-5** (ALIVE bank, function 0xF = EINT). Our ALIVE controller
  and the gpa0 bank already exist in the DT; Power uses gpa0-0 the same way.
- Power: S2MPU05 PMIC regulators `vtouch_2.8v` (LDO32, boot-on) and
  `vtouch_3.3v` (LDO35, not boot-on). Samsung's node only names the supply
  `tsp_avdd` and flags `gpio_ldo_en`; which LDO that maps to is not in the DT.
  Unknown: whether a separate 1.8 V I/O supply exists.
- Not needed for plain touch: firmware `tsp_zinitix/zt7538_a5y17.fw`, spay/aod,
  touchkey (the capacitive keys are a separate I2C, gpd4-3/gpd4-5).

## What mainline already has
- `drivers/input/touchscreen/zinitix.c` accepts `zinitix,bt532` (also bt402 ...
  bt548, bt554, at100). It reads `touchscreen-size-x/y` and the usual
  touchscreen properties, takes supplies `vdd` + `vddo` (or `vcca` + `vdd`)
  and an interrupt. It does not flash firmware, so the IC runs whatever is in
  its flash (Samsung's); this works on other Samsung phones.
- S2MPU05 PMIC: `samsung,s2mpu05-pmic` in `drivers/regulator/s2mps11.c` and
  `samsung,s2mpu05.yaml`; the Exynos7870 phones (j5y17lte, j6lte, a2corelte,
  j7xelte, on7xelte) already use it. So K3 is mostly device tree work.

## What we still need (the work)
1. The **NFC pin controller** (`pinctrl@139C0000`, SPI 439; bank gpc2 is in
   our patch 0002 as `exynos7880_pin_banks5`) is **not in our DT** yet. Add it,
   or skip it by driving the touch bus as GPIO only after adding the node
   anyway (the bank has to exist for `&gpc2`).
2. The PMIC node: S2MPU05 at its I2C address on its bus (look it up in
   `exynos7880-a5y17lte_common.dtsi` near line 400; the MUIC style i2c-gpio
   pattern can carry it until the CMU_PERI I2C clocks exist) with LDO32 and
   LDO35 at least; copy the regulator constraints from the Samsung node.
3. The touch node: `compatible = "zinitix,bt532"`, `reg = <0x20>`,
   `interrupts-extended = <&gpa0 5 IRQ_TYPE_LEVEL_LOW>` (polarity to check),
   `touchscreen-size-x = <1080>`, `touchscreen-size-y = <1920>`, `vdd-supply`
   and `vddo-supply` from the two vtouch LDOs. Bus: i2c-gpio first (works,
   proven with the MUIC), hardware I2C after the clock driver (K2).
4. Config: INPUT_TOUCHSCREEN, TOUCHSCREEN_ZINITIX, REGULATOR, MFD_SEC_I2C,
   REGULATOR_S2MPS11, I2C_GPIO (already). All built in.
5. Phone test (read-only): `dmesg | grep -i zinitix`, then `evtest` on the new
   event node: coordinates inside 0..1079 / 0..1919 while touching the corners.
   If probe fails with -EIO, a supply is probably off or the wrong LDO: try
   enabling the other one by hand through the regulator sysfs first.

## Risks
- Which LDO feeds the IC, and whether a 1.8 V rail has to be on, is guessed
  until tried. Regulators are always-on in Samsung's DT for most LDOs, so
  leaving both vtouch rails on at boot is the safe first try.
- The panel's touch coordinates may be rotated or mirrored relative to the
  display; fix in userspace first (libinput calibration matrix) and only then
  in the DT.
