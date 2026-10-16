# KISS ESC telemetry

Terms: ESC is electronic speed controller. DShot is the digital ESC protocol
the tester drives; in bidirectional DShot the ESC answers each frame on the
same wire. EDT is Extended DShot Telemetry, carried in those answers. eRPM is
electrical revolutions per minute, RPM is revolutions per minute. KISS
telemetry is the serial protocol of the KISS ESC line, sent on its own wire.
PIO is the RP2350's programmable input/output, UART is universal asynchronous
receiver-transmitter, GPIO is general-purpose input/output, CRC is cyclic
redundancy check, LSB is least significant bit, FIFO is first-in, first-out
buffer, XOR is exclusive or, UI is user interface, IOVDD is the RP2350's
input/output supply voltage, SDIO is Secure Digital input/output, CI is
continuous integration. AM32 is an open-source ESC firmware.

Status: implemented on both boards: decoder, PIO receiver, request and drain on
core1, merge with EDT, source tags on the main screen, both eRPM sources in the
log. Working on hardware (owner report). Issue #1 separately reports correct
values with a receiver driving the ESC and the display only listening. Which
board each report covers and the line voltage have no record.

## Why

Without the third wire the firmware gets everything it knows about the ESC
from EDT, inside the bidirectional DShot eRPM frame. EDT costs no extra wire,
but it is coarse, because each frame has only 8 bits of payload:

| Quantity | EDT resolution | KISS resolution | Ratio |
|---|---|---|---|
| Voltage | 0.25 V/LSB, 63.75 V max | 0.01 V/LSB, 655.35 V max | 25x |
| Current | 1 A/LSB, 255 A max | 0.01 A/LSB, 655.35 A max | 100x |
| Temperature | 1 °C/LSB | 1 °C/LSB | same |
| Consumption | not carried | 1 mAh/LSB | — |
| eRPM | ~eRPM/256 to eRPM/511 | 100 eRPM/LSB | see below |

On a 6S pack (six cells in series), 0.25 V/LSB moves the voltage readout in
steps of about 1 %, and a sag of 200 mV is invisible. 1 A/LSB makes idle current unmeasurable and any
efficiency figure derived from it meaningless.

The eRPM row is not a simple win either way; see the next section.

## eRPM: which source is finer

The bidirectional DShot eRPM frame does not carry eRPM. It carries a
**period**, as a 3-bit exponent and a 9-bit mantissa. ESCs normalise the
mantissa, which is what EDT's frame detection relies on, so the mantissa sits
in 256..511 and the period step is `2^exponent ≈ period/512`.

That makes bidirectional DShot a **relative** measurement: about 0.2 to 0.4 %
regardless of speed. Its *absolute* eRPM error grows with RPM:

```
d(eRPM) = 60e6/P² × step ≈ eRPM / mantissa,   mantissa ∈ [256, 511]
```

KISS is the opposite: a flat **absolute** 100 eRPM, so its *relative* error
shrinks as RPM rises. The two cross over low:

| Poles | Crossover (mechanical RPM) |
|---|---|
| 12 | ~6,455 |
| 14 | ~5,533 |
| 16 | ~4,841 |

Above that, KISS is the finer measurement. In mechanical RPM after dividing by
`poles/2`, which is the number displayed, on a 14-pole motor:

| Mechanical RPM | Bidir DShot step | KISS step |
|---|---|---|
| 3,000 | 8.4 | 14.3 |
| 5,000 | 11.7 | 14.3 |
| 8,000 | 29.9 | 14.3 |
| 12,000 | 33.6 | 14.3 |
| 20,000 | 46.7 | 14.3 |
| 30,000 | 105.0 | 14.3 |

On a 14-pole motor KISS is flat at about 14 RPM, while the bidirectional DShot
step grows to 105 RPM at 30,000 RPM. Above the crossover, about 6,500
mechanical RPM on 12 poles, 5,500 on 14 and 4,800 on 16, **KISS resolves RPM
better**. Below it, bidirectional DShot does.
[erpm_resolution.py](erpm_resolution.py) is the arithmetic behind both tables.

### Why bidirectional DShot stays the RPM source

Rate and latency, not resolution.

- Bidirectional DShot returns eRPM on **every** frame: 1 kHz at
  `DSHOT_PERIOD_US` = 1000 µs. KISS runs at the request cadence, 50 Hz.
- Anything dynamic (spin-up, step response, oscillation, a desync) lives in
  that 1 kHz stream and is invisible at 50 Hz.
- KISS adds about 900 µs of transport latency plus request scheduling.

Averaging N bidirectional samples beats quantisation only if the signal is
noisy enough to dither it. On a steady RPM the quantisation error is
deterministic and averaging returns the same wrong value.

The log carries both sources, `eRPM[0]` from bidirectional DShot and
`eRPMkiss[0]` from KISS. The main readout shows bidirectional DShot only,
smoothed for display (`RPM_FILTER_SHIFT`); KISS eRPM is not displayed. Which
source the readout should use above the crossover is open and is to be
decided from logged data.

## Protocol

Checked against Betaflight `src/main/sensors/esc_sensor.c`, the reference
implementation. 10 bytes, 115200 baud 8N1, sent about 900 µs after the
request.

| Byte | Field | Encoding |
|---|---|---|
| 0 | Temperature | °C, unsigned |
| 1–2 | Voltage | big-endian u16, 0.01 V/LSB |
| 3–4 | Current | big-endian u16, 0.01 A/LSB |
| 5–6 | Consumption | big-endian u16, 1 mAh/LSB |
| 7–8 | eRPM | big-endian u16, 100 eRPM/LSB |
| 9 | CRC8 | over bytes 0..8 |

The fields are big-endian; the rest of DShot is not.

### CRC8

Polynomial 0x07, init 0x00, no reflection, no final XOR. This is plain CRC-8,
not the 0xD5 variant that several third-hand write-ups claim. Betaflight's
form:

```c
static uint8_t updateCrc8(uint8_t data, uint8_t seed) {
    uint8_t crc = data ^ seed;
    for (int i = 0; i < 8; i++)
        crc = (crc & 0x80) ? (0x07 ^ (crc << 1)) : (crc << 1);
    return crc;
}
// crc = 0; for each of bytes 0..8: crc = updateCrc8(byte, crc);
```

A frame whose CRC does not match is dropped whole. A bad CRC on a 10-byte frame
could mean any byte is wrong. The host test checks `kissCrc8()` against the
published CRC-8 check value for `"123456789"`, 0xF4, not against a value this
code produced.

## Requesting a frame

The DShot frame is 11 throttle bits, 1 telemetry-request bit and 4 CRC bits.
The ESC sends a KISS frame on the telemetry wire when it receives a frame with
that bit set. The DShot driver in `src/dshot/` takes the value and the bit
separately, `out_dshot_send(pin, value, telemetry)`, and adds the checksum.
Command frames always go out with the bit set; throttle frames set it only on
the slots that ask for a KISS reply.

The firmware builds the 12-bit value in `kissBuildDshotPayload()`
(`src/kiss_telem.cpp`), and `esc_task.cpp` splits it into the value and the
bit for the driver:

```c
uint16_t raw = throttle0to2000 ? (uint16_t)(throttle0to2000 + 47) : 0;
return (uint16_t)((raw << 1) | (requestTelemetry ? 1u : 0u));
```

The zero case matters. `sendThrottle()` skips the +47 offset for zero so that
zero means stop rather than 47/2047 throttle; dropping it would make stop a
small forward command. Every throttle frame goes through this builder, with or
without the request bit, and the host tests pin its output to what
`sendThrottle()` produces.

Setting the bit does not disturb the eRPM reply on the signal line; the two
channels are independent.

### Cadence

The DShot frame pump runs at `DSHOT_PERIOD_US` = 1000 µs. A KISS frame
occupies about 870 µs on the wire (10 bytes × 10 bits ÷ 115200 baud) and the
ESC starts it about 900 µs after the request, so requests on consecutive
frames would overlap.

A request goes out on every `KISS_REQUEST_EVERY_N`-th frame, 20 by default,
which is 50 Hz. Values below 2 fail the build. No request goes out while a
DShot command burst (EDT enable, beacon) is queued. At each request the frame
assembler is reset, because KISS has no start delimiter and the request
boundary is the only alignment there is. A reply not complete after
`KISS_REPLY_TIMEOUT_MS`, 10 ms, is counted in `kissTimeouts` and dropped.
Betaflight uses 100 ms per request across four motors; one ESC allows the
shorter timeout.

## Receiving

KISS telemetry needs a **third wire** besides signal and ground. The ESC's
telemetry pad goes to a GPIO; the line is transmit-only from the ESC.

The receiver is a UART built from one PIO state machine
(`src/pio_uart_rx.pio`, 8 cycles per bit), not one of the RP2350's two hardware
UARTs. A hardware UART can receive only on GPIOs where `n % 4 == 1`. The 2.8"
board frees two GPIOs, GP28 and GP29; only GP29 is a UART receive pin, and GP29
carries the ESC signal. So the PIO receiver is what makes KISS possible on that
board. The block and state machine are claimed from whatever is free at run
time, because the DShot driver is always resident and the 2.8" also runs the
SDIO card driver. When no state machine is free the effect is that of an
unplugged wire.

Any free GPIO that is not the ESC's can receive. Defaults:

| Board | KISS pin | Header | Default |
|---|---|---|---|
| RP2350-Touch-LCD-2 | GP5 | P1 pin 10 | on |
| RP2350-Touch-LCD-2.8 | GP28 | J4 pin 11 | off |

The 2.8" default is off because that board brings out exactly two pins and
most users have not soldered the second wire; one tap on CFG → SETUP turns it
on. The SETUP stepper skips the ESC's pin, and on the 2.8" it has no third pin
to step to. Each board descriptor fails the build if a pin override leaves the
board's free set or puts both wires on one pin.

The firmware enables the internal pull-up on the KISS pin, so an unconnected
wire reads idle instead of decoding noise.

> **Voltage.** The KISS specification says the line idles at 3.6 V. The
> RP2350's absolute maximum GPIO voltage is IOVDD + 0.3 V = 3.6 V, so a
> strictly conforming KISS ESC sits at the limit with no margin. The level
> real ESCs drive is not measured. A 1 kΩ series resistor on the line limits
> the current into the pin's clamp diode.

## Merging with EDT

Per field, prefer KISS, fall back to EDT. Not a global switch: an ESC may send
KISS frames with consumption stuck at zero, or EDT temperature with no KISS
wire at all. `escMerge()` in `src/esc_merge.cpp`:

```
volts  = kissFresh ? kiss.volts : (edtFresh ? edt.volts : none)
amps   = kissFresh ? kiss.amps  : (edtFresh ? edt.amps  : none)
tempC  = kissFresh ? kiss.tempC : (edtFresh ? edt.tempC : none)
mAh    = kissFresh ? kiss.mAh   : 0, source none
rpm    = bidirectional DShot eRPM frame; kiss.eRPM kept alongside, not merged
```

`kissFresh` is a CRC-valid frame within the last `KISS_STALE_MS`, 500 ms.
Unplugging the telemetry wire mid-session falls back to EDT within half a
second rather than freezing the last reading. Consumption drops to zero when
KISS goes stale, because a stale mAh beside a live voltage would read as the
motor having stopped drawing current.

EDT fields carry an arrival timestamp each and expire after `EDT_STALE_MS`,
1000 ms, per field (`escFieldFresh()`). A field without an expiry would keep
an unplugged or swapped ESC's last voltage, current and temperature on screen
indefinitely. An expired status block reads `--`, never OK.

The voltage, current and temperature tiles show the merged value with a KISS
or EDT tag, because 12.25 V from EDT means "within a 0.25 V step" and 12.25 V
from KISS means "within 0.01 V". Consumption is shown only while KISS is the
source. The log keeps the sources apart; see
[blackbox-logging.md](blackbox-logging.md#field-set).

### `EscTelemetry` fields

`EscTelemetry` in `src/esc_task.h` carries the KISS values beside the EDT
ones:

```c
float    kissVolts;      /**< 0.01 V resolution. */
float    kissAmps;       /**< 0.01 A resolution. */
int16_t  kissTempC;
uint16_t kissMah;        /**< Consumption since ESC power-on, not since arm. */
uint32_t kissErpm;       /**< 100 eRPM steps. Kept for comparison, not merged. */
uint32_t kissLastMs;     /**< millis() of the last CRC-valid frame. */
uint32_t kissGood;       /**< CRC-valid frames since boot. */
uint32_t kissBad;        /**< CRC failures since boot. */
uint32_t kissTimeouts;   /**< Requests that got no complete reply. */
bool     haveKiss;       /**< True once any frame has been decoded. */
```

`haveKiss` is a plain flag because `kissLastMs` beside it decides freshness.

## Where the code runs

Core1 owns DShot and must not block. Core1 also decodes KISS, inside
`escTaskPoll()`, rather than setting the request bit and leaving the receiver
to core0. That keeps all ESC state on one core, inside the existing critical
section.

The PIO receiver joins its FIFOs into 8 bytes of receive space, about 700 µs
at 115200 baud against 1000 µs between DShot frames. So `kissDrain()` runs on
every pass of core1's loop, ahead of the frame deadline, instead of once per
frame slot. It is a FIFO read that returns at once when the FIFO is empty. The
time it adds to `escTaskPoll()` is not measured.

The AM32 bootloader path (`escTaskSuspend()`) and every rebuild of the DShot
driver stop the KISS receiver first (`kissUartEnd()`), so a receiver left
running cannot feed the decoder noise while another user owns the pin.

## Testing

Host tests in `test/test_kiss.cpp`, run by `runKissTests()`:

- CRC-8 against the published check value, and over an all-zero frame.
- Decoding a synthetic frame: big-endian assembly and each scale factor, and
  the field ranges.
- A frame with one bit flipped is rejected.
- Byte-at-a-time feeding: a partial frame, then the rest.
- Recovery from a bad frame, and resynchronisation after junk bytes.
- The 12-bit payload builder: zero stays zero, throttle 1 becomes 48, the
  request bit lands in bit 0.
- Merge policy: KISS fresh, stale and never seen, per field; staleness across
  the `millis()` rollover; EDT expiry; source labels.

The PIO program and `src/pio_uart_rx.cpp` are device-only. `make -C test
syntax` and the CI permutation job typecheck them; nothing runs them on the
host.

## Where the code is

- `src/kiss_telem.h` and `.cpp`: CRC-8, frame decode, byte-feed state
  machine, the 12-bit payload builder.
- `src/pio_uart_rx.pio`, `.h` and `.cpp`: the PIO receiver.
- `src/esc_task.cpp`: the request bit through `out_dshot_send()`, the drain in
  `escTaskPoll()`, the `EscTelemetry` fields.
- `src/esc_merge.cpp`: the merge policy. The UI draws the source tags.
- `src/config.h`: `KISS_TELEM_ENABLE`, `KISS_PIN_LCD_2`, `KISS_PIN_LCD_2_8`,
  `KISS_ENABLE_LCD_2`, `KISS_ENABLE_LCD_2_8`, `KISS_REQUEST_EVERY_N`,
  `KISS_STALE_MS`, `KISS_REPLY_TIMEOUT_MS`. The pin and the enable are
  runtime settings on CFG → SETUP; the macros are their defaults.
- README.md, section "KISS telemetry (optional third wire)": the wiring.

## Hardware record

The owner reports KISS telemetry working on hardware, the display's own
telemetry request included. Issue #1 separately reports correct values with a
receiver driving the ESC and the display only listening. Which board each
report covers is not recorded. The line level is not measured.
[STATUS.md](../../STATUS.md) holds the full verification record.

## Open questions

- Does an ESC need `DSHOT_CMD_SIGNAL_LINE_TELEMETRY_ENABLE` first, or is the
  telemetry bit enough on AM32? The firmware does not send it, as Betaflight
  does not. Unconfirmed on an ESC.
- Consumption counts from ESC power-on, and the firmware shows it raw. Show it
  raw, or subtract a zero taken at arming?
- Is 50 Hz the right default, or should it follow the UI refresh rate?
- Which eRPM source should the main readout use above the crossover? Decide
  from logged data, not from the arithmetic above: the analysis assumes the
  ESC's period measurement is exact, and the real noise floor may swamp the
  difference.
- If KISS eRPM proves the better steady-state number, is a fused estimate worth
  it (bidirectional DShot for dynamics, KISS to correct slow bias), or is that
  more machinery than the readout justifies?

## Sources

- [Betaflight `esc_sensor.c`](https://github.com/betaflight/betaflight/blob/master/src/main/sensors/esc_sensor.c): frame layout and CRC8
- [DSHOT - the missing Handbook](https://brushlesswhoop.com/dshot-and-bidirectional-dshot/): DShot framing, telemetry bit, EDT types
- [KISS ESC onewire telemetry protocol](https://www.rcgroups.com/forums/showatt.php?attachmentid=8524039&d=1450424877): original specification
- [rcbench `protocols/dshot`](https://github.com/subtilitas/rcbench/tree/453cf334/protocols/dshot): the origin of the DShot driver in `src/dshot/`
