/**
 * @file usb_msc.h
 * @brief Serving the microSD card to a host as a read-only USB drive.
 *
 * The board is already a USB device — it appears as a serial port. This adds a
 * second interface to the same device, a mass storage one, so that a host can
 * copy logs off the card without anybody finding the microSD adapter that lives
 * in a drawer. @see usb_dev.h for the descriptors that make one device out of
 * two interfaces.
 *
 * @section msc_exclusive Why this is a handover and not a mode
 *
 * A FAT filesystem has no arbitration. Two writers with independent caches will
 * corrupt it, and "two writers" here means the firmware's FatFs and the host's
 * filesystem driver, neither of which can see the other. So the card is never
 * shared: it is *handed over*. The logger flushes, closes and unmounts, and only
 * then does the medium appear to the host. Nothing in sd_log.cpp may touch the
 * card again until the host has let go.
 *
 * That is what @ref MscState is: not a display mode but who owns the card.
 *
 * @section msc_readonly Why read-only
 *
 * The use case is getting `.BFL` files onto a laptop, and read-only buys three
 * things for the cost of nothing anyone wanted: a host filesystem driver cannot
 * corrupt the card, the log numbering cannot be disturbed under the firmware's
 * feet, and there is no need to re-scan on eject to discover what changed. The
 * refusal is reported properly, as a write-protect sense code, so hosts say
 * "disk is write protected" rather than failing in some inventive way.
 *
 * @section msc_medium How "no card in the reader" is expressed
 *
 * The mass storage interface is always enumerated, so the host always sees a
 * removable drive. Outside @ref MscState::Serving it answers TEST UNIT READY
 * with "medium not present", which is exactly what a card reader with an empty
 * slot does, and every host already knows what to do about it. Pressing USB
 * DRIVE is the software equivalent of pushing a card into the slot.
 *
 * The rules below are pure functions for the same reason edtAutoAction() is:
 * this file's other half pulls in TinyUSB and cannot be linked into the host
 * suite, and a rule no test can reach is a rule that will be wrong.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Who owns the card.
 *
 * The two transitional states are not decoration. Unmounting flushes and can
 * take milliseconds, and remounting re-reads the FAT; doing either inside the
 * touch handler that asked for it would stall the UI mid-frame, and doing it
 * without a state to sit in means the frame after the tap has no honest answer
 * to "what is happening".
 */
enum class MscState : uint8_t {
	Idle = 0,   /**< The firmware owns the card. Host sees an empty reader. */
	Handover,   /**< Flushing and unmounting. Host still sees an empty reader. */
	Serving,    /**< The host owns the card. The firmware must not touch it. */
	Reclaim,    /**< Host has let go; remounting. */
};

/** @brief What just happened, as far as the state machine is concerned. */
enum class MscEvent : uint8_t {
	None = 0,
	Request,    /**< The user pressed USB DRIVE. */
	Release,    /**< The user pressed EJECT. */
	HostEject,  /**< The host ejected the medium itself. */
	CardGone,   /**< The card stopped answering while the host held it. */
	Done,       /**< The transition in progress finished. */
};

/**
 * @brief Why the card cannot be handed to a host right now.
 *
 * Ordered by how much the user needs to hear about it: a spinning motor
 * outranks a recording, which outranks an empty slot.
 */
enum class MscRefusal : uint8_t {
	None = 0,    /**< It can be handed over. */
	Armed,       /**< The ESC is armed. */
	Recording,   /**< A log is open. Stopping it is the user's call, not ours. */
	NoCard,      /**< Nothing to hand over. */
};

/**
 * @brief Decide whether the card may be handed to a host.
 *
 * Recording is a refusal rather than something this silently stops, and that is
 * a deliberate difference from how CFG treats arming. Force-disarming on the way
 * into a screen costs the user nothing they were keeping; ending a recording
 * costs them the run they were in the middle of measuring. So this says no and
 * names the reason, and the user stops the log themselves.
 *
 * Armed outranks it because a motor is spinning either way, and because the
 * handover pauses the logger for seconds — which is a thing to do while a
 * propeller is stationary.
 *
 * @param cardPresent A card is mounted and usable.
 * @param armed       The ESC is armed.
 * @param recording   A log file is currently open.
 * @return @ref MscRefusal::None if the handover may proceed, else the reason.
 */
static inline MscRefusal mscRefusal(bool cardPresent, bool armed, bool recording) {
	if (armed)        return MscRefusal::Armed;
	if (recording)    return MscRefusal::Recording;
	if (!cardPresent) return MscRefusal::NoCard;
	return MscRefusal::None;
}

/** @brief Caption for a refusal, for the screen to print verbatim. */
static inline const char *mscRefusalText(MscRefusal r) {
	switch (r) {
		case MscRefusal::Armed:     return "DISARM FIRST";
		case MscRefusal::Recording: return "STOP RECORDING FIRST";
		case MscRefusal::NoCard:    return "NO CARD TO SHARE";
		case MscRefusal::None:      break;
	}
	return "";
}

/**
 * @brief Advance the ownership state machine.
 *
 * Pure, and total: every state answers every event, because the events that
 * matter least are the ones that arrive at the worst moment. A host ejecting
 * during @ref MscState::Handover is a real ordering — the medium became present
 * and the host was quick — and the answer has to be something other than
 * undefined.
 *
 * @ref MscEvent::CardGone is the one that cannot be ignored. If the card stops
 * answering while a host holds it there is nothing to serve and nothing to
 * remount, so it drops straight to @ref MscState::Idle rather than through
 * @ref MscState::Reclaim, whose whole job is to remount a card that is there.
 *
 * @param s State now.
 * @param e What happened.
 * @return State after the event.
 */
static inline MscState mscNext(MscState s, MscEvent e) {
	// The card leaving beats everything, from any state. Nothing to serve,
	// nothing to reclaim.
	if (e == MscEvent::CardGone) return MscState::Idle;

	switch (s) {
		case MscState::Idle:
			return e == MscEvent::Request ? MscState::Handover : MscState::Idle;

		case MscState::Handover:
			// A release arriving mid-handover is the user changing their mind
			// before the host ever saw the medium. Remount and say no more
			// about it.
			if (e == MscEvent::Release || e == MscEvent::HostEject)
				return MscState::Reclaim;
			return e == MscEvent::Done ? MscState::Serving : MscState::Handover;

		case MscState::Serving:
			if (e == MscEvent::Release || e == MscEvent::HostEject)
				return MscState::Reclaim;
			return MscState::Serving;

		case MscState::Reclaim:
			// A request arriving mid-reclaim would mean unmounting something
			// that is still being mounted. Let it finish; the user can press
			// again, and will, because the button will be there.
			return e == MscEvent::Done ? MscState::Idle : MscState::Reclaim;
	}
	return MscState::Idle;
}

/** @brief True while the host owns the card and the firmware must not touch it. */
static inline bool mscHoldsCard(MscState s) {
	return s == MscState::Serving || s == MscState::Handover;
}

/** @brief Short label for the screen. @param s State. @return Text. */
static inline const char *mscStateText(MscState s) {
	switch (s) {
		case MscState::Idle:     return "OFF";
		case MscState::Handover: return "HANDING OVER";
		case MscState::Serving:  return "USB DRIVE";
		case MscState::Reclaim:  return "RECLAIMING";
	}
	return "";
}

/**
 * @defgroup msc_runtime Runtime side
 * @brief The half that talks to TinyUSB and the card. Not host-linkable.
 * @{
 */

/** @brief Wire the MSC interface up. Call once, after the card is mounted. */
void mscInit();

/**
 * @brief Service the state machine. Call from the UI loop on core0.
 *
 * Does the unmounting and remounting that @ref MscState::Handover and
 * @ref MscState::Reclaim stand for, one step per call, so a card that takes
 * its time cannot stall a frame.
 */
void mscTick();

/**
 * @brief Ask for the card to be handed to a host.
 * @return The refusal, or @ref MscRefusal::None if the handover started.
 */
MscRefusal mscRequest();

/** @brief Take the card back. Safe to call in any state. */
void mscRelease();

/** @brief Where the handover has got to. @return Current state. */
MscState mscGetState();

/**
 * @brief Blocks the host has read since the medium last appeared.
 *
 * Shown on the SD LOG screen while serving, because a progress figure is the
 * difference between "it is copying" and "it has hung", and USB full speed
 * moves about a megabyte a second — long enough on a full card to want to know.
 *
 * @return Count of 512-byte blocks.
 */
uint32_t mscBlocksRead();

/** @} */
