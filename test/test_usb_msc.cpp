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

void runUsbMscTests() {
	testRefusals();
	testStateMachine();
}
