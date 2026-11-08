// SPDX-License-Identifier: BSD-2-Clause-Patent
// MSM8996 BLSP QUP v2 I2C READ-ONLY register probe.
//
// The MSM8996 BLSP blocks are the older "QUP v2" serial engines (not the GENI
// engines used on SM8550). They drive the board's I2C (touch, sensors), SPI
// and UART (the boot console is BLSP2 UART2 @ 0x75b0000).
//
// Scope: map the QUP register block, read the wrapper state/version
// registers and publish them as IORegistry diagnostics. This component does
// NOT configure QUP_CONFIG, take the mini-core out of reset, program the
// clock, touch either FIFO, or perform an I2C transaction.
//
// "BLSP8996Probed = true" means ONLY "the MMIO window was mapped and the
// hard-wired HW_VERSION register did not read back as a bus failure
// pattern". It is NOT a claim that the I2C bus works. See README.md.
//
// ADT binding: devicetree.py exports `blsp2-i2c6` from msm8996.dtsi with
// compatible = "qcom,i2c-qup-v2.2.1". The vendor CAF Android board.dtb uses
// "qcom,i2c-msm-v2" style names; those are accepted below as defensive
// aliases only and are never emitted by the current UniXNU ADT.
//
// NOTE: the early kernel console uses the MSM UARTDM directly via an XNU
// core patch (UniXNU-msm8996-common patches/xnu/0002-msm8996-blsp-console),
// separate from this runtime I2C probe.
#include <IOKit/IOService.h>
#include <IOKit/IOPlatformExpert.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSString.h>
#include <libkern/libkern.h>
#include <pexpert/pexpert.h>
#include "Blsp8996Regs.h"

uint32_t blsp8996_probe_calls = 0;
volatile uint32_t blsp8996_matched = 0;
volatile uint32_t blsp8996_mapped  = 0;
volatile uint32_t blsp8996_status  = 0;
// 1 entered start, 2 mapped, 3 registers read, 4 readback plausible,
// 5 diagnostics published.
volatile uint32_t blsp8996_step    = 0;
const char *volatile blsp8996_failure = nullptr;
volatile uint32_t blsp8996_config       = 0;
volatile uint32_t blsp8996_state        = 0;
volatile uint32_t blsp8996_operation    = 0;
volatile uint32_t blsp8996_error_flags  = 0;
volatile uint32_t blsp8996_hw_version   = 0;

namespace {
const char *const kCompatible[] = {
  // UniXNU ADT (authoritative, from msm8996.dtsi).
  "qcom,i2c-qup-v2.2.1", "qcom,spi-qup-v2.2.1",
  // Defensive aliases seen in vendor CAF device trees.
  "qcom,i2c-msm-v2", "qcom,i2c-qup-v2", "qcom,msm8996-i2c",
};

bool degenerate(uint32_t value)
{
  return value == 0 || value == 0xffffffffu;
}
} // namespace

class BlspI2c : public IOService {
  OSDeclareDefaultStructors(BlspI2c)
  IOMemoryMap *fMap = nullptr;
  uintptr_t fBase = 0;
  static uint32_t read32(uintptr_t base, unsigned offset);
  static bool     contains(const char *data, size_t length, const char *needle);
public:
  IOService *probe(IOService *provider, SInt32 *score) override;
  bool start(IOService *provider) override;
  void stop(IOService *provider) override;
};

OSDefineMetaClassAndStructors(BlspI2c, IOService)

uint32_t BlspI2c::read32(uintptr_t base, unsigned offset)
{
  return *reinterpret_cast<volatile uint32_t *>(base + offset);
}

bool BlspI2c::contains(const char *data, size_t length, const char *needle)
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

IOService *BlspI2c::probe(IOService *provider, SInt32 *score)
{
  blsp8996_probe_calls++;
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
    blsp8996_failure = "provider exposes no readable 'compatible' property";
    return nullptr;
  }
  bool match = false;
  for (size_t index = 0; index < sizeof(kCompatible) / sizeof(kCompatible[0]) && !match; index++) {
    match = contains(text, length, kCompatible[index]);
  }
  if (!match) { return nullptr; }
  blsp8996_matched = 1;
  if (score) { *score = 900; }
  return this;
}

bool BlspI2c::start(IOService *provider)
{
  blsp8996_step = 1;
  if (!IOService::start(provider)) { return false; }

  setProperty("BLSP8996ProbeScope", "register-read-only; no QUP/I2C transactions");

  auto fail = [&](const char *reason, uint32_t status) -> bool {
    blsp8996_failure = reason;
    blsp8996_status  = status;
    setProperty("BLSP8996Failure", reason);
    setProperty("BLSP8996Status", static_cast<unsigned long long>(status), 32);
    setProperty("BLSP8996Probed", false);
    if (fMap) { fMap->release(); fMap = nullptr; }
    fBase = 0;
    return false;
  };

  fMap = provider->mapDeviceMemoryWithIndex(0);
  if (!fMap) { return fail("mapDeviceMemoryWithIndex(0) failed", 1); }
  fBase = static_cast<uintptr_t>(fMap->getVirtualAddress());
  if (!fBase) { return fail("mapped QUP MMIO has a null virtual address", 2); }
  blsp8996_mapped = 1;
  blsp8996_step   = 2;

  // Read-only snapshot of the QUP wrapper. Nothing is written, so the engine
  // stays in whatever state the firmware left it in (usually RESET).
  blsp8996_config      = read32(fBase, Blsp8996::REG_QUP_CONFIG);
  blsp8996_state       = read32(fBase, Blsp8996::REG_QUP_STATE);
  blsp8996_operation   = read32(fBase, Blsp8996::REG_QUP_OPERATION);
  blsp8996_error_flags = read32(fBase, Blsp8996::REG_QUP_ERROR_FLAGS);
  blsp8996_hw_version  = read32(fBase, Blsp8996::REG_QUP_HW_VERSION);
  blsp8996_step        = 3;

  uint32_t snapshot[5] = {
    blsp8996_config, blsp8996_state, blsp8996_operation,
    blsp8996_error_flags, blsp8996_hw_version,
  };
  // Order: QUP_CONFIG, QUP_STATE, QUP_OPERATION, QUP_ERROR_FLAGS,
  // QUP_HW_VERSION.
  setProperty("BLSP8996Registers", snapshot, static_cast<unsigned int>(sizeof(snapshot)));
  setProperty("BLSP8996ControllerPhys", static_cast<unsigned long long>(Blsp8996::kBlsp2I2c6Phys), 64);

  // QUP_STATE/CONFIG/OPERATION/ERROR_FLAGS may all legitimately read zero
  // (engine in RESET, no transaction ever issued). Only the hard-wired
  // HW_VERSION is a usable presence check.
  if (degenerate(blsp8996_hw_version)) {
    return fail("QUP HW_VERSION read back as 0 or all-ones: block unclocked, in reset, MMIO aborted, or wrong offset", 3);
  }
  blsp8996_step = 4;

  blsp8996_status  = 0;
  blsp8996_failure = nullptr;
  setProperty("BLSP8996Status", 0ULL, 32);
  setProperty("BLSP8996HwVersion", static_cast<unsigned long long>(blsp8996_hw_version), 32);
  setProperty("BLSP8996State", static_cast<unsigned long long>(blsp8996_state), 32);
  setProperty("BLSP8996Probed", true);
  registerService();
  blsp8996_step = 5;
  return true;
}

void BlspI2c::stop(IOService *provider)
{
  if (fMap) { fMap->release(); fMap = nullptr; }
  fBase = 0;
  IOService::stop(provider);
}
