// SPDX-License-Identifier: BSD-2-Clause-Patent
// MSM8996 platform operations — address table plus honest unimplemented stubs.
//
// This file deliberately contains NO MMIO access. Enabling an A5xx GPU clock,
// a VDD rail, an SMMUv2 context bank or the MDP5/DSI rails is exactly the
// class of operation that cannot be validated offline: the clock/reset
// sequencing, regulator IDs and PHY timing all come from the board's GCC and
// PMIC configuration and must be observed on hardware. Returning -1 here is
// the correct behaviour until then.
#include "Msm8996Platform.h"

int msm8996_platform_available(void)
{
    // Used to return 1 ("the kit exists"), which callers could misread as
    // "GPU/display power control is available". 0 until at least one
    // capability bit is qualified on the device.
    return 0;
}

unsigned msm8996_platform_capabilities(void)
{
    return 0;
}

const char *msm8996_platform_scope(void)
{
    return "address-table-only; no MMCC/GCC clock enables, no VDD rail, "
           "no SMMUv2 context bank, no MDP5/DSI power sequencing";
}

int msm8996_gpu_power_on(void)
{
    // TODO: MMCC/GCC GPU clock enables + VDD regulator, A5xx has no GMU.
    return -1;
}

int msm8996_gpu_power_off(void)
{
    return -1;
}

int msm8996_gpu_clock_set(uint32_t hz)
{
    (void)hz;
    return -1;
}

int msm8996_smmu_context_bank(const char *master)
{
    // TODO: needs an owning SMMUv2 (arm,mmu-500) IOService first. ArmSmmu4K
    // already targets MMU-500; there is no "v2 quirk branch" to add.
    (void)master;
    return -1;
}

int msm8996_display_power_on(void)
{
    // TODO: MDP5/DSI0 rails + DSI PHY 28nm init; the ADT mdss children are
    // still disabled because mdss has no "ranges" property.
    return -1;
}
