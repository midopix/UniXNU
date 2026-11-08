# NativeUfs8996

MSM8996 (Snapdragon 820) UFS 2.0 **read-only register probe** for UniXNU.

This component is the MSM8996 analogue of the SM8550 `ShengUfs` component:
an `IOService` subclass matched to an `IOPlatformDevice` personality with
`compatible` matching in `probe()`. It is **not** a storage driver.

## Hardware binding / ADT export

`UniXNU-msm8996-common/dts/msm8996.dtsi`:

```
ufshc: ufshc@624000 {
    compatible = "qcom,msm8996-ufshc", "qcom,ufshc", "jedec,ufs-2.0";
    unixnu,adt-name = "ufshc";
    reg = <0 0x624000 0 0x2500>;
    status = "disabled";
};
```

The SoC dtsi ships every peripheral disabled. `devicetree.py` only exports
nodes that carry `unixnu,adt-name` **and** pass `enabled()`, so the board DTS
must override the status. `UniXNU-g0245d/dts/g0245d.dts` does that:

```
&ufshc { status = "okay"; };
```

Without this override the driver is linked into `kernel.macho` but no
`IOPlatformDevice` nub named `ufshc` exists and `probe()` is never called for
it. Verified offline: the generated ADT `arm-io` exports
`intc, ufshc, gpu, rpm-msg-ram, tcsr-mutex, blsp2-i2c6`.

The `compatible` strings above are the ones the ADT actually carries. The
vendor CAF Android `board.dtb` uses different names (`jedec,ufs-1.1`,
`qcom,ufshc`); `probe()` accepts a few of those as defensive aliases, but
do not confuse the vendor DTB with the UniXNU-generated ADT.

## Current state: read-only register snapshot

`start()`:

1. maps the first MMIO region (UFSHCI at `0x624000`, size `0x2500`);
2. reads eight registers at offsets `<= 0x2C`: `CAP`, `VER`, `HCPID`,
   `HCMID`, `AHIT`, `IS`, `IE`, `HCTRL`;
3. publishes them verbatim as `UFS8996Registers` (OSData, ascending offset
   order) plus `UFS8996Version` / `UFS8996Capabilities` /
   `UFS8996ProductID` / `UFS8996ManufacturerID` / `UFS8996ControllerPhys`;
4. **gates success** on `CAP` and `VER` not reading back as 0 or all-ones.
   Those are hard-wired identification registers; `IS`/`IE`/`HCTRL` may
   legitimately be zero (no interrupts pending, HCE still clear) and are
   diagnostics only;
5. on failure records `UFS8996Failure` / `UFS8996Status`, sets
   `UFS8996Probed = false`, releases the mapping and returns false without
   calling `registerService()`.

`UFS8996ProbeScope` is always published first as
`"register-read-only; no HCE/UIC/UTP commands"`. `UFS8996Probed = true`
therefore means **only** "MMIO mapped and CAP/VER look plausible" — not that
UFS storage works, the link is up, or a root volume can mount.

The volatile globals (`ufs8996_step`, `ufs8996_ver`, `ufs8996_cap`,
`ufs8996_failure`, …) remain for the boot page / QR keyframe.

## What remains (hardware-required)

- Confirm on-device that the newly enabled MMIO read does not raise a
  synchronous abort; if the block is clock-gated, gate the read behind the
  GCC clock enable instead.
- **Re-verify every offset >= 0x30 against the MSM8996 UFSHCI IP.** Layout
  differs between UFSHCI revisions; the header intentionally defines no
  transfer-layer offsets. Do not assume 0x30 is the UIC command register.
- HCE enable + `HCS.Ready` wait, UE enable, UIC `DME_LINKSTARTUP`.
- UTRL/UTMRL setup and doorbell rings, NOP-OUT, query requests, unit
  discovery, RPMB / boot-LU handling.
- Expose `IOBlockStorageDevice`, integrate the storage family and
  (optionally) the verified-root policy bundle behind `native-ufs-root=1`.

## Build / wiring (current)

Registered in `unixnu-src/components.toml` as `NativeUfs8996`
(`build-arm-driver.py NativeUfs8996` -> `libNativeUfs8996.a`). Already
enabled in `UniXNU-g0245d/board.toml` (`[components].include` and the full
`[kernel].archives` list) and force-loaded into `kernel.macho` through
`CONFIG_XNU_EXTRA_LD_ARCHIVES` in `configs/g0245d_defconfig`.

After any defconfig edit, re-run
`./unixnu defconfig g0245d_defconfig -D ../UniXNU-g0245d` — `build kernel`
reads the cached `out/.../.config` and will otherwise silently keep the old
flags (this previously produced a "archive built but not linked" false
success). Verify with `linkarchives` (`-force_load` entries) and
`llvm-nm kernel.macho`.

## Why not reuse ShengUfs

`ShengUfs` targets the SM8550 UFS 3.x/4.x controller. MSM8996 is UFS 2.0
(`jedec,ufs-2.0`) with a different UFSHCI/PHY generation (QMP UFS PHY at
`0x627000`), so a dedicated component is required.
