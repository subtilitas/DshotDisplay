/*
 * DShot out, and the reply back on the same wire.
 *
 * Implements the DShot protocol of ESCs.  DShot is the name of a protocol; it
 * is used here only to say which ESCs this module works with.  The project
 * claims no right in the name.
 *
 * Plain DShot is one state machine holding the line low between frames.
 * Bidirectional DShot is two: a transmitter that releases the line when the
 * frame ends and raises a flag, and a receiver that starts on that flag and
 * samples the reply.  The turnaround is about 30 us after a frame that is
 * itself 27 us long, so it happens in the PIO (programmable input/output)
 * block and not in a loop on the processor.
 *
 * A frame is sent when the caller asks.  Nothing here has a rate of its own:
 * how often an ESC (electronic speed controller) is told what to do is a
 * property of the project, not of the protocol, and it is the caller's with
 * everything else that is timed against the loop.
 *
 * Extended telemetry is asked for by the caller, not here: this file has no
 * frame rate of its own and a command has to be repeated on one.  The caller
 * sends DSHOT_CMD_EDT_ENABLE on the schedule dshot_edt_frame() sets and
 * passes edt to out_dshot_poll(), which is the only thing that tells an
 * extended frame from an electrical period.
 *
 * From rcbench (https://github.com/subtilitas/rcbench), file
 * protocols/dshot/rp2350/out_dshot.h at commit 453cf334.  The one change is
 * that out_dshot_poll() returns a dshot_reply_t instead of a bool, so a
 * caller can count a missing reply and a damaged one apart.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RCBENCH_OUT_DSHOT_H
#define RCBENCH_OUT_DSHOT_H

#include <stdbool.h>
#include <stdint.h>

#include "dshot.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Pins that can be bound at one time.  A project that needs another
 *  number defines it for the whole build. */
#ifndef OUT_DSHOT_MAX_PINS
#define OUT_DSHOT_MAX_PINS 8u
#endif

/**
 * Take @p pin at @p rate_kbit kbit/s.
 *
 * @p bidirectional inverts the line and claims a second state machine, which
 * has to be the one above the transmitter's in the same block: the two
 * rendezvous on a flag relative to their own numbers, so an ESC on one block
 * cannot start another's receiver.  A bind that cannot get that pair is
 * refused rather than falling back to a one-way output that would look like
 * an ESC with no telemetry.
 *
 * A pin that is already bound with the same rate and mode stays as it is and
 * the call returns true.  With a different rate or mode it returns false and
 * the old binding keeps running; release the pin first to change it.
 */
bool out_dshot_bind(uint8_t pin, uint16_t rate_kbit, bool bidirectional);

/** Give back the pin and the state machines. */
void out_dshot_release(uint8_t pin);

/**
 * Send one frame.
 *
 * @p value is 0..2047 and is used as given, so a caller can send a command as
 * readily as a throttle; dshot_throttle() is what maps travel onto it.  The
 * frame is dropped rather than queued if the previous one has not gone yet,
 * because a queue of stale throttle values is worse than a missed update.
 *
 * On a bidirectional pin this also arms the receiver for the reply.
 */
void out_dshot_send(uint8_t pin, uint16_t value, bool telemetry);

/**
 * Stop sending.
 *
 * No frames means no edges, which is what not driving means here.  An ESC
 * stops on silence after its own timeout; sending explicit zeros instead
 * would keep it armed and waiting, which is not what a disarmed bench should
 * look like from the ESC's side.
 */
void out_dshot_stop(uint8_t pin);

/**
 * Take the reply to the last frame, and say which of three things it was.
 *
 * DSHOT_REPLY_NONE on a bidirectional pin that has not answered, on a pin
 * that has sent no frame since the last poll, and always on a plain DShot
 * pin.  A NONE is not an error to report: an ESC that does not do
 * bidirectional DShot never answers, and that is what it looks like.
 *
 * DSHOT_REPLY_BAD when a reply arrived and failed the line code, the group
 * code or the checksum.  DSHOT_REPLY_GOOD fills @p out.
 *
 * @p edt says whether this ESC was asked for extended telemetry and is
 * expected to have accepted it.  It cannot be read off the wire, and getting
 * it wrong only matters for an ESC that does not normalise its exponent: one
 * that does either sets the mantissa's top bit, which reads as a period, or
 * leaves the type nibble at zero, which reads as a period too.
 */
dshot_reply_t out_dshot_poll(uint8_t pin, bool edt, dshot_telem_t *out);

#ifdef __cplusplus
}
#endif

#endif /* RCBENCH_OUT_DSHOT_H */
