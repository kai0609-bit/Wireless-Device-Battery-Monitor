#pragma once
// Google Fast Pair non-discoverable advertisement: service data of 16-bit UUID 0xFE2C, bytes after
// the UUID. Kept independent of Win32 so captured broadcasts can be checked without the earbuds.
//
//   [0] version and flags, 0bVVVVFFFF. Version 0 and 1 share the field layout below (a Sony
//       WF-1000XM5 sends 0x10); flag bits are ignored. Higher versions are not read.
//   then fields with a header byte 0bLLLLTTTT (L = length, T = type):
//   T 0 / 2  account key filter (shown / hidden UI)     T 1  salt
//   T 3 / 4  battery (shown / hidden UI): one byte per component, in order left, right, case;
//            bit 7 = charging, bits 0-6 = level 0-100, 0x7F = unknown
//   T 5      remaining battery time
// Discoverable-mode advertisements carry only the 3-byte model id and are ignored.
// Layout from the Fast Pair specification (developers.google.com/nearby/fast-pair/specifications/
// extensions/batterynotification).
#include <cstddef>
#include <cstdint>
#include <vector>

namespace FastPair {

struct Levels {
    int left = -1, right = -1, box = -1;  // percent, -1 unknown
    bool leftChg = false, rightChg = false, boxChg = false;
};

constexpr int kMaxVersion = 1;

inline int Component(std::uint8_t b, bool& charging) {
    int level = b & 0x7F;
    charging = level <= 100 && (b & 0x80) != 0;
    return level <= 100 ? level : -1;
}

// `why` (optional) gets the reason when it returns false; only `--dump` asks for it.
inline bool Parse(const std::uint8_t* d, std::size_t n, Levels& out, const wchar_t** why = nullptr) {
    auto fail = [&](const wchar_t* reason) {
        if (why) *why = reason;
        return false;
    };
    if (n < 2) return fail(n == 0 ? L"nothing after the UUID" : L"only 1 byte after the UUID");
    // A 3-byte one starting with 0x00 was always read as version 0 (and still is); any other 3-byte one
    // is a discoverable-mode model id.
    if (n == 3 && d[0] != 0x00) return fail(L"3 bytes, first not 0x00: discoverable-mode model id (no battery in it)");
    if ((d[0] >> 4) > kMaxVersion) return fail(L"unknown version (high nibble of the first byte above 1)");
    for (std::size_t i = 1; i < n;) {
        std::size_t len = d[i] >> 4, type = d[i] & 0x0F;
        if (i + 1 + len > n) return fail(L"a field's length runs past the end of the data");
        if ((type == 3 || type == 4) && len >= 1) {
            out.left = Component(d[i + 1], out.leftChg);
            if (len >= 2) out.right = Component(d[i + 2], out.rightChg);
            if (len >= 3) out.box = Component(d[i + 3], out.boxChg);
            return out.left >= 0 || out.right >= 0 || out.box >= 0 ||
                   fail(L"battery field present, but every level is unknown (0x7F) or over 100");
        }
        i += 1 + len;
    }
    return fail(L"no battery field (type 3 / 4)");
}

// Windows' single level for a headset lies within the broadcast's left..right span, widened by `tol`.
inline bool LevelFits(int windowsPct, const Levels& l, int tol = 3) {
    int lo = 101, hi = -1;
    for (int v : { l.left, l.right }) {
        if (v < 0) continue;
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    return windowsPct >= 0 && hi >= 0 && windowsPct >= lo - tol && windowsPct <= hi + tol;
}

// Pairs broadcasts that could not be matched by address with connected headsets by level.
// result[i] = index into `adverts` for headset i, or -1. A pair is made only when it is unique both
// ways: the headset fits exactly one broadcast and that broadcast fits no other headset.
inline std::vector<int> MatchByLevel(const std::vector<int>& headsetPct, const std::vector<Levels>& adverts, int tol = 3) {
    std::vector<int> result(headsetPct.size(), -1);
    std::vector<int> fitsPerAdvert(adverts.size(), 0);
    for (int pct : headsetPct)
        for (std::size_t j = 0; j < adverts.size(); j++)
            if (LevelFits(pct, adverts[j], tol)) fitsPerAdvert[j]++;
    for (std::size_t i = 0; i < headsetPct.size(); i++) {
        int only = -1, count = 0;
        for (std::size_t j = 0; j < adverts.size(); j++)
            if (LevelFits(headsetPct[i], adverts[j], tol)) { only = (int)j; count++; }
        if (count == 1 && fitsPerAdvert[only] == 1) result[i] = only;
    }
    return result;
}

}  // namespace FastPair
