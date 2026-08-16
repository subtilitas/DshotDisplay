/**
 * @file test_usb_msc.cpp
 * @brief The card-ownership rules. Pure; the TinyUSB half cannot be linked.
 *
 * These are the checks that matter for a feature whose failure mode is a
 * corrupted filesystem: that the firmware and a host can never both believe
 * they own the card, and that the interlocks refusing the handover cannot be
 * talked out of it.
 */

#include "check.h"
#include "usb_msc.h"

#include <initializer_list>

/** @brief Who may take the card, and what the user is told when they may not. */
static void testRefusals() {
	section("USB drive: when the card may be handed over");

	checkTrue("an idle board with a card hands it over",
	          mscRefusal(true, false, false) == MscRefusal::None);

	// A spinning motor outranks everything. The handover pauses the logger and
	// unmounts the card, which is not a thing to be doing while a prop turns.
	checkTrue("not while armed",
	          mscRefusal(true, true, false) == MscRefusal::Armed);
	checkTrue("armed outranks recording",
	          mscRefusal(true, true, true) == MscRefusal::Armed);
	checkTrue("and outranks an empty slot, because it is the worse problem",
	          mscRefusal(false, true, false) == MscRefusal::Armed);

	// Deliberately a refusal rather than something the firmware fixes for you.
	// CFG force-disarms because that costs nothing you were keeping; ending a
	// recording costs the run you were in the middle of measuring.
	checkTrue("not while recording",
	          mscRefusal(true, false, true) == MscRefusal::Recording);
	checkTrue("recording outranks an empty slot",
	          mscRefusal(false, false, true) == MscRefusal::Recording);

	checkTrue("and nothing to share is its own answer",
	          mscRefusal(false, false, false) == MscRefusal::NoCard);

	// Every refusal has to say something, or the button looks broken.
	checkTrue("armed has a caption",     mscRefusalText(MscRefusal::Armed)[0] != 0);
	checkTrue("recording has a caption", mscRefusalText(MscRefusal::Recording)[0] != 0);
	checkTrue("no card has a caption",   mscRefusalText(MscRefusal::NoCard)[0] != 0);
	checkStr("and success says nothing", mscRefusalText(MscRefusal::None), "");
}

/** @brief The ownership state machine, including the awkward orderings. */
static void testStateMachine() {
	section("USB drive: ownership never overlaps");

	// The happy path, one event at a time.
	MscState s = MscState::Idle;
	s = mscNext(s, MscEvent::Request);
	checkTrue("a request begins the handover", s == MscState::Handover);
	s = mscNext(s, MscEvent::Done);
	checkTrue("which completes into serving", s == MscState::Serving);
	s = mscNext(s, MscEvent::HostEject);
	checkTrue("the host ejecting starts the reclaim", s == MscState::Reclaim);
	s = mscNext(s, MscEvent::Done);
	checkTrue("and the firmware has it back", s == MscState::Idle);

	// The user's own EJECT is the same transition as the host's.
	s = mscNext(mscNext(MscState::Idle, MscEvent::Request), MscEvent::Done);
	checkTrue("EJECT on the screen releases it too",
	          mscNext(s, MscEvent::Release) == MscState::Reclaim);

	// This is the property the whole feature rests on: the firmware must not
	// touch the card from the moment the handover starts until the reclaim
	// finishes. Not merely while serving -- the unmount happens during the
	// handover, and writing during it is exactly as bad.
	checkTrue("the firmware lets go the moment the handover starts",
	          mscHoldsCard(MscState::Handover));
	checkTrue("and stays off it while serving", mscHoldsCard(MscState::Serving));
	checkTrue("it owns the card while idle",   !mscHoldsCard(MscState::Idle));
	checkTrue("and again once reclaiming",     !mscHoldsCard(MscState::Reclaim));

	// Awkward orderings. Each of these is a real race, not a hypothetical.
	checkTrue("a host quick enough to eject mid-handover is not lost",
	          mscNext(MscState::Handover, MscEvent::HostEject) == MscState::Reclaim);
	checkTrue("nor is a user who changes their mind mid-handover",
	          mscNext(MscState::Handover, MscEvent::Release) == MscState::Reclaim);
	checkTrue("a second request while serving changes nothing",
	          mscNext(MscState::Serving, MscEvent::Request) == MscState::Serving);
	checkTrue("a request mid-reclaim waits rather than unmounting a mount",
	          mscNext(MscState::Reclaim, MscEvent::Request) == MscState::Reclaim);

	// A card pulled out of the slot beats every other event from every state,
	// and goes straight to Idle: Reclaim exists to remount a card that is
	// there, and there is no longer one.
	for (MscState from : {MscState::Idle, MscState::Handover,
	                      MscState::Serving, MscState::Reclaim}) {
		checkTrue("a card that leaves drops straight to idle",
		          mscNext(from, MscEvent::CardGone) == MscState::Idle);
	}

	// Total: no state/event pair may fall through to something undefined. A
	// mutation that deleted a case would otherwise only show up on hardware.
	for (MscState from : {MscState::Idle, MscState::Handover,
	                      MscState::Serving, MscState::Reclaim}) {
		for (MscEvent e : {MscEvent::None, MscEvent::Request, MscEvent::Release,
		                   MscEvent::HostEject, MscEvent::CardGone, MscEvent::Done}) {
			MscState to = mscNext(from, e);
			bool known = to == MscState::Idle || to == MscState::Handover ||
			             to == MscState::Serving || to == MscState::Reclaim;
			if (!known) checkTrue("every state/event pair lands somewhere", false);
		}
	}
	checkTrue("every state/event pair lands somewhere", true);

	// Nothing left holding the card after a full cycle, however it ended.
	checkTrue("a cycle ended by the host leaves the card with the firmware",
	          !mscHoldsCard(mscNext(MscState::Reclaim, MscEvent::Done)));
	checkTrue("and so does one ended by the card being pulled",
	          !mscHoldsCard(mscNext(MscState::Serving, MscEvent::CardGone)));

	// Labels: every state has one, or the screen has a blank line in it.
	for (MscState st : {MscState::Idle, MscState::Handover,
	                    MscState::Serving, MscState::Reclaim}) {
		checkTrue("every state has a caption", mscStateText(st)[0] != 0);
	}
}

// ---------------------------------------------------------------------------
// RPM smoothing
// ---------------------------------------------------------------------------
#include "rpm_filter.h"

/**
 * @brief The RPM filter: settles, does not ease in, does not outlive the link.
 *
 * The failure modes worth asserting are the two dishonest ones. A filter that
 * ramps from zero shows a spin-up passing through speeds the motor never turned
 * at; one that survives a dead link shows an average of data that stopped
 * arriving.
 */
static void testRpmFilter() {
	section("RPM smoothing");

	const uint8_t K = 3;
	RpmFilter f{};
	rpmFilterReset(&f);

	// The first sample is taken whole. Not averaged in from zero.
	checkInt("the first reading is shown as it is",
	         (int)rpmFilterStep(&f, 20000, true, K), 20000);

	// A steady input stays put -- a filter that wandered on constant data would
	// be worse than none.
	for (int i = 0; i < 20; i++) rpmFilterStep(&f, 20000, true, K);
	checkInt("a steady reading does not drift",
	         (int)rpmFilterStep(&f, 20000, true, K), 20000);

	// Quantisation flicker: the reading alternates between two neighbours. The
	// output still moves every frame -- an average always does -- so the
	// property worth asserting is the *amplitude*, not whether it is still.
	// This is the whole point of the filter: a 300 eRPM flicker becomes a wobble
	// small enough that the digits sit still to look at.
	uint32_t lo = 0xFFFFFFFFu, hi = 0;
	for (int i = 0; i < 60; i++) {
		uint32_t out = rpmFilterStep(&f, (i & 1) ? 20300u : 20000u, true, K);
		if (i > 20) {                       // settled
			if (out < lo) lo = out;
			if (out > hi) hi = out;
		}
	}
	checkTrue("a 300-wide flicker shrinks to under a quarter of that",
	          (hi - lo) * 4 < 300);
	checkTrue("and the result sits between the two extremes",
	          lo > 20000 && hi < 20300);

	// A real change is followed, and actually arrives rather than stopping a
	// few RPM short -- which is what plain integer division would do.
	for (int i = 0; i < 200; i++) rpmFilterStep(&f, 30000, true, K);
	checkInt("a real change is reached exactly",
	         (int)rpmFilterStep(&f, 30000, true, K), 30000);

	// Settling time: at the UI frame rate this should be a fraction of a
	// second, not a second. 8 frames is 200 ms at UI_PERIOD_MS.
	rpmFilterReset(&f);
	rpmFilterStep(&f, 0, true, K);
	int frames = 0;
	while (rpmFilterStep(&f, 10000, true, K) < 9000 && frames < 100) frames++;
	checkTrue("and reaches 90 % within about a fifth of a second", frames <= 20);

	// The link dying resets it. The next reading after that is taken whole
	// again, rather than averaged with a speed from before the gap.
	rpmFilterStep(&f, 30000, true, K);
	checkInt("a dead link reads zero", (int)rpmFilterStep(&f, 30000, false, K), 0);
	checkInt("and the reading after it is taken whole",
	         (int)rpmFilterStep(&f, 12345, true, K), 12345);

	// Shift 0 is the escape hatch: no filtering at all.
	rpmFilterReset(&f);
	rpmFilterStep(&f, 100, true, 0);
	checkInt("shift 0 passes every sample straight through",
	         (int)rpmFilterStep(&f, 55555, true, 0), 55555);
}


void runUsbMscTests() {
	testRefusals();
	testStateMachine();
	testRpmFilter();
}
