# Blsp8996

MSM8996 BLSP QUP v2 **read-only register probe** (I2C path) for UniXNU.

The MSM8996 BLSP blocks are the older QUP v2 serial engines, not the GENI
engines used on SM8550. This component is the I2C transport that a future
touch / sensor driver would ride on. It is **not** an I2C controller driver.

## Hardware binding / ADT export

`UniXNU-msm8996-common/dts/msm8996.dtsi` declares the QUP nodes disabled.
Only one of them is currently enabled by the board DTS
(`UniXNU-g0245d/dts/g0245d.dts`):

```
&blsp2_i2c6 { status = "okay"; };   // i2c@75ba000, SPI 106
```

`blsp2-i2c6` is the board's **touch bus** (Synaptics DSX at I2C 0x20) —
earlier revisions of this README incorrectly named `blsp1-i2c6`
(`0x757a000`). The overlapping alias `&blsp2_spi6` shares the same register
window and stays disabled.

`devicetree.py` exports only nodes with `unixnu,adt-name` that pass
`enabled()`, so the `status = "okay"` override is mandatory: without it the
linked driver has no `IOPlatformDevice` nub to match.

`probe()` matches `qcom,i2c-qup-v2.2.1` / `qcom,spi-qup-v2.2.1`, which is
exactly what the UniXNU ADT carries. The vendor CAF Android `board.dtb` uses
`qcom,i2c-msm-v2`-style names; those are accepted as defensive aliases only.

## Current state: read-only register snapshot

`start()`:

1. maps the first MMIO region (QUP block);
2. reads `QUP_CONFIG` (0x000), `QUP_STATE` (0x004), `QUP_OPERATION` (0x018),
   `QUP_ERROR_FLAGS` (0x01C) and `QUP_HW_VERSION` (0x020);
3. publishes them verbatim as `BLSP8996Registers` (OSData) plus
   `BLSP8996HwVersion` / `BLSP8996State` / `BLSP8996ControllerPhys`;
4. **gates success** on `QUP_HW_VERSION` not reading back as 0 or all-ones.
   `QUP_STATE`/`CONFIG`/`OPERATION`/`ERROR_FLAGS` may legitimately be zero
   (engine in RESET, no transaction ever issued) and are diagnostics only;
5. on failure records `BLSP8996Failure` / `BLSP8996Status`, sets
   `BLSP8996Probed = false`, releases the mapping and returns false without
   `registerService()`.

`BLSP8996ProbeScope` is always published as
`"register-read-only; no QUP/I2C transactions"`. `BLSP8996Probed = true`
means **only** "MMIO mapped and HW_VERSION looks plausible".

`REG_QUP_HW_VERSION = 0x020` comes from the mainline QUP v2 binding and still
needs confirmation against the MSM8996 BLSP IP; the raw value is published so
a wrong offset is visible on the first device boot.

## What remains (hardware-required)

- Confirm on-device that the newly enabled MMIO reads do not raise a
  synchronous abort; gate behind GCC clock/reset if needed.
- QUP_CONFIG programming (I2C mini-core, N=7, FIFO/block mode), mini-core
  reset release, `I2C_MASTER_CLK_CTL` / `I2C_MASTER_STATUS`.
- Real write/read transfers via TX/RX FIFOs and a `IOI2CInterface` API.
- Synaptics DSX touch driver on BLSP2 I2C6; sensor drivers on other QUPs.
- A runtime UART driver for `blsp2-uart2` (`0x75b0000`) if needed. The early
  kernel console is separate: an XNU core patch drives MSM UARTDM directly
  (`UniXNU-msm8996-common patches/xnu/0002-msm8996-blsp-console`).

## Build / wiring (current)

Registered in `unixnu-src/components.toml` as `Blsp8996`
(`build-arm-driver.py Blsp8996` -> `libBlsp8996.a`). Already enabled in
`UniXNU-g0245d/board.toml` and force-loaded into `kernel.macho` through
`CONFIG_XNU_EXTRA_LD_ARCHIVES`; verified with `linkarchives` and `llvm-nm`.

After any defconfig edit, re-run
`./unixnu defconfig g0245d_defconfig -D ../UniXNU-g0245d` to refresh
`out/.../.config`.
