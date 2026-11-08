// SPDX-License-Identifier: BSD-2-Clause-Patent
// MSM8996 (Snapdragon 820) UFS 2.0 READ-ONLY register probe.
//
// Scope: map the UFSHCI register block, read the host-controller
// identification/capability registers and publish them as IORegistry
// diagnostics. This component does NOT enable HCE, issue any UIC command,
// build UTRL/UTMRL, submit a UTP transfer, or expose IOBlockStorageDevice.
//
// "UFS8996Probed = true" therefore means ONLY "the MMIO window was mapped
// and the CAP/VER identification registers did not read back as a bus
// failure pattern". It is NOT a claim that UFS storage works, that the link
// is up, or that a root volume can be mounted. See README.md.
//
// ADT binding: devicetree.py exports the `ufshc` node from
// UniXNU-msm8996-common/dts/msm8996.dtsi, whose compatible list is
//   "qcom,msm8996-ufshc", "qcom,ufshc", "jedec,ufs-2.0".
// Those are the strings the probe actually sees. The vendor CAF Android
// board.dtb (Linux 3.18) uses a different naming set; do not confuse the
// vendor DTB with the UniXNU-generated ADT. The extra aliases below are
// defensive only, for a future ADT generated straight from the vendor tree.
#include <IOKit/IOService.h>
#include <IOKit/IOPlatformExpert.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSString.h>
#include <libkern/libkern.h>
#include <pexpert/pexpert.h>
#include <kern/debug.h>
#include "Ufs8996Regs.h"

// Volatile diagnostics surfaced on the boot page, mirroring ShengUfs so the
// bring-up QR/keyframe can show probe progress without a serial console.
uint32_t ufs8996_probe_calls = 0;
volatile uint32_t ufs8996_matched = 0;
volatile uint32_t ufs8996_mapped  = 0;
volatile uint32_t ufs8996_status  = 0;
// 1 entered start, 2 mapped, 3 registers read, 4 readback plausible,
// 5 diagnostics published. A failed probe keeps the step at its failure point.
volatile uint32_t ufs8996_step    = 0;
const char *volatile ufs8996_failure = nullptr;
volatile uint32_t ufs8996_cap    = 0;
volatile uint32_t ufs8996_ver    = 0;
volatile uint32_t ufs8996_hcpid  = 0;
volatile uint32_t ufs8996_hcmid  = 0;
volatile uint32_t ufs8996_ahit   = 0;
volatile uint32_t ufs8996_is     = 0;
volatile uint32_t ufs8996_ie     = 0;
volatile uint32_t ufs8996_hctrl  = 0;

namespace {
const char *const kCompatible[] = {
  // UniXNU ADT (authoritative, from msm8996.dtsi).
  "qcom,msm8996-ufshc", "qcom,ufshc", "jedec,ufs-2.0",
  // Defensive aliases seen in vendor CAF device trees; never emitted by the
  // current ADT, accepted so a vendor-DTB-derived ADT still matches.
  "jedec,ufs-1.1", "qcom,ufshc-msm8996", "qcom,msm8996-ufs",
};

// A read of 0 or all-ones is the classic signature of a clock-gated block,
// an unclaimed MMIO range, or a bus abort, not a valid identification value.
bool degenerate(uint32_t value)
{
  return value == 0 || value == 0xffffffffu;
}
} // namespace

class NativeUfs8996 : public IOService {
  OSDeclareDefaultStructors(NativeUfs8996)
  IOMemoryMap *fMap = nullptr;
  uintptr_t fBase = 0;
  static uint32_t read32(uintptr_t base, unsigned offset);
  static bool     contains(const char *data, size_t length, const char *needle);
public:
  IOService *probe(IOService *provider, SInt32 *score) override;
  bool start(IOService *provider) override;
  void stop(IOService *provider) override;
};

OSDefineMetaClassAndStructors(NativeUfs8996, IOService)

uint32_t NativeUfs8996::read32(uintptr_t base, unsigned offset)
{
  return *reinterpret_cast<volatile uint32_t *>(base + offset);
}

bool NativeUfs8996::contains(const char *data, size_t length, const char *needle)
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

IOService *NativeUfs8996::probe(IOService *provider, SInt32 *score)
{
  ufs8996_probe_calls++;
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
    ufs8996_failure = "provider exposes no readable 'compatible' property";
    return nullptr;
  }
  bool match = false;
  for (size_t index = 0; index < sizeof(kCompatible) / sizeof(kCompatible[0]) && !match; index++) {
    match = contains(text, length, kCompatible[index]);
  }
  if (!match) { return nullptr; }
  ufs8996_matched = 1;
  if (score) { *score = 1000; }
  return this;
}

bool NativeUfs8996::start(IOService *provider)
{
  ufs8996_step = 1;
  if (!IOService::start(provider)) { return false; }

  // Publish the scope first: even a failed probe must never be mistaken for
  // "UFS storage is available".
  setProperty("UFS8996ProbeScope", "register-read-only; no HCE/UIC/UTP commands");

  auto fail = [&](const char *reason, uint32_t status) -> bool {
    ufs8996_failure = reason;
    ufs8996_status  = status;
    setProperty("UFS8996Failure", reason);
    setProperty("UFS8996Status", static_cast<unsigned long long>(status), 32);
    setProperty("UFS8996Probed", false);
    if (fMap) { fMap->release(); fMap = nullptr; }
    fBase = 0;
    return false;
  };

  fMap = provider->mapDeviceMemoryWithIndex(0);
  if (!fMap) { return fail("mapDeviceMemoryWithIndex(0) failed", 1); }
  fBase = static_cast<uintptr_t>(fMap->getVirtualAddress());
  if (!fBase) { return fail("mapped UFSHCI MMIO has a null virtual address", 2); }
  ufs8996_mapped = 1;
  ufs8996_step   = 2;

  // Read-only snapshot. All offsets here are <= 0x2C and are stable across
  // UFSHCI 1.x/2.x. No register is written, including HCTRL.
  ufs8996_cap   = read32(fBase, Ufs8996::REG_CAP);
  ufs8996_ver   = read32(fBase, Ufs8996::REG_VER);
  ufs8996_hcpid = read32(fBase, Ufs8996::REG_HCPID);
  ufs8996_hcmid = read32(fBase, Ufs8996::REG_HCMID);
  ufs8996_ahit  = read32(fBase, Ufs8996::REG_AHIT);
  ufs8996_is    = read32(fBase, Ufs8996::REG_IS);
  ufs8996_ie    = read32(fBase, Ufs8996::REG_IE);
  ufs8996_hctrl = read32(fBase, Ufs8996::REG_HCTRL);
  ufs8996_step  = 3;

  uint32_t snapshot[8] = {
    ufs8996_cap, ufs8996_ver, ufs8996_hcpid, ufs8996_hcmid,
    ufs8996_ahit, ufs8996_is, ufs8996_ie, ufs8996_hctrl,
  };
  // Order matches Ufs8996::REG_* ascending: CAP, VER, HCPID, HCMID,
  // AHIT, IS, IE, HCTRL.
  setProperty("UFS8996Registers", snapshot, static_cast<unsigned int>(sizeof(snapshot)));
  setProperty("UFS8996ControllerPhys", static_cast<unsigned long long>(Ufs8996::kControllerPhys), 64);

  // CAP and VER are hard-wired identification registers: a clocked,
  // present controller returns non-zero, non-all-ones values. IS/IE/HCTRL
  // may legitimately read zero (no interrupts pending, HCE still cleared),
  // so they are diagnostics only and never gate the probe.
  if (degenerate(ufs8996_cap) || degenerate(ufs8996_ver)) {
    return fail("UFSHCI CAP/VER read back as 0 or all-ones: controller unclocked, in reset, or MMIO aborted", 3);
  }
  ufs8996_step = 4;

  ufs8996_status  = 0;
  ufs8996_failure = nullptr;
  setProperty("UFS8996Status", 0ULL, 32);
  setProperty("UFS8996Version", static_cast<unsigned long long>(ufs8996_ver), 32);
  setProperty("UFS8996Capabilities", static_cast<unsigned long long>(ufs8996_cap), 32);
  setProperty("UFS8996ProductID", static_cast<unsigned long long>(ufs8996_hcpid), 32);
  setProperty("UFS8996ManufacturerID", static_cast<unsigned long long>(ufs8996_hcmid), 32);
  setProperty("UFS8996Probed", true);
  registerService();
  ufs8996_step = 5;
  return true;
}

void NativeUfs8996::stop(IOService *provider)
{
  if (fMap) { fMap->release(); fMap = nullptr; }
  fBase = 0;
  IOService::stop(provider);
}
