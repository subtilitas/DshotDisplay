<!--
SPDX-License-Identifier: MIT
Written from the SD Physical Layer Simplified Specification v9.10 and the
RP2350 datasheet; no third-party SD host-driver source was consulted.
-->

# SDIO card driver

Status: planned. This note is written before the code. The 2.8" board will use
the driver; the 2.0" board uses the library's SPI driver and does not run it.

Terms: SD is Secure Digital. SDIO is Secure Digital input/output, here the SD
bus in SD mode with four data lines (not the SDIO card standard). SPI is Serial
Peripheral Interface. PIO is the RP2350's programmable input/output block, SM
is one PIO state machine, DMA is direct memory access, FIFO is a first-in,
first-out queue, GPIO is general-purpose input/output. CRC is cyclic
redundancy check. SDSC, SDHC and SDXC are the standard, high and extended
capacity card types. RCA is the relative card address, CID the card
identification register, CSD the card-specific data register, OCR the
operation conditions register. FatFs is the FAT file system module the library
carries. USB MSC is USB (Universal Serial Bus) mass storage.

## Why

The SD library DshotDisplay uses, `no-OS-FatFS-SD-SDIO-SPI-RPi-Pico` v3.6.2
(commit 6c644cf), states in its README that its SDIO driver derives from
ZuluSCSI-firmware, which is GPL-3.0 (GNU General Public License version 3).
This driver replaces that one. It is MIT and written from the SD Association's
public specification and the RP2350 datasheet. The library's FatFs copy, its
SPI driver and its glue stay; the build compiles none of the library's
`src/sd_driver/SDIO/` files.

## Interface the library expects

From the library's public headers `src/sd_driver/sd_card.h`,
`src/include/hw_config.h` and `src/sd_driver/sd_card_constants.h`, and from
how `src/sd_driver/sd_card.c` and `src/src/glue.c` call them.

`sd_card_t` carries the configuration (`type`, `sdio_if_p`, card detect), a
state block (`m_Status`, `card_type`, `CSD`, `CID`, `sectors`, a mutex) and
seven function pointers that an interface driver fills in:

| Pointer | Called by | Meaning |
|---|---|---|
| `init` | `disk_initialize()` | Bring the card up. Returns `m_Status`, a FatFs `DSTATUS`; `STA_NOINIT` clear means ready. A card already initialised returns at once. |
| `deinit` | user code | Mark the card uninitialised and release the bus. |
| `read_blocks(card, buf, sector, count)` | `disk_read()` | Read `count` 512-byte sectors. Returns `block_dev_err_t`, 0 on success. |
| `write_blocks(card, buf, sector, count)` | `disk_write()` | Write `count` sectors. |
| `sync` | `disk_ioctl(CTRL_SYNC)` | Finish any write still in progress. |
| `get_num_sectors` | `disk_ioctl(GET_SECTOR_COUNT)`, `sd_log.cpp`, `usb_msc.cpp` | Card capacity in 512-byte sectors, 0 when unknown. |
| `sd_test_com` | user code | True when a card answers on the bus. |

`sd_init_driver()` in `sd_card.c` calls `sd_sdio_ctor(card)` once for every
card whose `type` is `SD_IF_SDIO`; the constructor fills the pointers. That
same file calls `rp2040_sdio_get_sd_status(card, status)` from
`sd_allocation_unit()` to read the 64-byte SD Status. `sd_card.h` includes
`SDIO/rp2040_sdio.h` for the type `sd_sdio_if_state_t`, the dynamic state
inside `sd_sdio_if_t`, and `sd_card.c` includes `SDIO/SdioCard.h` for the two
functions. This driver supplies all three names. `glue.c` maps the
`block_dev_err_t` codes onto FatFs results: `NO_RESPONSE`, `NO_INIT`,
`NO_DEVICE` and `UNUSABLE` become `RES_NOTRDY`; `PARAMETER` becomes
`RES_PARERR`; `WRITE_PROTECTED` becomes `RES_WRPRT`; `CRC`, `WRITE` and the
rest become `RES_ERROR`.

`sd_sdio_if_t` gives the pins (`CLK_gpio`, `CMD_gpio`, `D0_gpio` to
`D3_gpio`), the PIO block (`SDIO_PIO`), the clock (`baud_rate`, in Hz) and
optional drive strengths. `DMA_IRQ_num` and `use_exclusive_DMA_IRQ_handler`
are ignored: this driver polls and uses no interrupt.

## SD protocol facts used

Section numbers are from the Physical Layer Simplified Specification v9.10.
The simplified specification removes the bus timing tables (4.12, 6.6.6);
where the driver needs a timing value it says so below.

**Bus.** CLK from host, CMD bidirectional, DAT0 to DAT3 bidirectional, all
lines pulled high when idle (3.6.1, 4.3.3). The CMD line carries commands and
responses, most significant bit first (3.6.1). After CMD0 the bus is one bit
wide; ACMD6 selects four bits in the transfer state (4.3.1).

**Command, 48 bits** (4.7.2, Table 4-20): start bit 0, transmission bit 1,
6-bit index, 32-bit argument, CRC7, end bit 1.

**Responses** (4.9):

| Type | Bits | Content | CRC7 |
|---|---|---|---|
| R1 | 48 | index, 32-bit card status (4.10.1) | yes |
| R1b | 48 | R1, then busy on DAT0 (4.9.2) | yes |
| R2 | 136 | `111111`, CID or CSD bits [127:1], end bit in place of bit 0 (4.9.3) | inside the register |
| R3 | 48 | `111111`, OCR, `1111111` in the CRC field (4.9.4) | no |
| R6 | 48 | index 3, new RCA [31:16], status bits 23, 22, 19 and 12:0 in [15:0] (4.9.5) | yes |
| R7 | 48 | index 8, voltage accepted [11:8], check pattern echo [7:0] (4.9.6) | yes |

**CRC7** (4.5): G(x) = x^7 + x^3 + 1 over the first 40 bits of a command or
response, or the first 120 bits of CID or CSD. The specification's examples:
CMD0 with argument 0 gives `1001010` (0x4A), CMD17 with argument 0 gives
`0101010` (0x2A), and the CMD17 response with status 0x00000900 gives
`0110011` (0x33).

**CRC16** (4.5): G(x) = x^16 + x^12 + x^5 + 1, initial value 0, over the data
bits of a block. Example: 512 bytes of 0xFF give 0x7FA1. In four-bit mode each
DAT line carries its own CRC16 over its own bits (3.6.1, 4.5).

**Data packet, four bits** (3.6.1, Figure 3-9): every line sends a start bit
0, then data, then its 16 CRC bits, then an end bit 1. Byte n goes out on two
clocks: bits 7, 6, 5, 4 on DAT3, DAT2, DAT1, DAT0, then bits 3, 2, 1, 0. Bytes
go in address order. A 512-byte block is 1 + 1024 + 16 + 1 = 1042 clocks.
Wide registers such as the SD Status go most significant bit first, which is
the same packing with byte 0 holding bits 511 to 504 (Figure 3-10).

**Write CRC status** (3.6.1, Figure 5-8): after each written block the card
answers on DAT0 alone with `0xxx1`, a start bit, three status bits and an end
bit, then holds DAT0 low while it programs. The simplified specification shows
the token but not the meaning of its three bits for SD mode. The driver takes
`010` as accepted, which is the code the SPI data response token uses for the
same event (7.3.3.1, with `101` for a CRC error and `110` for a write error),
and treats every other value as a failure.

**Busy.** DAT0 low after an R1b response or a written block means busy. The
card needs clock edges to release DAT0 (4.4), so the clock runs while the
driver waits.

**Clock** (4.4): the host may stop or slow the clock at any time, except that
it gives 8 clocks after the last response, data block or CRC status token
before it stops. Identification runs at 100 kHz to 400 kHz (4.2.1, 4.4).
Default speed runs up to 25 MHz: TRAN_SPEED 0x32 means 25 MHz (5.3.2). The
driver caps the data clock at the card's TRAN_SPEED.

**Power up** (6.4.1.1): the card accepts its first command 1 ms after power is
stable, after at least 74 clocks with CMD high. The board powers the slot
permanently, so the driver runs 1 ms of clock at 400 kHz, 400 clocks, before
CMD0.

**Initialisation** (4.2.1 to 4.2.3, Figures 4-1 and 4-2):

1. CMD0, no response: every card to idle.
2. CMD8, argument 0x1AA (2.7 V to 3.6 V, check pattern 0xAA), R7 (4.3.13). A
   response with a different echo is an unusable card. No response means a
   version 1.x card, a card that refuses the voltage, or no card.
3. CMD55 with RCA 0, R1, then ACMD41, R3, repeated until OCR bit 31 (power-up
   done) is set, for at least 1 s from the first ACMD41 (4.2.3). The argument
   is HCS (bit 30, only after a CMD8 response) plus the voltage window 3.2 V
   to 3.4 V (OCR bits 20 and 21, Table 5-1). The host keeps polling below
   50 ms intervals when it stops the clock in between (4.4). A card that
   cannot use the window goes to the inactive state and stops answering
   (4.2.2); the driver reports that as a voltage mismatch.
4. OCR bit 30 (CCS) set after a CMD8 response: SDHC or SDXC, block addressing.
   Clear: SDSC, byte addressing (4.3.14).
5. CMD2, R2: CID. CMD3, R6: RCA.
6. CMD9 with the RCA, R2: CSD. CSD version 1 (5.3.2): capacity is
   (C_SIZE + 1) x 2^(C_SIZE_MULT + 2) x 2^READ_BL_LEN bytes; a 2 GB card has
   READ_BL_LEN 10 (4.3.2). CSD version 2 (5.3.3): (C_SIZE + 1) x 512 KiB.
   Version 3 (SDUC) is refused.
7. CMD7 with the RCA, R1b: to the transfer state.
8. CMD55 with the RCA, then ACMD6 with argument 2: four-bit bus (4.3.1,
   Table 4-31).
9. SDSC only: CMD16 with 512, block length (4.3.14). SDHC and SDXC fix it at
   512 bytes.
10. Data clock up.

**Card status** (4.10.1, Table 4-42). Error bits checked in every R1: 31
OUT_OF_RANGE, 30 ADDRESS_ERROR, 29 BLOCK_LEN_ERROR, 28 ERASE_SEQ_ERROR, 27
ERASE_PARAM, 26 WP_VIOLATION, 24 LOCK_UNLOCK_FAILED, 21 CARD_ECC_FAILED, 20
CC_ERROR, 19 ERROR, 16 CSD_OVERWRITE, 3 AKE_SEQ_ERROR. Mask 0xFD390008. Bits 23
COM_CRC_ERROR and 22 ILLEGAL_COMMAND have clear condition B: they describe the
previous command, which got no response at all, so they do not fail the
command whose response carries them. CURRENT_STATE is bits 12:9, 4 is
transfer. OUT_OF_RANGE in the CMD12 response after a read that ends on the
last block is ignored (4.3.3).

**Transfers.** CMD17 reads one block, CMD18 reads blocks until CMD12 (4.3.3).
CMD24 writes one block, CMD25 writes blocks until CMD12 (4.3.4). The argument
is the block number on SDHC and SDXC and the byte address on SDSC (4.3.14).
CMD13 with the RCA returns the card status (R1). ACMD13 returns the 64-byte SD
Status as a data block (4.10.2).

**Timeouts.**

| Wait | Value | Source |
|---|---|---|
| Response to a command | 5 ms | not in the simplified specification; covers a 136-bit response and 64 clocks of turnaround at 100 kHz (2.0 ms) |
| ACMD41 until ready | 1100 ms from the first ACMD41 | 4.2.3: more than 1 s |
| Between ACMD41 polls | 5 ms | 4.4: below 50 ms |
| Each read block, from the command or the previous block | 250 ms | 4.6.2.1: 100 ms minimum |
| Busy after a written block, after CMD12, after CMD7 | 1000 ms | 4.6.2.2: 250 ms SDHC, 500 ms SDXC, more than 500 ms recommended |
| Write block on the wire to its CRC status | twice the bus time of 1100 clocks, plus 1 ms | bus time |
| DMA abort completion | 100 us | RP2350 datasheet 12.6.8.3 |

Every wait in the driver is one of these. A missing card, a card pulled out
and a dead card all end in a timeout and an error code, never a hang.

## PIO design

Two programs, 32 instructions in all, on two state machines of one block. The
command machine generates the clock with side-set and runs the CMD line. The
data machine has no side-set; it watches the clock pin and moves DAT0 to DAT3.
Both are in `src/sdio/sd_pio.pio`.

**Clock.** The command machine's every instruction sets CLK (non-optional
side-set, RP2350 datasheet 11.5.1), low or high. One instruction is half a
clock period, so the machine's clock divider is f_sys / (2 x f_SD) (11.5.5). At
150 MHz that is 3.0 for 25 MHz, 7.5 for 10 MHz and 187.5 for 400 kHz. The
driver keeps the divider at 3 or more, which caps the clock at f_sys / 6. An
instruction that stalls holds its side-set (11.5.1), so a command machine
waiting for its next command parks CLK low. After a response the machine
enters a loop that toggles CLK and does nothing else. That free-running clock
carries read data, busy release and the 8 trailing clocks; the processor stops
it by forcing a jump to the start (11.5.7). The program never needs strict
alternation of high and low: the card accepts stretched phases (4.4) and the
data machine synchronises on edges, not on instruction counts.

**Command and response, 13 instructions** (`sdpio_cmd`). OUT, SET, IN and JMP
pin all map to CMD. The processor pushes two words: 8 bits of bit count, 48
command bits, 8 bits of response count. The program drives CMD, shifts the
48 bits out on falling edges (the card samples on rising edges), releases
CMD, clocks until it reads a 0 on CMD (the response start bit), then shifts
in the stated number of bits: 47 for a 48-bit response or 135 for R2, plus 8
more that give the card its 8 trailing clocks. It pushes the last partial word
and enters the free-running clock loop. Autopull and autopush at 32 bits
(11.5.4). A command without a response (CMD0) leaves the program waiting for a
start bit; the processor waits for the program counter to reach that loop,
lets 8 clocks pass, and stops it.

**Data, 19 instructions** (`sdpio_dat`). IN, OUT and SET map to D0 to D3 and
JMP pin to CLK; every edge is a `wait 0 jmppin` then `wait 1 jmppin`, a WAIT
source the RP2350 adds (11.4.3), so CLK can be any GPIO. The machine runs at
the system clock.

- Receive: the processor sets register Y to the nibble count of one block less
  one: 1039 for 512 bytes (1024 data nibbles and 16 CRC nibbles), 143 for the
  64-byte SD Status. The program waits for DAT0 high, then low (a start bit),
  then the rising edge that carries it, and samples four lines on each of the
  next Y + 1 rising edges, eight nibbles to a word with autopush. It wraps and
  waits for the next block's start bit, so multi-block reads need no
  processor work between blocks. Waiting for DAT0 high first keeps the last
  CRC bit of one block from reading as the next start bit.
- Transmit: the processor sends one stream per block: a header word, the
  128 data words, two CRC words and a tail word. The header holds the nibble
  count less one, 1044, in its top 16 bits and the nibbles F, F, F, 0 below:
  three idle clocks with the lines driven high, then the start bit. The tail's
  first nibble is the end bit. Nibbles change after rising edges, which puts
  half a period of setup before the card's next rising edge. One rising edge
  after the end bit the program releases the lines and samples 16 nibbles,
  two words, from which the processor finds the CRC status token on DAT0. The
  processor watches busy on DAT0 itself.

Sample points. The command machine reads CMD in its falling-edge instruction,
which sees the pad as it was two system clocks earlier through the input
synchroniser (11.5.6.3): late in the high phase. The data machine reads DAT
one system clock after it sees a rising edge, which is two clocks after the
edge reaches the pad. At 150 MHz and 25 MHz both points are about 7 ns after
the rising edge at the pin; at 10 MHz the command point is about 37 ns after
it. The design assumes default-speed timing: the card changes its outputs
after the falling edge and holds them to the next falling edge. The simplified
specification draws that (Figure 6-11) without numbers, so 25 MHz is a bench
question, not a design claim.

**DMA.** Two channels, claimed at the first init and released by `deinit`.

- Receive uses control blocks (RP2350 datasheet 12.6.9.2). A table holds two
  four-word entries per block, written by the control channel into the data
  channel's alias 3 registers (CTRL, WRITE_ADDR, TRANS_COUNT, READ_ADDR_TRIG,
  12.6.3.1): 128 words from the RX FIFO into the caller's buffer, then 2 CRC
  words into a CRC array. The data channel chains back to the control channel
  (12.6.3.2); the last entry chains to itself, which ends the sequence. Byte
  swap (CTRL.BSWAP) turns the FIFO's first-nibble-in-the-top words into bytes
  in address order. A command reads at most 32 blocks, 16 KiB; longer reads
  are split. An unaligned buffer is read one block at a time through an
  aligned 512-byte bounce buffer (12.6.2.1.1).
- Transmit uses one channel, 132 words from a staging buffer into the TX FIFO.
  The processor builds the staging buffer from the caller's bytes and computes
  the CRC on it, so the caller's buffer has no alignment requirement.

**CRC16 on four lines.** Interleaving four CRC16 registers one bit apart is
the same as one 64-bit register over the nibble stream with polynomial
G(x^4) = x^64 + x^48 + x^20 + 1. Writing the stream as S(x) = sum over lines j
of x^j A_j(x^4), each lane's remainder R_j(y) = A_j(y) y^16 mod G(y) gives
S(x) x^64 = sum x^j Q_j(x^4) G(x^4) + sum x^j R_j(x^4), and the last sum has
degree at most 63, so it is the remainder. Its 16 nibbles, top first, are
exactly the 16 CRC clocks on the wire. The driver computes it four bits at a
time from a 16-entry table, and the host suite checks it against four
separate bitwise CRC16s.

**Budget.**

| Resource | Use |
|---|---|
| PIO | 2 state machines, 32 of 32 instructions, on the block in `sd_sdio_if_t.SDIO_PIO`: pio1 in `sd_hw_config.c` |
| DMA | 2 channels, no interrupt |
| RAM (random-access memory) | 1024 B control table, 256 B CRC words, 528 B transmit staging, 512 B bounce buffer |
| Flash | 128 B four-line CRC table, plus code |

pio1 holds nothing else on the 2.8" board; DShot and KISS are on pio2
(STATUS.md, `esc_task.cpp`).

## Layers

- `src/sdio/sd_proto.c`: pure functions. CRC7, CRC16, the four-line CRC,
  command framing and the PIO word layout, response parsing, CSD capacity and
  TRAN_SPEED, write stream packing, receive CRC check, CRC status token.
- `src/sdio/sd_host.c`: the card protocol over a bus interface of function
  pointers (command, clock, receive, transmit, busy, time). Initialisation,
  reads, writes, status, retries, every timeout. No hardware access.
- `src/sdio/sd_pio.c`: the bus interface on PIO and DMA. Thin: it moves words
  and waits, and it decides nothing about the protocol.
- `src/sdio/sd_card_glue.c`: `sd_sdio_ctor()`, the seven callbacks,
  `rp2040_sdio_get_sd_status()`, the lock, `m_Status`, and the mapping onto
  `block_dev_err_t`.
- `src/sdio/compat/SDIO/`: the two header names the library includes.

The host suite compiles the first two as C and runs them against a simulated
card in `test/test_sdio.cpp`.

Errors. A command that gets no response, or a transfer that times out, marks
the card uninitialised (`STA_NOINIT`), so the next FatFs mount runs the full
initialisation again. That is how a removed card and a reinserted card are
told apart without a card-detect line. Reads and writes are retried twice
after a CRC error or a timeout, three attempts in all.

## Pins and clock on DshotDisplay

From `src/sd_hw_config.c`, `src/board_rp2350_touch_lcd_2_8.h` and
`src/config.h`:

| Signal | GPIO |
|---|---|
| CLK | 19 |
| CMD | 20 |
| DAT0 to DAT3 | 21, 22, 23, 24 |

PIO block pio1. Data clock `SD_LOG_SDIO_HZ`, 10 MHz. Initialisation at
400 kHz or less. The pin constraint is D1 to D3 directly after D0, because one
IN or OUT moves all four lines; `board_desc_lcd2_8.cpp` asserts it. CLK and
CMD can be any GPIO. Internal pull-ups are enabled on CMD and DAT0 to DAT3;
pull-downs are never enabled (RP2350-E9).

`tools/sdtest` sweeps the SDIO clock from 25 MHz down to 1 MHz. Commit 036adfe
records the 2.8" board mounting a 64 GB card there at 25 MHz with the previous
driver. Nothing is recorded for this driver.

## Bench tests needed

- Initialisation on an SDSC card (2 GB or less) and on SDHC and SDXC cards.
- Sustained logging at 7.3 kB/s; BUF PEAK and WORST FLUSH on the SD LOG
  screen.
- USB DRIVE: a host reads files, multi-block reads.
- Card pulled out while logging, while idle and while a host reads; then
  reinserted.
- 10 MHz and 25 MHz, with `tools/sdtest` and in the firmware.

## Provenance

Sources used, and nothing else:

- SD Association, *SD Specifications Part 1 Physical Layer Simplified
  Specification*, version 9.10, 2023-12-01: 3.6.1 and Figures 3-7 to 3-10
  (bus protocol, token and data packet formats); 4.2.1 to 4.2.3 and Figures
  4-1 to 4-4 (reset, CMD8, ACMD41, identification); 4.3, 4.3.1, 4.3.3, 4.3.4
  (transfer state, wide bus, read, write); 4.3.13 (CMD8); 4.3.14 (SDSC and
  SDHC addressing); 4.4 (clock control); 4.5 (CRC7 and CRC16, with the
  examples used as test vectors); 4.6.1 and 4.6.2 (errors, timeouts); 4.7.2
  and Tables 4-23 to 4-31 (command format and arguments); 4.9.1 to 4.9.6
  (responses); 4.10.1 and Table 4-42 (card status); 5.1 and Table 5-1 (OCR);
  5.2 (CID); 5.3.1 to 5.3.3 (CSD versions 1 and 2, TRAN_SPEED); 6.4.1.1
  (power-up time); Figure 5-8 (CRC status token shape); 7.3.3.1 (data
  response token codes).
- Raspberry Pi, *RP2350 Datasheet*, build 2026-01-10 (80d627281ed5): 11.2.4
  (shift registers and counters), 11.4.3 (WAIT, including JMPPIN), 11.5.1
  (side-set), 11.5.2 (wrapping), 11.5.4 (autopush and autopull), 11.5.5
  (clock dividers), 11.5.6 (GPIO mapping, output priority, input
  synchronisers), 11.5.7 (forced instructions); 12.6.2.1.1 (alignment),
  12.6.2.2 (transfer count reload), 12.6.3.1 and 12.6.3.2 (aliases,
  chaining), 12.6.8.3 (abort), 12.6.9.2 (control blocks).
- Pico SDK 2.1.1 headers and sources (BSD-3-Clause): `hardware_pio`,
  `hardware_dma`, `hardware_gpio`, `hardware_clocks`, and pioasm.
- The SD library's own files outside its SDIO, ZuluSCSI and SdFat
  directories, for the interface only: `src/CMakeLists.txt`,
  `src/include/hw_config.h`, `src/include/sd_timeouts.h`,
  `src/include/util.h`, `src/sd_driver/sd_card.h`, `sd_card.c`,
  `sd_card_constants.h`, `sd_regs.h`, `sd_timeouts.c`, `dma_interrupts.c` and
  `.h`, `src/src/glue.c`, and `src/sd_driver/SPI/sd_card_spi.c`.

No third-party SD or SDIO host-driver source was consulted: not the library's
`src/sd_driver/SDIO/` files, not its generated `src/sd_driver/rp2040_sdio.pio.h`,
not ZuluSCSI, SdFat or any other implementation.
