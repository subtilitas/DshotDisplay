# KISS ESC telemetry

Status: design, not implemented.
Branch: `dev/telemetry-logging`.

## Why

The firmware currently gets everything it knows about the ESC from Extended
DShot Telemetry, carried inside the bidirectional DShot eRPM frame. EDT is free
— no extra wire — but it is coarse, because each frame has only 8 bits of
payload:

| Quantity | EDT resolution | KISS resolution | Ratio |
|---|---|---|---|
| Voltage | 0.25 V/LSB, 63.75 V max | 0.01 V/LSB, 655.35 V max | 25x |
| Current | 1 A/LSB, 255 A max | 0.01 A/LSB, 655.35 A max | 100x |
| Temperature | 1 °C/LSB | 1 °C/LSB | same |
| Consumption | not carried | 1 mAh/LSB | — |
| eRPM | full eRPM frame resolution | **100 eRPM/LSB** | **KISS is worse** |

On a 6S pack, 0.25 V/LSB means the voltage readout moves in ~1% steps and a sag
of 200 mV is invisible. 1 A/LSB makes idle current unmeasurable and makes any
efficiency figure derived from it meaningless.

The last row is the important one and it drives the whole design: **KISS is not
a replacement for bidirectional DShot.** Its eRPM field is quantised to 100
eRPM, which is far coarser than the eRPM frame we already decode. KISS is a
better source for volts, amps and consumption, and a worse source for RPM.

## Protocol

Confirmed against Betaflight `src/main/sensors/esc_sensor.c`, which is the
reference implementation. 10 bytes, 115200 8N1, sent about 900 µs after the
request.

| Byte | Field | Encoding |
|---|---|---|
| 0 | Temperature | °C, unsigned |
| 1–2 | Voltage | big-endian u16, 0.01 V/LSB |
| 3–4 | Current | big-endian u16, 0.01 A/LSB |
| 5–6 | Consumption | big-endian u16, 1 mAh/LSB |
| 7–8 | eRPM | big-endian u16, 100 eRPM/LSB |
| 9 | CRC8 | over bytes 0..8 |

Big-endian, note — the rest of DShot is not, so this is easy to get backwards.

### CRC8

Poly 0x07, init 0x00, no reflection, no final XOR. This is plain CRC-8, *not*
the 0xD5 variant that several third-hand write-ups claim. Betaflight's form:

```c
static uint8_t updateCrc8(uint8_t data, uint8_t seed) {
    uint8_t crc = data ^ seed;
    for (int i = 0; i < 8; i++)
        crc = (crc & 0x80) ? (0x07 ^ (crc << 1)) : (crc << 1);
    return crc;
}
// crc = 0; for each of bytes 0..8: crc = updateCrc8(byte, crc);
```

A frame whose CRC does not match is dropped whole. There is no partial credit —
a bad CRC on a 10-byte frame could mean any byte is wrong.

## Requesting a frame

This is the part that needs care, because the DShot library we use does not
expose it.

The DShot frame is 11 throttle bits + 1 telemetry-request bit + 4 CRC bits. The
ESC sends a KISS frame on the telemetry wire when it receives a frame with that
bit set. In `bastian2001/pico-bidir-dshot`:

```c
void BidirDShotX1::sendThrottle(uint16_t throttle) {
    if (throttle > 2000) throttle = 2000;
    if (throttle) throttle += 47;
    throttle <<= 1;          // telemetry bit ends up 0, always
    this->sendRaw12Bit(throttle);
}

void BidirDShotX1::sendRaw11Bit(uint16_t data) {
    data = (data << 1) | 1;  // telemetry bit 1, always
    this->sendRaw12Bit(data);
}
```

So `sendThrottle()` can never request telemetry and `sendRaw11Bit()` always
does. Neither is what we want. `sendRaw12Bit()` is public, though, so we build
the 12-bit value ourselves and no library fork is needed:

```c
uint16_t raw = throttle ? throttle + 47 : 0;   // 0 stays 0: motor stop
s_esc->sendRaw12Bit((raw << 1) | wantTelemetry);
```

The `throttle ? ... : 0` is not cosmetic. `sendThrottle()` skips the +47 offset
for zero so that zero means stop rather than 47/2047 throttle, and dropping that
would make "stop" a small forward command.

Setting the bit does not disturb the eRPM reply on the signal line; the two
channels are independent. Cost is one KISS frame's worth of ESC time.

### Cadence

The DShot frame pump runs at `DSHOT_PERIOD_US` = 1000 µs. A KISS frame occupies
~870 µs on the wire (10 bytes × 10 bits ÷ 115200) and the ESC starts it ~900 µs
after the request, so back-to-back requests at 1 kHz would overlap.

Proposal: request every Nth frame, N configurable, default 20 → 50 Hz. That is
far faster than the display can show and leaves the wire idle 95% of the time.
Betaflight uses a 100 ms timeout per request; we can be much tighter with one
ESC because there is no round-robin over four motors.

## Wiring

KISS telemetry needs a **third wire** — the existing setup is signal + ground.
The ESC's telemetry pad goes to an RP2350 UART RX pin. It is transmit-only from
the ESC, so TX is not connected.

The board has GP4 as the DShot signal pin. UART RX candidates that are both free
and brought out on the 2.54 mm headers:

| Pin | UART | Header |
|---|---|---|
| GP5 | UART1 RX | P1 pin 10 |
| GP9 | UART1 RX | P1 pin 2 |
| GP1 | UART0 RX | P2 pin 8 |
| GP21 | UART1 RX | P1 pin 5 |

GP5 is the tentative pick — adjacent to the DShot pin in numbering, and UART1
leaves UART0 alone for the USB-serial debug path. In arduino-pico, UART1 is
`Serial2`.

> **Voltage.** The KISS spec says the line idles at 3.6 V. RP2350 GPIO absolute
> maximum is IOVDD + 0.3 = 3.6 V, so a strictly conforming KISS ESC sits exactly
> at the limit with no margin. Most BLHeli_32 and AM32 ESCs drive 3.3 V and are
> fine. Worth a series resistor (1 k) on the telemetry line as cheap insurance,
> and worth measuring before trusting it. This needs checking on real hardware
> before we call the feature done.

## Merging with EDT

Per-field, prefer KISS, fall back to EDT. Not a global switch: an ESC may send
KISS frames but leave consumption at zero, or send EDT temperature but no KISS
wire at all.

```
volts  = kissFresh && kissHaveVolts ? kiss.volts : edt.volts
amps   = kissFresh && kissHaveAmps  ? kiss.amps  : edt.amps
tempC  = kissFresh                  ? kiss.tempC : edt.tempC
mAh    = kissFresh                  ? kiss.mAh   : (none)
rpm    = always from the bidirectional DShot eRPM frame
```

`kissFresh` = a CRC-valid frame within the last `KISS_STALE_MS` (proposal: 500
ms). Unplugging the telemetry wire mid-session should fall back to EDT within
half a second rather than freezing the last reading.

RPM deliberately never comes from KISS — see the table at the top.

The UI should show which source is live. A number that silently changes
provenance and resolution is worse than no number: 12.25 V from EDT and 12.25 V
from KISS mean different things about what the next reading can be.

### Extending `EscTelemetry`

`EscTelemetry` in `esc_task.h` already has the `have*` flag convention. Add:

```c
float    kissVolts;      /**< 0.01 V resolution. */
float    kissAmps;       /**< 0.01 A resolution. */
int16_t  kissTempC;
uint16_t kissMah;        /**< Cumulative consumption since ESC power-on. */
uint32_t kissLastMs;     /**< millis() of the last CRC-valid frame. */
uint32_t kissGood;       /**< CRC-valid frames since boot. */
uint32_t kissBad;        /**< CRC failures since boot. */
uint32_t kissTimeouts;   /**< Requests that got no reply. */
bool     haveKiss;       /**< True once any frame has been decoded. */
```

Keeping the KISS values distinct from the EDT ones rather than overwriting them
means the UI can show both during bring-up, which is how we validate the decoder
against a source we already trust.

## Where the code runs

Core1 owns DShot and must not block. Two options:

1. **Decode on core1.** UART RX FIFO is 32 bytes, larger than a 10-byte frame,
   so `escTaskPoll()` can drain it without blocking. Keeps all ESC state on one
   core and inside the existing critical section. Adds a bounded but non-zero
   cost to the frame pump.
2. **Decode on core0.** Core1 sets the telemetry bit, core0 owns the UART. Keeps
   core1's timing untouched, but splits ESC state across two cores and needs its
   own synchronisation.

Leaning towards option 1: the FIFO makes the work small and bounded, and the
existing critical-section pattern already covers it. Worth measuring the added
`escTaskPoll()` time before committing.

The AM32 bootloader path (`escTaskSuspend()`) must also stop KISS requests — it
takes the signal pin over entirely, and a UART left enabled would accumulate
garbage.

## Testing

Host tests, following the existing `test/` pattern:

- CRC8 over the Betaflight reference vector, and over an all-zero frame.
- Decode a synthetic frame; check big-endian assembly and each scale factor.
- A frame with one bit flipped is rejected.
- Byte-at-a-time feeding: partial frame, then the rest, decodes correctly.
- Framing recovery: garbage bytes then a good frame. Since KISS has no start
  delimiter, resynchronisation is by request boundary and inter-frame gap, and
  this is where a decoder is most likely to be wrong.
- Merge policy: KISS fresh vs stale vs never-seen, each field.
- The 12-bit frame builder: throttle 0 stays 0; throttle 1 becomes 48; the
  telemetry bit lands in bit 0; CRC unchanged from `sendThrottle()` for the
  same throttle with the bit clear.

## Work breakdown

1. `src/kiss_telem.h` / `.cpp` — CRC8, frame decode, byte-feed state machine.
   Pure, no hardware. Testable on the host.
2. Host tests for the decoder.
3. `KISS_TELEM_PIN`, `KISS_TELEM_UART`, `KISS_REQUEST_EVERY_N`, `KISS_STALE_MS`
   in `config.h`; UART stub in `test/stubs/`.
4. Wire into `esc_task.cpp`: telemetry-bit request path via `sendRaw12Bit()`,
   UART drain in `escTaskPoll()`, new `EscTelemetry` fields.
5. Merge policy + source indicator in the UI.
6. Hardware bring-up: verify the wire level, confirm frames arrive, cross-check
   KISS against EDT on the same ESC.
7. README wiring section.

## Open questions

- Does the ESC need `DSHOT_CMD_SIGNAL_LINE_TELEMETRY_ENABLE` first, or is the
  telemetry bit enough on AM32? Betaflight does not send it, which suggests the
  bit alone is enough, but this wants confirming on the actual ESC.
- Consumption is cumulative since ESC power-on, not since app start. Show raw,
  or subtract a zero taken at arm time?
- Is 50 Hz the right default, or should it follow the UI refresh rate?

## Sources

- [Betaflight `esc_sensor.c`](https://github.com/betaflight/betaflight/blob/master/src/main/sensors/esc_sensor.c) — frame layout and CRC8, definitive
- [DSHOT - the missing Handbook](https://brushlesswhoop.com/dshot-and-bidirectional-dshot/) — DShot framing, telemetry bit, EDT types
- [KISS ESC onewire telemetry protocol](https://www.rcgroups.com/forums/showatt.php?attachmentid=8524039&d=1450424877) — original spec
- [`bastian2001/pico-bidir-dshot`](https://github.com/bastian2001/pico-bidir-dshot) — the DShot library this firmware uses
