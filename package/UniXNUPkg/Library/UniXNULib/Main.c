/** @file
  UniXNURun() and logging.

  Copyright (c) 2026, UniXNU contributors. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Internal.h"

#include <Protocol/SerialIo.h>
#include <Protocol/Cpu.h>
#include <Protocol/Timer.h>
#include <Protocol/HardwareInterrupt.h>

STATIC EFI_SERIAL_IO_PROTOCOL  *mSerial;

VOID
UniXNULogInit (
  VOID
  )
{
  if (EFI_ERROR (gBS->LocateProtocol (&gEfiSerialIoProtocolGuid, NULL, (VOID **)&mSerial))) {
    mSerial = NULL;
  }
}

VOID
EFIAPI
UniXNULog (
  IN CONST CHAR8  *Format,
  ...
  )
{
  VA_LIST  Args;
  CHAR8    Line[256];
  UINTN    Prefix;
  UINTN    Size;

  Prefix = AsciiSPrint (Line, sizeof (Line), "UNIXNU: ");
  VA_START (Args, Format);
  AsciiVSPrint (Line + Prefix, sizeof (Line) - Prefix - 2, Format, Args);
  VA_END (Args);
  AsciiStrCatS (Line, sizeof (Line), "\r\n");

  DEBUG ((PcdGet32 (PcdUniXNUDebugLevel), "%a", Line));
  if (mSerial != NULL) {
    Size = AsciiStrLen (Line);
    mSerial->Write (mSerial, &Size, Line);
  }
}

EFI_STATUS
EFIAPI
UniXNURun (
  IN EFI_HANDLE      ImageHandle,
  IN UNIXNU_HOST  Host
  )
{
  EFI_STATUS       Status;
  UNIXNU_CONFIG        Config;
  UNIXNU_UI            Ui;
  UNIXNU_INJECT_STATS  Stats;
  UINTN            Index;
  BOOLEAN          Countdown;
  UNIXNU_ENTRY         *Entry;
  UNIXNU_ENTRY         BootCopy;

  //
  // A boot manager waits for people; the 5 minute BDS watchdog does not.
  //
  gBS->SetWatchdogTimer (0, 0, 0, NULL);
  UniXNULogInit ();
  UniXNULog (
    "UniXNU %a starting as %a, firmware %s rev 0x%x",
    UNIXNU_VERSION_STRING,
    (Host == UniXNUHostFirmware) ? "firmware boot option" : "application",
    (gST->FirmwareVendor != NULL) ? gST->FirmwareVendor : L"?",
    gST->FirmwareRevision
    );

  //
  // G0245D: ported from upstream UniXNU 1833131 (fix(package): connect
  // controllers on every boot path). Storage may not be connected yet
  // whichever way UniXNU was entered. Started straight from BDS it is
  // because nothing has connected the controllers; started as a DXE driver
  // it is because UniXNURamDiskDxe publishes its BlockIo during dispatch,
  // before this runs, so without this the DiskIo -> SimpleFileSystem
  // connection never happens and the initrd volume has no filesystem:
  // payload discovery reports fs 0 and the menu offers no boot entry.
  // Keeping the BDS-only guard is exactly the v103 payload-discovery gap.
  // [boot] connect_all can still turn this off.
  //
  UniXNUConnectAll ();

  UniXNUConfigLoad (ImageHandle, &Config);
  UniXNULog (
    "config: %u entries, %u drivers, %u dt patches, %u acpi tables, %u slices, %u kits loaded, %u skipped",
    (UINT32)Config.EntryCount,
    (UINT32)Config.DriverCount,
    (UINT32)Config.PatchCount,
    (UINT32)Config.AcpiCount,
    (UINT32)Config.SliceCount,
    (UINT32)Config.KitsLoaded,
    (UINT32)Config.KitsSkipped
    );

  UniXNUPublishSlices (&Config);

  //
  // Drivers before the UI: an injected driver may provide the display or
  // the input device the menu needs.
  //
  ZeroMem (&Stats, sizeof (Stats));
  UniXNUInjectDrivers (ImageHandle, &Config, &Stats);
  UniXNUInjectAcpi (&Config, &Stats);

  UniXNUUiInit (&Config, &Ui);

  //
  // G0245D v99-diag: timer-independent hardware diagnostics.
  // CPU=EFI_CPU_ARCH protocol, HW=hardware-interrupt (GIC) protocol,
  // IRQ=CPU interrupts really enabled, TMR=EFI_TIMER_ARCH protocol,
  // PCT=CNTPCT_EL0 delta across a busy loop (0 means the counter is dead).
  // This line is drawn by the menu itself, so it is never overwritten.
  //
  {
    EFI_CPU_ARCH_PROTOCOL  *Cpu;
    VOID                   *Proto;
    BOOLEAN                IrqState;
    UINT32                 CpuOk, HwOk, IrqOn, TmrOk;
    UINT64                 Pct1, Pct2;
    volatile UINTN         Spin;

    CpuOk    = 0;
    IrqOn    = 0;
    IrqState = FALSE;
    if (!EFI_ERROR (gBS->LocateProtocol (&gEfiCpuArchProtocolGuid, NULL, (VOID **)&Cpu))) {
      CpuOk = 1;
      if (!EFI_ERROR (Cpu->GetInterruptState (Cpu, &IrqState)) && IrqState) {
        IrqOn = 1;
      }
    }
    HwOk  = EFI_ERROR (gBS->LocateProtocol (&gHardwareInterruptProtocolGuid, NULL, &Proto)) ? 0 : 1;
    TmrOk = EFI_ERROR (gBS->LocateProtocol (&gEfiTimerArchProtocolGuid, NULL, &Proto)) ? 0 : 1;

    Pct1 = ArmReadCntPctReg ();
    for (Spin = 0; Spin < 300000; Spin++) {
    }
    Pct2 = ArmReadCntPctReg ();

    UnicodeSPrint (
      Ui.Info,
      sizeof (Ui.Info),
      L"drv %u/%u kit %u | CPU%u HW%u IRQ%u TMR%u PCT%u",
      (UINT32)Stats.DriversLoaded,
      (UINT32)Stats.DriversFailed,
      (UINT32)Config.KitsLoaded,
      CpuOk,
      HwOk,
      IrqOn,
      TmrOk,
      (UINT32)(Pct2 - Pct1)
      );
  }

  Countdown = TRUE;
  for ( ; ;) {
    Index  = UniXNUMenuRun (&Ui, &Config, Countdown);
    Entry  = &Config.Entries[Index];
    BootCopy = *Entry;
    if (Entry->Confirm != NULL) {
      UINTN  Choice;

      if (!UniXNUMenuConfirm (&Ui, Entry, &Choice)) {
        UniXNULog ("menu: entry %u not confirmed", (UINT32)Index);
        UnicodeSPrint (Ui.Notice, sizeof (Ui.Notice), L"'%s' cancelled", Entry->Title);
        Countdown = FALSE;
        continue;
      }

      if (Entry->ConfirmCount > 0) {
        //
        // The chosen option's arguments follow the entry's own, on a copy: the entry itself stays as configured.
        //
        UINTN  Length = (Entry->Args != NULL ? StrLen (Entry->Args) : 0) + 1 + StrLen (Entry->ConfirmArgs[Choice]) + 1;

        BootCopy.Args = AllocateZeroPool (Length * sizeof (CHAR16));
        if (BootCopy.Args != NULL) {
          UnicodeSPrint (BootCopy.Args, Length * sizeof (CHAR16), L"%s %s", Entry->Args != NULL ? Entry->Args : L"", Entry->ConfirmArgs[Choice]);
        } else {
          BootCopy.Args = Entry->Args;
        }
      }
    }

    if ((Entry->Type == UniXNUEntryAction) && (Entry->Action == UniXNUActionTools)) {
      UniXNUMenuRunTools (&Ui, &Config);
      Countdown = FALSE;
      continue;
    }

    Status = UniXNUBootEntry (ImageHandle, &Config, &BootCopy);
    if ((Entry->Type == UniXNUEntryAction) && (Entry->Action == UniXNUActionExit)) {
      return EFI_SUCCESS;
    }

    //
    // The payload returned or failed: back to the menu, without the
    // countdown so a broken default cannot loop on a machine with input.
    //
    UnicodeSPrint (Ui.Notice, sizeof (Ui.Notice), L"'%s' returned: %r", Entry->Title, Status);
    UniXNULog ("menu: entry %u returned %r", (UINT32)Index, Status);
    gBS->SetWatchdogTimer (0, 0, 0, NULL);
    Countdown = FALSE;
  }
}
