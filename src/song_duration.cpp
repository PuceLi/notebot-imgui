#include "song_duration.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

namespace meteor {
namespace {

bool read8(std::istream& in, std::uint8_t& value) {
    const int byte = in.get();
    if (byte == std::char_traits<char>::eof()) return false;
    value = static_cast<std::uint8_t>(byte);
    return true;
}

bool read16(std::istream& in, std::uint16_t& value) {
    std::uint8_t lo, hi;
    if (!read8(in, lo) || !read8(in, hi)) return false;
    value = static_cast<std::uint16_t>(lo | (hi << 8));
    return true;
}

bool read32(std::istream& in, std::uint32_t& value) {
    std::uint16_t lo, hi;
    if (!read16(in, lo) || !read16(in, hi)) return false;
    value = static_cast<std::uint32_t>(lo) | (static_cast<std::uint32_t>(hi) << 16);
    return true;
}

bool skip(std::istream& in, std::streamoff bytes) {
    in.seekg(bytes, std::ios::cur);
    return static_cast<bool>(in);
}

bool skipString(std::istream& in) {
    std::uint32_t length;
    return read32(in, length) && length <= 1'048'576 && skip(in, length);
}

std::optional<int> textLastTick(std::istream& in) {
    int last = -1;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line.rfind("//", 0) == 0) continue;
        std::istringstream fields(line);
        int tick, note;
        char colon;
        if (fields >> tick >> colon >> note && colon == ':' && tick >= 0 && tick <= 100'000'000)
            last = std::max(last, tick);
    }
    if (last < 0) return std::nullopt;
    return last;
}

std::optional<int> nbsLastTick(std::istream& in) {
    std::uint16_t length, height, speed;
    std::uint8_t version = 0, ignored;
    if (!read16(in, length)) return std::nullopt;
    if (length == 0) {
        if (!read8(in, version) || !read8(in, ignored)) return std::nullopt;
        if (version >= 3 && !read16(in, length)) return std::nullopt;
    }
    if (!read16(in, height)) return std::nullopt;
    for (int i = 0; i < 4; ++i) if (!skipString(in)) return std::nullopt;
    if (!read16(in, speed) || speed == 0 || !skip(in, 3 + 5 * 4) || !skipString(in)) return std::nullopt;
    if (version >= 4 && !skip(in, 4)) return std::nullopt;

    double tick = -1.0;
    int last = -1;
    for (;;) {
        std::uint16_t jumpTicks;
        if (!read16(in, jumpTicks)) return std::nullopt;
        if (jumpTicks == 0) break;
        tick += jumpTicks * (2000.0 / speed);
        if (!std::isfinite(tick) || tick > 100'000'000) return std::nullopt;
        for (;;) {
            std::uint16_t jumpLayers;
            if (!read16(in, jumpLayers)) return std::nullopt;
            if (jumpLayers == 0) break;
            std::uint8_t instrument, key;
            if (!read8(in, instrument) || !read8(in, key) || (version >= 4 && !skip(in, 4))) return std::nullopt;
            if (instrument <= 15) last = std::max(last, static_cast<int>(std::round(tick)));
        }
    }
    if (last < 0) return std::nullopt;
    return last;
}

} // namespace

std::optional<int> songLastTick(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    if (path.extension() == ".txt") return textLastTick(in);
    if (path.extension() == ".nbs") return nbsLastTick(in);
    return std::nullopt;
}

} // namespace meteor
