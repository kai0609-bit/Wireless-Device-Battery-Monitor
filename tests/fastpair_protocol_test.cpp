#include "../src/drivers/fastpair_protocol.h"

#include <cassert>
#include <cstdio>
#include <vector>

using Bytes = std::vector<std::uint8_t>;

bool Parse(const Bytes& b, FastPair::Levels& out) { return FastPair::Parse(b.data(), b.size(), out); }

FastPair::Levels Lr(int left, int right) {
    FastPair::Levels l;
    l.left = left;
    l.right = right;
    return l;
}

int main() {
    // Sony WF-1000XM5, captured with `--dump`: version byte 0x10, account key filter (0x50), salt (0x21),
    // hidden-UI battery (0x34) 42 4C 2F = 66 / 76 / 47, then a type 6 field.
    {
        const Bytes xm5 = { 0x10, 0x50, 0x06, 0xC0, 0x21, 0xA1, 0x7C, 0x21, 0xEE, 0x67,
                            0x34, 0x42, 0x4C, 0x2F, 0x46, 0x4E, 0xBC, 0xA3, 0x8B };
        FastPair::Levels l;
        assert(Parse(xm5, l));
        assert(l.left == 66 && l.right == 76 && l.box == 47);
        assert(!l.leftChg && !l.rightChg && !l.boxChg);
    }
    // Version 0 (Jabra layout) still reads, with charging bits.
    {
        FastPair::Levels l;
        assert(Parse({ 0x00, 0x33, 0xD0, 0x64, 0x7F }, l));
        assert(l.left == 80 && l.leftChg && l.right == 100 && !l.rightChg && l.box == -1);
    }
    // Flag bits in the low nibble are ignored.
    {
        FastPair::Levels l;
        assert(Parse({ 0x01, 0x13, 0x32 }, l) == false);  // 3 bytes, first not 0x00: model id
        assert(Parse({ 0x1F, 0x13, 0x32, 0x00 }, l) && l.left == 50);
    }
    // Discoverable-mode model ids (3 bytes) stay ignored; a 3-byte one starting with 0x00 reads as before.
    {
        FastPair::Levels l;
        assert(!Parse({ 0x10, 0x34, 0x42 }, l));
        assert(!Parse({ 0xF5, 0x24, 0x77 }, l));
        FastPair::Levels v0;
        assert(Parse({ 0x00, 0x13, 0x42 }, v0) && v0.left == 66);
    }
    // Unknown versions, truncated fields, no battery, all unknown.
    {
        FastPair::Levels l;
        assert(!Parse({ 0x20, 0x13, 0x42, 0x00 }, l));
        assert(!Parse({ 0x10, 0x34, 0x42, 0x4C }, l));
        assert(!Parse({ 0x10, 0x21, 0xEE, 0x67 }, l));
        assert(!Parse({ 0x10, 0x33, 0x7F, 0x7F, 0x7F }, l));
        assert(!Parse({ 0x10 }, l) && !Parse({}, l));
    }
    // Level fit: Windows' level within left..right, +-3.
    {
        FastPair::Levels l = Lr(66, 76);
        assert(FastPair::LevelFits(63, l) && FastPair::LevelFits(70, l) && FastPair::LevelFits(79, l));
        assert(!FastPair::LevelFits(62, l) && !FastPair::LevelFits(80, l) && !FastPair::LevelFits(-1, l));
        assert(FastPair::LevelFits(52, Lr(-1, 50)) && !FastPair::LevelFits(50, Lr(-1, -1)));
    }
    // Matching: unique both ways only.
    {
        using V = std::vector<int>;
        assert((FastPair::MatchByLevel({ 70 }, { Lr(66, 76) }) == V{ 0 }));
        assert((FastPair::MatchByLevel({ 90 }, { Lr(66, 76) }) == V{ -1 }));
        assert((FastPair::MatchByLevel({ 70 }, { Lr(66, 76), Lr(68, 72) }) == V{ -1 }));   // two broadcasts fit
        assert((FastPair::MatchByLevel({ 70, 72 }, { Lr(66, 76) }) == V{ -1, -1 }));      // two headsets fit
        assert((FastPair::MatchByLevel({ 70, 20 }, { Lr(10, 20), Lr(66, 76) }) == V{ 1, 0 }));
        assert((FastPair::MatchByLevel({ 70 }, {}) == V{ -1 }));
    }
    std::puts("Fast Pair protocol tests passed (WF-1000XM5 version 1, version 0, flags, model id, level matching).");
    return 0;
}
