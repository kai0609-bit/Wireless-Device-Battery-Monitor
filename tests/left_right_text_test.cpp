#include "../src/devices.h"

#include <cassert>
#include <cstdio>

int main() {
    assert(LeftRightText(63, 73) == L"L63% R73%");
    assert(LeftRightText(100, 100) == L"L100% R100%");
    assert(LeftRightText(0, 5) == L"L0% R5%");
    // One bud unknown (in the case, out of range): no pair, the single level is shown instead.
    assert(LeftRightText(-1, 73).empty());
    assert(LeftRightText(63, -1).empty());
    assert(LeftRightText(-1, -1).empty());
    // A device that never set them (defaults) gets no pair either.
    DeviceStatus s;
    assert(LeftRightText(s.left, s.right).empty());
    std::puts("Left / right text tests passed (both known, one or both unknown, defaults).");
    return 0;
}
