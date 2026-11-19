#pragma once
#include <string>
#include <vector>

enum class DeviceKind { Other, Mouse, Headset, Keyboard, Gamepad, Phone, Microphone };

// One battery-powered device, as seen during a single poll.
struct DeviceStatus {
    std::wstring key;          // stable identity across polls (used for the tray icon id)
    std::wstring name;         // human readable name
    std::wstring via;          // "2.4 GHz receiver", "Bluetooth", "USB cable", ...
    DeviceKind kind = DeviceKind::Other;
    int  percent    = -1;      // 0..100, or -1 when unknown right now
    std::wstring detail;       // extra breakdown for the tooltip, e.g. "L 80%, R 75%, case 40%"
    int  left       = -1;      // earbuds: each bud's level when known (-1 = unknown); `percent` stays the lower one
    int  right      = -1;
    std::wstring levelText;    // set when the device only reports coarse levels ("Low", "Full"...)
    bool charging   = false;
    bool full       = false;   // on power and fully charged (charging may also be set)
    bool asleep     = false;   // receiver answered but the device is sleeping (keep last value)
    bool connected  = true;    // false: receiver present, device switched off (hidden)
    int  millivolts = 0;       // battery voltage when reported (0 = unknown)
    int  graceSec   = 0;       // keep showing the last value this long if the device stops answering
    unsigned short vid = 0;    // USB / Bluetooth HID vendor id when known (picks the brand color)
};

// "L63% R73%" for the taskbar strip when both buds are known, else "" (the single level is shown).
// The case level stays in the tooltip only.
inline std::wstring LeftRightText(int left, int right) {
    if (left < 0 || right < 0) return L"";
    return L"L" + std::to_wstring(left) + L"% R" + std::to_wstring(right) + L"%";
}

// Queries every supported device. Blocking (can take a second or two); call from a worker thread.
std::vector<DeviceStatus> PollAllDevices();

// Prints every HID collection (for adding support for new devices).
std::wstring DumpHidCollections();

// Listens ~10 s for Google Fast Pair broadcasts and lists them with the paired Bluetooth devices
// (Windows only, `--dump` only: the support report does not wait for it). See drivers/fastpair.cpp.
std::wstring DumpFastPairAdverts();
