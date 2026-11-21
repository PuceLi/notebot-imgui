#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace notebot {

// UI-only mirror of the note-block instrument IDs used by the game mod.
enum class Instrument : std::uint8_t {
    Harp, BassDrum, Snare, Hat, Bass, Flute, Bell, Guitar,
    Chime, Xylophone, IronXylophone, CowBell, Didgeridoo, Bit,
    Banjo, Pling, Trumpet, TrumpetExposed, TrumpetWeathered, TrumpetOxidized
};

inline constexpr std::size_t kMappedInstrumentCount =
    static_cast<std::size_t>(Instrument::TrumpetOxidized) + 1;
using NoteMap = std::array<int, kMappedInstrumentCount>;

inline constexpr Instrument nbsInstrument(std::uint8_t instrument) {
    switch (instrument) {
    case 0: return Instrument::Harp;
    case 1: return Instrument::Bass;
    case 2: return Instrument::BassDrum;
    case 3: return Instrument::Snare;
    case 4: return Instrument::Hat;
    case 5: return Instrument::Guitar;
    case 6: return Instrument::Flute;
    case 7: return Instrument::Bell;
    case 8: return Instrument::Chime;
    case 9: return Instrument::Xylophone;
    case 10: return Instrument::IronXylophone;
    case 11: return Instrument::CowBell;
    case 12: return Instrument::Didgeridoo;
    case 13: return Instrument::Bit;
    case 14: return Instrument::Banjo;
    case 15: return Instrument::Pling;
    default: return static_cast<Instrument>(instrument);
    }
}

inline constexpr NoteMap makeDefaultNoteMap() {
    NoteMap mapping{};
    for (std::size_t index = 0; index < mapping.size(); ++index)
        mapping[index] = static_cast<int>(nbsInstrument(static_cast<std::uint8_t>(index))) + 1;
    return mapping;
}

} // namespace notebot
