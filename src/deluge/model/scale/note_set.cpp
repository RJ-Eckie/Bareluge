#include "model/scale/note_set.h"
#include "model/scale/preset_scales.h"

NoteSet::NoteSet(std::initializer_list<uint8_t> notes) : bits{0} {
	for (uint8_t note : notes) {
		add(note);
	}
}

void NoteSet::addUntrusted(uint8_t note) {
	// Constrain added note to be strictly increasing, and < 12.
	//
	// Used when reading mode notes from song files, and other
	// untrusted sources.
	if (count() > 0 && note <= highest()) {
		note = highest() + 1;
	}
	if (note > 11) {
		note = 11;
	}
	add(note);
}

int8_t NoteSet::operator[](uint8_t index) const {
	uint16_t wip = bits;
	int8_t note = -1;
	int8_t n = -1;
	while (index > n++) {
		if (!wip) {
			// The desired degree does not exist.
			return -1;
		}
		// For each subsequent scale degree, find the number
		// of semitones, increment the note counter
		// and shift out the bit.
		uint8_t step = std::countr_zero(wip) + 1;
		wip >>= step;
		note += step;
	}
	// Return the note
	return note;
}

int8_t NoteSet::highestNotIn(NoteSet other) const {
	for (int8_t i = highest(); i >= 0; i--) {
		if (has(i) && !other.has(i)) {
			return i;
		}
	}
	return -1;
}

int8_t NoteSet::degreeOf(uint8_t note) const {
	if (has(note)) {
		// Mask everything before the note
		uint16_t mask = ~(0xffff << note);
		// How many notes under mask?
		uint16_t under = bits & mask;
		return std::popcount(under);
	}
	else {
		return -1;
	}
}

NoteSet NoteSet::operator|(const NoteSet& other) {
	NoteSet newSet = other;
	newSet.bits |= bits;
	return newSet;
}

int8_t NoteSet::majorness() const {
	int8_t majorness = 0;

	// The 3rd is the main indicator of majorness, to my ear
	if (has(4)) {
		majorness++;
	}
	if (has(3)) {
		majorness--;
	}

	// If it's still a tie, try the 2nd, 6th, and 7th to help us decide
	if (majorness == 0) {
		if (has(1)) {
			majorness--;
		}
		if (has(8)) {
			majorness--;
		}
		if (has(9)) {
			majorness++;
		}
	}
	return majorness;
}

void NoteSet::addMajorDependentModeNotes(uint8_t i, bool preferHigher, const NoteSet notesWithinOctavePresent) {
	// If lower one present...
	if (notesWithinOctavePresent.has(i)) {
		// If higher one present as well...
		if (notesWithinOctavePresent.has(i + 1)) {
			add(i);
			add(i + 1);
		}
		// Or if just the lower one
		else {
			add(i);
		}
	}
	// Or, if lower one absent...
	else {
		// We probably want the higher one
		if (notesWithinOctavePresent.has(i + 1) || preferHigher) {
			add(i + 1);
			// Or if neither present and we prefer the lower one, do that
		}
		else {
			add(i);
		}
	}
}

// Function to modulate NoteSet up offset semitones
NoteSet NoteSet::modulateByOffset(uint8_t offset) {
	NoteSet newSet = *this;
	// Normalize offset to be within the range [0, 11]
	offset = offset % 12;

	// If offset by 0, return the original chord
	if (offset == 0) {
		return newSet;
	}
	// Extract the last 12 bits
	uint16_t newBits = bits & 0x0FFF; // 0x0FFF = 0000 1111 1111 1111 in binary

	// Bits that will wrap around
	uint16_t wrapAroundBits = (newBits >> (12 - offset)) & ((1 << offset) - 1);

	// Shift new bits to the left by offset and wrap around
	newBits = ((newBits << offset) & 0x0FFF) | wrapAroundBits;

	newSet.bits = (bits & 0xF000) | newBits;
	return newSet;
}

NoteSet NoteSet::toImpliedScale() const {
	bool moreMajor = (majorness() >= 0);

	NoteSet scale;
	scale.add(0);

	// 2nd
	scale.addMajorDependentModeNotes(1, true, *this);

	// 3rd
	scale.addMajorDependentModeNotes(3, moreMajor, *this);

	// 4th, 5th
	if (has(5)) {
		scale.add(5);
		if (has(6)) {
			scale.add(6);
			if (has(7)) {
				scale.add(7);
			}
		}
		else {
			scale.add(7);
		}
	}
	else {
		if (has(6)) {
			if (has(7) || moreMajor) {
				scale.add(6);
				scale.add(7);
			}
			else {
				scale.add(5);
				scale.add(6);
			}
		}
		else {
			scale.add(5);
			scale.add(7);
		}
	}

	// 6th
	scale.addMajorDependentModeNotes(8, moreMajor, *this);

	// 7th
	scale.addMajorDependentModeNotes(10, moreMajor, *this);

	return scale;
}

int NoteSet::scaleSize() const {
	NoteSet tmp = *this;
	tmp.add(0);
	return tmp.count();
}

#ifdef IN_UNIT_TESTS
const TestString StringFrom(const NoteSet& set) {
	// We print out as chromatic notes across C, even though NoteSet does _not_ specify the root.
	// This is just easier to read and think about when debugging.
	std::string out;
	static const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
	out.append("NoteSet(");
	bool first = true;
	for (int i = 0; i < NoteSet::size; i++) {
		if (set.has(i)) {
			if (!first) {
				out.append(", ");
			}
			else {
				first = false;
			}
			out.append(names[i]);
		}
	}
	out.append(")");
	return TestString(out);
}

std::ostream& operator<<(std::ostream& output, const NoteSet& set) {
	output << StringFrom(set).asCharString();
	return output;
}
#endif
