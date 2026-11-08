// SPDX-License-Identifier: BSD-2-Clause-Patent
// MSM8996 Adreno 530 (A5xx) READ-ONLY register probe.
//
// The Adreno 530 is an A5xx part. Unlike the A6xx/A7xx parts driven by the
// SM8550 Adreno740 component, A5xx has NO GMU: power and clock are managed
// through the MSM8996 MMCC/GCC and a regulator/voltage rail, and the command
// model uses the older RB/CP interface rather than HFI/GMU. A real A5xx
// bring-up is a large, hardware-qualified effort and is the single largest
// item in the MSM8996 porting plan.
//
// Scope: map the GPU MMIO block, read three RBBM registers and publish them
// as IORegistry diagnostics. This component does NOT enable clocks or power,
// soft-reset the GPU, configure VBIF, build a ringbuffer, submit commands,
// adopt ZAP firmware, or initialise MDP5/DSI display.
//
// IMPORTANT: the A5xx RBBM offsets in Adreno530Regs.h are UNVERIFIED. The
// probe therefore publishes Adreno530OffsetsQualified = false unconditionally
// and "Adreno530Probed = true" means ONLY "the MMIO window mapped and the
// reads did not all come back as all-ones (the bus-failure pattern)". It is
// NOT a claim that the GPU is usable. See README.md.
//
// ADT binding: devicetree.py exports `gpu` from msm8996.dtsi with
// compatible = "qcom,adreno-530.2", "qcom,adreno". The vendor CAF Android
// board.dtb uses KGSL-style names (qcom,kgsl-3d0); those are accepted below
// as defensive aliases only and are never emitted by the current ADT.
#include <IOKit/IOService.h>
#include <IOKit/IOPlatformExpert.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSString.h>
#include <libkern/libkern.h>
#include <pexpert/pexpert.h>
#include "Adreno530Regs.h"

uint32_t adreno530_probe_calls = 0;
volatile uint32_t adreno530_matched      = 0;
volatile uint32_t adreno530_mapped       = 0;
volatile uint32_t adreno530_status       = 0;
// 1 entered start, 2 mapped, 3 registers read, 4 readback plausible,
// 5 diagnostics published.
volatile uint32_t adreno530_step         = 0;
const char *volatile adreno530_failure   = nullptr;
volatile uint32_t adreno530_rbbm_soft_reset = 0;
volatile uint32_t adreno530_rbbm_status     = 0;
volatile uint32_t adreno530_rbbm_cntl       = 0;

namespace {
const char *const kCompatible[] = {
  // UniXNU ADT (authoritative, from msm8996.dtsi).
  "qcom,adreno-530.2", "qcom,adreno",
  // Defensive aliases seen in vendor CAF / KGSL device trees.
  "qcom,kgsl-3d0", "qcom,kgsl-3d", "qcom,adreno-530",
};

bool busFailurePattern(uint32_t value)
{
  return value == 0xffffffffu;
}
} // namespace

class NativeAdreno530 : public IOService {
  OSDeclareDefaultStructors(NativeAdreno530)
  IOMemoryMap *fMap = nullptr;
  uintptr_t fBase = 0;
  static uint32_t read32(uintptr_t base, unsigned offset);
  static bool     contains(const char *data, size_t length, const char *needle);
public:
  IOService *probe(IOService *provider, SInt32 *score) override;
  bool start(IOService *provider) override;
  void stop(IOService *provider) override;
};

OSDefineMetaClassAndStructors(NativeAdreno530, IOService)

uint32_t NativeAdreno530::read32(uintptr_t base, unsigned offset)
{
  return *reinterpret_cast<volatile uint32_t *>(base + offset);
}

bool NativeAdreno530::contains(const char *data, size_t length, const char *needle)
{
  size_t wanted = 0;
  while (needle[wanted]) { wanted++; }
  for (size_t at = 0; at + wanted <= length; at++) {
    size_t index = 0;
    while (index < wanted && data[at + index] == needle[index]) { index++; }
    if (index == wanted) { return true; }
  }
  return false;
}

IOService *NativeAdreno530::probe(IOService *provider, SInt32 *score)
{
  adreno530_probe_calls++;
  OSObject *property = provider->getProperty("compatible");
  OSData   *data   = OSDynamicCast(OSData, property);
  OSString *string = OSDynamicCast(OSString, property);
  const char *text = nullptr;
  size_t length = 0;
  if (data) {
    text = static_cast<const char *>(data->getBytesNoCopy());
    length = data->getLength();
  } else if (string) {
    text = string->getCStringNoCopy();
    length = string->getLength();
  }
  if (!text) {
    adreno530_failure = "provider exposes no readable 'compatible' property";
    return nullptr;
  }
  bool match = false;
  for (size_t index = 0; index < sizeof(kCompatible) / sizeof(kCompatible[0]) && !match; index++) {
    match = contains(text, length, kCompatible[index]);
  }
  if (!match) { return nullptr; }
  adreno530_matched = 1;
  if (score) { *score = 1000; }
  return this;
}

bool NativeAdreno530::start(IOService *provider)
{
  adreno530_step = 1;
  if (!IOService::start(provider)) { return false; }

  setProperty("Adreno530ProbeScope",
              "register-read-only; A5xx offsets UNVERIFIED; no clocks/power/VBIF/ringbuffer/GMU");
  // Never claim the register map has been qualified: the three offsets below
  // come from a generic A5xx guess, not the A530 TRM.
  setProperty("Adreno530OffsetsQualified", false);

  auto fail = [&](const char *reason, uint32_t status) -> bool {
    adreno530_failure = reason;
    adreno530_status  = status;
    setProperty("Adreno530Failure", reason);
    setProperty("Adreno530Status", static_cast<unsigned long long>(status), 32);
    setProperty("Adreno530Probed", false);
    if (fMap) { fMap->release(); fMap = nullptr; }
    fBase = 0;
    return false;
  };

  fMap = provider->mapDeviceMemoryWithIndex(0);
  if (!fMap) { return fail("mapDeviceMemoryWithIndex(0) failed", 1); }
  fBase = static_cast<uintptr_t>(fMap->getVirtualAddress());
  if (!fBase) { return fail("mapped GPU MMIO has a null virtual address", 2); }
  adreno530_mapped = 1;
  adreno530_step   = 2;

  // Read-only snapshot. A5xx RBBM offsets are unqualified; the values are
  // published verbatim so a wrong offset is obvious on the first device boot.
  adreno530_rbbm_soft_reset = read32(fBase, Adreno530::REG_RBBM_SOFT_RESET);
  adreno530_rbbm_status     = read32(fBase, Adreno530::REG_RBBM_STATUS);
  adreno530_rbbm_cntl       = read32(fBase, Adreno530::REG_RBBM_CNTL);
  adreno530_step            = 3;

  uint32_t snapshot[3] = {
    adreno530_rbbm_soft_reset, adreno530_rbbm_status, adreno530_rbbm_cntl,
  };
  // Order: RBBM_SOFT_RESET, RBBM_STATUS, RBBM_CNTL.
  setProperty("Adreno530Registers", snapshot, static_cast<unsigned int>(sizeof(snapshot)));
  setProperty("Adreno530GpuPhys", static_cast<unsigned long long>(Adreno530::kGpuPhys), 64);

  // An idle/reset GPU legitimately reads back zeros, so zero is NOT treated
  // as a failure here. Only the all-ones pattern (unclaimed MMIO / bus
  // abort) is rejected. This deliberately weak check is why
  // Adreno530OffsetsQualified stays false.
  if (busFailurePattern(adreno530_rbbm_soft_reset) &&
      busFailurePattern(adreno530_rbbm_status) &&
      busFailurePattern(adreno530_rbbm_cntl)) {
    return fail("all three RBBM reads returned all-ones: GPU unpowered, unclocked, or MMIO aborted", 3);
  }
  adreno530_step = 4;

  adreno530_status  = 0;
  adreno530_failure = nullptr;
  setProperty("Adreno530Status", 0ULL, 32);
  setProperty("Adreno530RbbmStatus", static_cast<unsigned long long>(adreno530_rbbm_status), 32);
  setProperty("Adreno530SmmuPhys", static_cast<unsigned long long>(Adreno530::kAdrenoSmmuPhys), 64);
  setProperty("Adreno530Probed", true);
  registerService();
  adreno530_step = 5;
  return true;
}

void NativeAdreno530::stop(IOService *provider)
{
  if (fMap) { fMap->release(); fMap = nullptr; }
  fBase = 0;
  IOService::stop(provider);
}
