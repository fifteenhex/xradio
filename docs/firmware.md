# XR819 firmware notes (fw_xr819.bin reverse engineering)

Notes from disassembling `boot_xr819.bin` and `fw_xr819.bin`
(armbian firmware, sha256 `fb81436a...42a9e1`, label
`WSC_LOAD_FR_A0_00_1000_1: Aug 19 2015 ASIC`). Everything below was read
out of the binaries, not vendor docs.

## Image container format ("XR01")

The driver streams `fw_xr819.bin` bytewise into a download FIFO; the
bootloader parses it as a record stream:

| record | layout | meaning |
|--------|--------|---------|
| header | `"XR01"` (u32) | consumed as first word of the checksum |
| 0 | `{0, addr, size}` + payload | load `size` bytes at `addr` |
| 1 | `{1, addr, value, size}` | fill (BSS) |
| 2 | `{2, nbytes}` + `{addr, value}` pairs | register pokes (RF/PHY init) |
| 3 | `{3, nbytes}` + payload | skipped |
| 4 | `{4, entry, fixup}` | end record, jump to `entry` |

The sum of *all* 32-bit words in the file must be 0 (the `fixup` word
balances it); on mismatch the bootloader reports `DOWNLOAD_ERR_CHECKSUM`.
The `DOWNLOAD_ERR_*` codes in `hwio.h` match the bootloader exactly.

## Memory map / CPU

ARM926-class core (ARM+Thumb, CP15), high region remapped:

- `0x00000000`: 111 KB main code (vectors + Thumb C code)
- `0x04000000`: data + BSS, stacks top out at `0x0400c000`
- `0x08000000`: bootloader load address (AHB SRAM)
- `0x09000000`: shared APB memory window (the driver's `APB_ADDR`)
- `0x0a900000`: debug UART; `0x0ab8xxxx-0x0abbxxxx`: RF/baseband
- `0xfff00000`: boot/warm-restart stub + kernel region, entry point

Firmware sources (from assert strings): `wsmlmac.c`, `hif.c`, `hi_msg.c`,
`lmc_*.c`, `tx_ptcs.c`, `syn_scan.c`, ... — the ST-E CW1200 LMAC lineage.

## WSM dispatch

Host messages are dispatched through a 37-entry table (`0x04000710`) on
`id & 0xc3f`, so ids >= 0x25 and ids with bits 10-11 set are invalid, as
are 0x02, 0x19, 0x1a, 0x1e, 0x1f, 0x21, 0x24. Invalid commands are not
fatal: the firmware echoes a 4-byte confirm (`id | 0x0400`) with *no
status word*. Note 0x19/0x1a (start/stop_find, still present in `wsm.c`
as dead code) are **not implemented** by this firmware.

The startup indication (0x0801) advertises `numInpChBufs = 30`,
`sizeInpChBuf = 1632`.

### Commands implemented by firmware but never sent by the driver

Of the 37 dispatch slots, 24 are used by the driver and 6 are the shared
"invalid" stub (0x02, 0x19, 0x1a, 0x1e, 0x1f, 0x21, 0x24 — note
start_find/stop_find 0x19/0x1a fall here and are dead code in `wsm.c`).
The remaining slots have real handlers the driver never invokes:

| id | handler | what it does |
|----|---------|--------------|
| **0x00** | `0x16025` | **memory READ (peek).** Request = `{u32 addr, u16 len, u16 flags}`; copies `len` bytes (capped at 1024) from `addr` into the confirm. Flag bits select a cache invalidate/clean pass first. Arbitrary read of the whole chip address space — firmware RAM, RF/baseband MMIO at `0x0ab8xxxx`, etc. |
| **0x01** | `0x16095` | **memory WRITE (poke).** Request = `{u32 addr, u16 len, u16 flags, payload...}`; writes the payload to `addr` (byte/half/word per flags), with a cache clean pass. Arbitrary write of the same space. |
| 0x03 | `0x15879` | test/echo helper, confirm id 0x403; optionally allocates a buffer |
| 0x0f | `0x10df7` | config setter (calls an internal MIB-style routine), confirm len 8 |
| 0x14 | `0x006d0d` | per-interface flag setter (ORs bit 3 of an interface struct), confirm len 12 |
| 0x15 | `0x10e83` | config setter, confirm len 8 |
| 0x1d | `0x10f1f` | **table/stats readback**, confirm id 0x41d; returns `count` 12-byte records (`count` from request), i.e. a variable-length telemetry array |
| 0x20 | `0x10f3f` | config setter, confirm id `0x420|if_id`, confirm len 8 |

The peek/poke pair (0x00/0x01) is the interesting one: it is a complete
runtime memory-access backdoor into the "black box" that the vendor
driver never touches. It needs no special unlock — just a normal WSM
message once the firmware is running. Uses: dumping live firmware RAM to
correlate against this disassembly, reading RF/PHY registers to diagnose
the reliability problems, or scripting hardware bring-up without a new
firmware build. The confirm for 0x00 is bounded to 1024 bytes; 0x01 has
no length sanity check of its own, so a bad `addr` will fault the CPU and
trip the exception path.

Note there is **no separate packet-injection command**: raw 802.11 TX is
the normal data path (id 0x0004), which already sends whatever frame the
host hands it. Monitor-mode injection therefore works through the
existing tx path, not a hidden command.

## Host interface rules (violations crash the firmware)

The firmware has no error paths on its host interface — every violated
invariant calls a panic routine that sends exception indication 0x0800
and spins forever. Recovered assert sites that the host can trigger:

| assert | condition |
|--------|-----------|
| `hif.c:879` code 0x32 | host message sequence number (WSM id bits 13-15) != its input ring index mod 8. Sequence numbers must be strictly consecutive |
| `hif.c:869` code 0x06 | WSM header length > actual SDIO transfer length |
| `hif.c:674` code 0x05 | more than 32 outstanding host->device messages |
| `hif.c:437` code 0x30 | 64 device->host messages pending (host stopped reading) |
| `tx_wsm_req_01.c:142` code 0x01 | TX request while internal descriptor pool empty — i.e. host exceeded the advertised 30 input buffers |
| `hi_msg.c:424/450` code 7/8 | firmware message heap exhausted |

There is also *no* bounds check between the DMA'd host message and the
input buffer it lands in (only the header-vs-transfer-length check
above), so writing more than `sizeInpChBuf` bytes corrupts the firmware
heap silently.

On the RX side the firmware stamps every message it queues to the host
with a consecutive 3-bit sequence number (its output ring index mod 8),
which the driver can (now does) use to detect lost/duplicated reads.

## Exception indication (0x0800)

Payload layout (128 bytes total including WSM header):

```
u32 reason;      /* 0..3 = CPU exception, 4 = assert */
u32 regs[18];    /* r0-r12, sp, lr, pc, cpsr, spsr */
char file[48];   /* asserts only: source file name */
```

For asserts, `regs[0..2]` are the assert arguments: file pointer, line,
code — which is why `wsm_handle_exception` prints `reg[1]`/`reg[2]` as
line/reason. The assert table above is mirrored by the decoder in
`wsm_handle_exception`. After sending this the firmware never recovers;
only a full reset + re-download helps.
