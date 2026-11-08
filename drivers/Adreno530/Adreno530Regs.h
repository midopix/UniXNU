// SPDX-License-Identifier: BSD-2-Clause-Patent
// MSM8996 Adreno 530 (A5xx) register offsets (scaffold, unverified).
//
// A5xx register offsets differ from the A6xx/A7xx parts used by Adreno740.
// Only REG_RBBM_STATUS is read by the probe (diagnostic). All offsets here
// MUST be re-verified against the Adreno 530 / A5xx TRM and the freedreno
// A5xx register headers before any GPU programming is attempted.
#pragma once

// G0245D: the kernel component build uses -nostdlibinc and has no libc++
// (<cstdint>); use the C <stdint.h> (provided by the Kernel.framework SDK
// headers) which exposes uint32_t/uint64_t in the global namespace.
#include <stdint.h>

namespace Adreno530 {

// Addresses from msm8996.dtsi (authoritative, transcribed from the SoC DTSI):
//   gpu@b00000         reg = <0 0xb00000 0 0x3f000>, SPI 300
//   iommu@b40000       reg = <0 0xb40000 0 0x10000>  (adreno SMMU, MMU-500 v2)
constexpr uint64_t kGpuPhys       = 0xb00000ull;
constexpr uint64_t kGpuRegSize    = 0x3f000ull;
constexpr unsigned kGpuIrq        = 300;
constexpr uint64_t kAdrenoSmmuPhys = 0xb40000ull;
constexpr uint64_t kAdrenoSmmuSize = 0x10000ull;

// RBBM status/control. THESE OFFSETS ARE UNVERIFIED against the Adreno 530 /
// A5xx TRM and the freedreno a5xx register headers. Adreno register layout
// shifts between generations; the probe reads them for diagnostics only and
// publishes Adreno530OffsetsQualified = false so nobody mistakes a plausible
// read-back for a validated A5xx register map.
constexpr unsigned REG_RBBM_SOFT_RESET = 0x000;
constexpr unsigned REG_RBBM_STATUS     = 0x004;
constexpr unsigned REG_RBBM_CNTL       = 0x008;

// VBIF, GRAS/SP/RB, CP, the command ring and the (absent) GMU are all TODO.
// A5xx has NO GMU: clocks/power come from MMCC/GCC + the VDD rail.

} // namespace Adreno530
