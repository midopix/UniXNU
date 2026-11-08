// SPDX-License-Identifier: BSD-2-Clause-Patent
// MSM8996 (Snapdragon 820) UFS 2.0 host-controller register layout.
//
// These offsets follow the JEDEC UFSHCI 2.1 programming interface, which the
// Qualcomm "qcom,msm8996-ufshc" (compatible "jedec,ufs-2.0") controller
// implements. The probe-stage registers (CAP/VER/HCTRL) are stable across
// UFSHCI 2.0/2.1 and are safe to read to confirm the controller is clocked
// and present. The transfer-list registers (UTRL/UTMRL) and the UIC command
// path are the transport layer and are intentionally NOT touched here until
// the controller is qualified on real hardware (see README).
//
// NOTE: offsets >= 0x30 (host-controller status, UTRL/UTMRL bases, UIC
// command registers) are NOT used by this probe. Their exact layout differs
// between UFSHCI revisions and between the JEDEC spec and the Qualcomm
// implementation; every one of them must be re-verified against the
// MSM8996 UFSHCI IP / A5xx-era TRM before any command submission is
// attempted. No such offset is defined here, on purpose.
#pragma once

// G0245D: the kernel component build uses -nostdlibinc and has no libc++
// (<cstdint>); use the C <stdint.h> (provided by the Kernel.framework SDK
// headers) which exposes uint32_t/uint64_t in the global namespace.
#include <stdint.h>

namespace Ufs8996 {

// Base MMIO offset of the standard UFSHCI register block (relative to the
// 0x624000 controller base in msm8996.dtsi).
constexpr unsigned kRegBase = 0x0000;

// Host Controller Capabilities.
constexpr unsigned REG_CAP   = 0x00;
// Host Controller Version (major:31:24, minor:23:16, version:15:8).
constexpr unsigned REG_VER   = 0x04;
// Host Controller Product ID.
constexpr unsigned REG_HCPID = 0x08;
// Host Controller Manufacturer ID.
constexpr unsigned REG_HCMID = 0x0C;
// Auto-Hibernate Idle Timer.
constexpr unsigned REG_AHIT  = 0x10;
// Interrupt Status.
constexpr unsigned REG_IS    = 0x20;
// Interrupt Enable.
constexpr unsigned REG_IE    = 0x24;
// Host Controller Control. Bit 0 (HCE) enables the controller. The probe
// never writes this register; the constants exist only so the read-back
// value can be decoded in diagnostics.
constexpr unsigned REG_HCTRL = 0x2C;
constexpr uint32_t HCTRL_HCE = (1u << 0);
constexpr uint32_t HCTRL_UE  = (1u << 1);

// UFSHCI VER layout, stable across UFSHCI 1.x/2.x.
//   bits 31:24 major, 23:16 minor, 15:8 version suffix.
// A programmed (non-degenerate) value is the probe's presence check.
constexpr unsigned VER_MAJOR_SHIFT   = 24;
constexpr unsigned VER_MINOR_SHIFT   = 16;
constexpr unsigned VER_VERSION_SHIFT = 8;
constexpr uint32_t VER_FIELD_MASK    = 0xffu;

// Controller MMIO window from msm8996.dtsi:
//   reg = <0 0x624000 0 0x2500>; interrupts = <0 265 0x4>.
constexpr uint64_t kControllerPhys = 0x624000ull;
constexpr uint64_t kControllerSize = 0x2500ull;
constexpr unsigned kControllerIrq  = 265;

} // namespace Ufs8996
