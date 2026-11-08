// SPDX-License-Identifier: BSD-2-Clause-Patent
// MSM8996 BLSP QUP v2 register offsets (bring-up scaffold).
//
// These are the classic Qualcomm QUP v2 offsets used by the BLSP I2C/SPI/UART
// engines on MSM8996. REG_QUP_STATE (0x004) is read by the probe to confirm
// the engine is out of reset; the remaining offsets are referenced by the
// transfer-engine TODO in BlspI2c.cpp and must be re-verified against the
// MSM8996 BLSP IP before any transaction is issued.
#pragma once

// G0245D: the kernel component build uses -nostdlibinc and has no libc++
// (<cstdint>); use the C <stdint.h> (provided by the Kernel.framework SDK
// headers) which exposes uint32_t/uint64_t in the global namespace.
#include <stdint.h>

namespace Blsp8996 {

// QUP wrapper registers (shared with the I2C mini-core in BLSP).
constexpr unsigned REG_QUP_CONFIG     = 0x000;
constexpr unsigned REG_QUP_STATE       = 0x004;
constexpr unsigned REG_QUP_OPERATION   = 0x018;
constexpr unsigned REG_QUP_ERROR_FLAGS = 0x01C;
constexpr unsigned REG_QUP_HW_VERSION  = 0x020;

// QUP_STATE decoding, from the Linux i2c-qup / spi-qup bindings. The STATE
// field is legitimately 0 (RESET), so it must NOT be used as a presence
// check; only the hard-wired HW_VERSION register is used for that.
constexpr uint32_t QUP_STATE_MASK  = 0x3u;
constexpr uint32_t QUP_STATE_VALID = (1u << 2);
constexpr uint32_t QUP_STATE_RESET    = 0;
constexpr uint32_t QUP_STATE_RUN_WAIT = 1;
constexpr uint32_t QUP_STATE_PAUSE    = 2;
constexpr uint32_t QUP_STATE_RUN      = 3;

// I2C mini-core registers (within the QUP block).
constexpr unsigned REG_I2C_MASTER_CLK_CTL = 0x078;
constexpr unsigned REG_I2C_MASTER_STATUS  = 0x07C;
constexpr unsigned REG_I2C_MASTER_TX_FIFO = 0x110;
constexpr unsigned REG_I2C_MASTER_RX_FIFO = 0x114;

// Board BLSP I2C bases from msm8996.dtsi:
//   blsp1-i2c3  @ 0x7577000  (unixnu,adt-name = blsp1-i2c3)
//   blsp1-i2c6  @ 0x757a000  (unixnu,adt-name = blsp1-i2c6)
//   blsp1-spi1  @ 0x7575000
//   blsp1-uart2 @ 0x7570000
//   blsp2-uart2 @ 0x75b0000  (boot console)
//   blsp2-i2c6  @ 0x75ba000  (touch bus; the only I2C node currently
//                             enabled in dts/g0245d.dts)
constexpr uint64_t kBlsp2I2c6Phys = 0x75ba000ull;
constexpr uint64_t kBlsp2I2c6Size = 0x1000ull;
constexpr unsigned kBlsp2I2c6Irq  = 106;

// NOTE: REG_QUP_HW_VERSION (0x020) is used as the presence check. Its offset
// is taken from the mainline QUP v2 binding and still needs confirmation
// against the MSM8996 BLSP IP on hardware; the raw value is published as a
// diagnostic so a wrong offset is visible immediately.

} // namespace Blsp8996
