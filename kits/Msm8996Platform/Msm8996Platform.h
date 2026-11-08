// SPDX-License-Identifier: BSD-2-Clause-Patent
// MSM8996 platform operations for the UniXNU native drivers.
//
// This kit is the MSM8996 analogue of the SM8550 `Sm8550Platform` kit. It is
// meant to provide SoC-specific helpers (GPU/display power, clock enables via
// MMCC/GCC, SMMU context-bank ownership) that the driver components
// (Adreno530, MDP5, NativeUfs8996) link against.
//
// For MSM8996 / A5xx there is NO GMU: GPU power and clock are driven directly
// through the MMCC/GCC clock controllers and a VDD rail, unlike the GMU-based
// SM8550 path.
//
// HONEST STATE: this kit currently implements NO hardware operation. Every
// power/clock/SMMU/display entry point returns -1, and
// msm8996_platform_capabilities() returns 0. It is registered as an
// `embedded` build-stage kit (build=[], outputs=[]), so it is NOT compiled
// into an archive and is NOT linked into kernel.macho. The constants below
// are a read-only address table for the future implementation.
#pragma once

#include <stdint.h>

// Known hardware facts transcribed from UniXNU-msm8996-common/dts/msm8996.dtsi.
// ADDRESSES ONLY — no clock, regulator, reset or register programming is
// implemented here. All of them require real-hardware qualification before
// any write to these blocks.
#define MSM8996_GCC_PHYS            0x300000ULL
#define MSM8996_GCC_SIZE            0x90000ULL
#define MSM8996_UFSHC_PHYS          0x624000ULL
#define MSM8996_UFSHC_SIZE          0x2500ULL
#define MSM8996_GPU_PHYS            0xb00000ULL
#define MSM8996_GPU_SIZE            0x3f000ULL
#define MSM8996_ADRENO_SMMU_PHYS    0xb40000ULL
#define MSM8996_ADRENO_SMMU_SIZE    0x10000ULL
#define MSM8996_MDSS_PHYS           0x900000ULL
#define MSM8996_MDP_PHYS            0x901000ULL
#define MSM8996_DSI0_PHYS           0x994000ULL
#define MSM8996_DSI0_PHY_PHYS       0x994400ULL
#define MSM8996_BLSP2_UART2_PHYS    0x75b0000ULL
#define MSM8996_BLSP2_I2C6_PHYS     0x75ba000ULL

// Capability bits for msm8996_platform_capabilities(). All are currently
// clear; a bit may be set only after the corresponding operation has been
// observed working on the device.
enum {
    MSM8996_CAP_GPU_POWER = 1u << 0,
    MSM8996_CAP_GPU_CLOCK = 1u << 1,
    MSM8996_CAP_SMMU_BANK = 1u << 2,
    MSM8996_CAP_DISPLAY   = 1u << 3,
};

#ifdef __cplusplus
extern "C" {
#endif

// Returns 0: the kit is registered but provides no qualified hardware
// operation. Query capabilities() instead of treating "available" as a
// statement that GPU/display/SMMU control exists.
int msm8996_platform_available(void);

// Bitmask of MSM8996_CAP_* operations actually implemented and qualified.
// Currently always 0.
unsigned msm8996_platform_capabilities(void);

// Human-readable scope of this kit, for diagnostics.
const char *msm8996_platform_scope(void);

// GPU (A530) clock/power control. No GMU on A5xx: these must toggle
// MMCC/GCC and the VDD rail directly. Stubs return -1 until qualified.
int msm8996_gpu_power_on(void);
int msm8996_gpu_power_off(void);
int msm8996_gpu_clock_set(uint32_t hz);

// Reserve and return the SMMU context-bank index for a given master name
// (e.g. "gpu", "ufshc"). Returns -1 until an owning SMMUv2 driver exists.
int msm8996_smmu_context_bank(const char *master);

// Display (MDP5/DSI0) power-on hook for the panel bring-up path.
// Returns -1 until the MDP5/DSI bus description and rails are qualified.
int msm8996_display_power_on(void);

#ifdef __cplusplus
}
#endif
