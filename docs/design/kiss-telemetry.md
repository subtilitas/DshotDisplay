# KISS ESC telemetry

Status: implemented (decoder, core1 integration, merge, UI); not yet run against a real ESC.
Branch: `dev/telemetry-logging`, continued on `dev/multiboard-sdio`.

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
| eRPM | ~eRPM/256 to eRPM/511 | 100 eRPM/LSB | see below |

On a 6S pack, 0.25 V/LSB means the voltage readout moves in ~1% steps and a sag
of 200 mV is invisible. 1 A/LSB makes idle current unmeasurable and makes any
efficiency figure derived from it meaningless.

The eRPM row is not a simple win either way — see the next section.

## eRPM: which source is actually finer

Worth working out rather than assuming, because the intuition that "100 eRPM/LSB
is coarse" is wrong for the motors this tool is used with.

The bidirectional DShot eRPM frame does not carry eRPM. It carries a **period**,
as a 3-bit exponent and a 9-bit mantissa. ESCs normalise the mantissa — that is
exactly what EDT's frame detection relies on — so the mantissa sits in 256..511
and the period step is `2^exponent ≈ period/512`.

That makes bidirectional DShot a **relative** measurement: about 0.2–0.4%
regardless of speed. Its *absolute* eRPM error therefore grows with RPM:

```
d(eRPM) = 60e6/P² × step ≈ eRPM / mantissa,   mantissa ∈ [256, 511]
```

KISS is the opposite: a flat **absolute** 100 eRPM, so its *relative* error
shrinks as RPM rises. The two cross over, and the crossover is low:

| Poles | Crossover (mechanical RPM) |
|---|---|
| 12 | ~6,455 |
| 14 | ~5,533 |
| 16 | ~4,841 |

Above that, KISS is the finer measurement. In mechanical RPM after dividing by
`poles/2` — which is the number actually displayed — on a 14-pole motor:

| Mechanical RPM | Bidir DShot step | KISS step |
|---|---|---|
| 3,000 | 8.4 | 14.3 |
| 5,000 | 11.7 | 14.3 |
| 8,000 | 29.9 | 14.3 |
| 12,000 | 33.6 | 14.3 |
| 20,000 | 46.7 | 14.3 |
| 30,000 | 105.0 | 14.3 |

KISS is flat at ~14 RPM while bidirectional DShot degrades to 105 RPM at 30k.
For a 12–16 pole motor anywhere above idle, **KISS resolves RPM better**.

### So why keep bidirectional DShot as the RPM source

Rate and latency, not resolution.

- Bidirectional DShot returns eRPM on **every** frame — 1 kHz at the current
  `DSHOT_PERIOD_US`. KISS runs at the request cadence, ~50 Hz proposed.
- Anything dynamic — spin-up, step response, oscillation, a desync — lives in
  that 1 kHz stream and is invisible at 50 Hz.
- KISS adds ~900 µs of transport latency plus request scheduling.

For the **log**, temporal resolution is the whole point, so eRPM comes from
bidirectional DShot. For the **display**, which repaints far slower than either
source, KISS is arguably the better number above the crossover.

One caveat before treating averaging as a fix: averaging N bidirectional samples
only beats quantisation if the underlying signal is noisy enough to dither it. On
a genuinely steady RPM the quantisation error is deterministic and averaging
returns the same wrong value more confidently.

Proposal: log both, display bidirectional DShot with KISS shown alongside during
bring-up, and revisit which one the main readout uses once there is hardware to
compare them on. Logging both costs one field and settles the question with
data instead of arithmetic.

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

> **Since implemented:** the receiver is no longer one of the two hardware
> UARTs. It is a UART built from one PIO state machine (`src/pio_uart_rx.pio`),
> because "the KISS pin must be a GPIO one of the PL011s can receive on" ruled
> out every pin on the 2.8" board — it frees two, only GP29 of them is a UART RX,
> and GP29 carries the ESC signal. Everything below about *requesting* a frame
> and decoding it is unchanged; only what turns the line into bytes is different.

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
leaves UART0 alone, in case a build ever wants a debug UART instead of USB CDC.

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
volts  = kissFresh ? kiss.volts : (edtFresh ? edt.volts : none)
amps   = kissFresh ? kiss.amps  : (edtFresh ? edt.amps  : none)
tempC  = kissFresh ? kiss.tempC : (edtFresh ? edt.tempC : none)
mAh    = kissFresh ? kiss.mAh   : none
rpm    = bidirectional DShot eRPM frame; kiss.eRPM kept alongside, not merged
```

`kissFresh` is a CRC-valid frame within the last `KISS_STALE_MS` (500 ms).
Unplugging the telemetry wire mid-session falls back to EDT within half a second
rather than freezing the last reading.

Both levels expire, and that second `edtFresh` was an afterthought — it is not
in the original sketch of this table, and its absence was a real bug. EDT sat
behind `have*` booleans that could only ever go true, so an ESC that was
unplugged or swapped left its last voltage, current and temperature on screen
indefinitely, indistinguishable from live data. EDT fields now carry arrival
timestamps and expire after `EDT_STALE_MS` (1 s), per field. See
`escFieldFresh()`.

RPM is the one field that is *not* merged. Both sources are kept and logged
separately — see the eRPM section above for why neither is strictly better.

The UI should show which source is live. A number that silently changes
provenance and resolution is worse than no number: 12.25 V from EDT and 12.25 V
from KISS mean different things about what the next reading can be.

### Extending `EscTelemetry`

`EscTelemetry` lives in `esc_task.h`. Add:

```c
float    kissVolts;      /**< 0.01 V resolution. */
float    kissAmps;       /**< 0.01 A resolution. */
int16_t  kissTempC;
uint16_t kissMah;        /**< Cumulative consumption since ESC power-on. */
uint32_t kissErpm;       /**< 100 eRPM steps. Kept for comparison, not merged. */
uint32_t kissLastMs;     /**< millis() of the last CRC-valid frame. */
uint32_t kissGood;       /**< CRC-valid frames since boot. */
uint32_t kissBad;        /**< CRC failures since boot. */
uint32_t kissTimeouts;   /**< Requests that got no reply. */
bool     haveKiss;       /**< True once any frame has been decoded. */
```

`haveKiss` is a plain flag because KISS freshness is decided by `kissLastMs`
beside it. The EDT fields have no such companion and originally used `have*`
flags alone, which is exactly why they never expired; they now carry one
timestamp each.

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
3. `KISS_TELEM_PIN`, `KISS_UART`, `KISS_REQUEST_EVERY_N`, `KISS_STALE_MS`
   in `config.h`; UART stub in `test/stubs/`.
4. Wire into `esc_task.cpp`: telemetry-bit request path via `sendRaw12Bit()`,
   UART drain in `escTaskPoll()`, new `EscTelemetry` fields.
5. Merge policy + source indicator in the UI. *Done: the voltage, current and
   temperature tiles read through escMerge() and carry a KISS/EDT tag.*
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
- Which eRPM source should the main readout use above the crossover? Decide from
  logged data, not from the arithmetic above — the analysis assumes the ESC's
  period measurement is itself exact, and the real noise floor may swamp the
  difference entirely.
- If KISS eRPM turns out to be the better steady-state number, is a fused
  estimate worth it — bidirectional DShot for dynamics, KISS to correct slow
  bias — or is that more machinery than the readout justifies?

## Sources

- [Betaflight `esc_sensor.c`](https://github.com/betaflight/betaflight/blob/master/src/main/sensors/esc_sensor.c) — frame layout and CRC8, definitive
- [DSHOT - the missing Handbook](https://brushlesswhoop.com/dshot-and-bidirectional-dshot/) — DShot framing, telemetry bit, EDT types
- [KISS ESC onewire telemetry protocol](https://www.rcgroups.com/forums/showatt.php?attachmentid=8524039&d=1450424877) — original spec
- [`bastian2001/pico-bidir-dshot`](https://github.com/bastian2001/pico-bidir-dshot) — the DShot library this firmware uses
