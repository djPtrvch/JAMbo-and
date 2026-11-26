#pragma once

namespace jambo::MidiTarget {

// Коды сохраняются в Android MIDI-профилях. При расширении не менять старые коды.
constexpr int knobCount = 16;
constexpr int press = 16;
constexpr int play = 32, rec = 33, undo = 34, redo = 35, metronome = 36, shift = 37;
constexpr int fx = 38, mod = 39, eq = 40, patterns = 41;
constexpr int step = 48;
constexpr int bpm = 64, level = 65, chord = 66, octaveUp = 67, octaveDown = 68;
constexpr int lfo = 69, morph = 73, mix = 74, master = 75, armMaster = 76;
constexpr int gridUp = 77, gridDown = 78, soundLeft = 79, soundRight = 80, pageArrow = 81;
constexpr int track = 100, mute = 116, solo = 132, arm = 148, key = 164;
constexpr int chordType = 200, row = 240, page = 248, clip = 300;
constexpr int windowRows = 8, keyCount = 25;
constexpr int keysIdentity = 1'100'000;

constexpr bool in(int target, int start, int count) { return target >= start && target < start + count; }

}  // namespace jambo::MidiTarget
