# NativeAdreno530

MSM8996 Adreno 530 (A5xx) **read-only register probe** for UniXNU.

This component is the MSM8996 analogue of the SM8550 `Adreno740`, but the
Adreno 530 is an **A5xx** part and is fundamentally different from the A7xx
part Adreno740 drives:

| | Adreno 740 (SM8550) | Adreno 530 (MSM8996) |
| --- | --- | --- |
| uArch | A7xx | A5xx |
| GMU | Yes (HFI/GMU handshake) | **No GMU** |
| Power/clock | GMU + SM8550Platform kit | MMCC/GCC + VDD rail, direct |
| Command model | GMU rings | Direct RB/CP submit |

Because there is no GMU, the `build-adreno740-kits.py` / `Sm8550Platform`
path does not apply. A dedicated A5xx bring-up is required — the single
largest item in the MSM8996 porting plan.

## Hardware binding / ADT export

From `UniXNU-msm8996-common/dts/msm8996.dtsi`:

```
gpu: gpu@b00000 {
    compatible = "qcom,adreno-530.2", "qcom,adreno";
    unixnu,adt-name = "gpu";
    reg = <0 0xb00000 0 0x3f000>;
    status = "disabled";
};
```

The board DTS `UniXNU-g0245d/dts/g0245d.dts` enables it:

```
&gpu { status = "okay"; };
```

`devicetree.py` exports only nodes with `unixnu,adt-name` passing
`enabled()`; without the override the linked driver has no
`IOPlatformDevice` nub to match. `probe()` matches the two ADT strings
above, plus defensive aliases (`qcom,kgsl-3d0`, `qcom,kgsl-3d`,
`qcom,adreno-530`) seen in the vendor CAF Android `board.dtb` but never
emitted by the current UniXNU ADT.

## Current state: read-only, unqualified offsets

`start()`:

1. maps the first MMIO region (GPU block at `0xb00000`, size `0x3f000`);
2. reads three RBBM registers (`REG_RBBM_SOFT_RESET` 0x000,
   `REG_RBBM_STATUS` 0x004, `REG_RBBM_CNTL` 0x008);
3. publishes them verbatim as `Adreno530Registers` (OSData) plus
   `Adreno530RbbmStatus` / `Adreno530GpuPhys` / `Adreno530SmmuPhys`;
4. **rejects only the all-ones bus-failure pattern on all three reads**. An
   idle/reset GPU legitimately reads back zeros, so zero is not a failure;
5. on failure records `Adreno530Failure` / `Adreno530Status`, sets
   `Adreno530Probed = false`, releases the mapping and returns false without
   `registerService()`.

`Adreno530OffsetsQualified` is published as **false unconditionally**: the
A5xx offsets above come from a generic guess, not the A530 TRM or the
freedreno a5xx headers. `Adreno530ProbeScope` spells out that there are no
clock/power/VBIF/ringbuffer/GMU operations.

`Adreno530Probed = true` therefore means **only** "MMIO mapped and the reads
were not all all-ones". It is NOT a claim that the GPU is usable.

`Msm8996Platform` remains an embedded kit (not compiled, not linked); all its
power/clock/SMMU/display hooks return -1 and `capabilities()` returns 0.
`ArmSmmu4K` already targets `arm,mmu-500` (SMMUv2) — the gap is an owning
IOService plus hardware init, not a "v2 quirk branch".

## What remains (largest porting item, hardware-qualified)

- On the device, read `Adreno530Registers` first and confirm no MMIO abort.
- Verify **every** RBBM offset against the A530 TRM before any write.
- GPU clock/power via MSM8996 MMCC/GCC + VDD rail (no GMU handshake).
- VBIF config, RBBM soft-reset/enable, GRAS/SP/RB/PC bring-up; ZAP firmware.
- Command ring (direct RB submit), ringbuffer, XNU GPU user-client ABI.
- An owning SMMUv2 component for context banks / DMA.
- MDP5 + DSI0 video-mode display: panel timing, DSI PHY @ 0x994400,
  MDP @ 0x901000. Note the `mdss` node has no `ranges` property, so its
  1-cell children cannot be exported to the ADT until the bus description
  is fixed. See `MSM8996-PORTING-PLAN.md`.
- Metal frontend and turnip/darwin backend wiring.

## Build / wiring (current)

Registered in `unixnu-src/components.toml` as `Adreno530`
(`build-arm-driver.py Adreno530` -> `libAdreno530.a`). Already enabled in
`UniXNU-g0245d/board.toml` and force-loaded into `kernel.macho` through
`CONFIG_XNU_EXTRA_LD_ARCHIVES`; verified with `linkarchives` and `llvm-nm`.

After any defconfig edit, re-run
`./unixnu defconfig g0245d_defconfig -D ../UniXNU-g0245d` to refresh
`out/.../.config`. Until the A5xx path is qualified, display still relies
on the UEFI `GopFramebuffer` handoff.
