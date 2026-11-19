// Google Fast Pair battery broadcasts: left, right and case levels of earbuds over Bluetooth.
//
// Fast Pair earbuds (Jabra Elite, Pixel Buds, many Sony / JBL / Bose models ...) put their battery in
// the non-discoverable Fast Pair advertisement, in clear, for Android's battery popup. Windows only
// reads one level (the standard Battery Service), so the Bluetooth fallback uses this for the split.
// The service data layout and its parser are in fastpair_protocol.h; verified on a Jabra Elite 8
// Active against the Sound+ app, and on a Sony WF-1000XM5 (version byte 0x10).
//
// Messages are matched to the paired device by Bluetooth address: the public one Windows paired with.
// Devices broadcasting from a random address (the WF-1000XM5) are paired by level instead, in
// PollBluetooth: see FastPair::MatchByLevel.
//
// `--dump` lists every 0xFE2C section heard for a few seconds, parsed or not, next to the paired
// devices' addresses, to see why a pair of earbuds shows no left / right split.
#include "drivers.h"
#include "ble_adverts.h"

#include <atomic>
#include <cstdio>
#include <map>

namespace {

SRWLOCK g_lock = SRWLOCK_INIT;
std::map<UINT64, FastPairBattery> g_battery;  // by Bluetooth address

// ---- `--dump` diagnostics -----------------------------------------------------------------------

struct DiagRecord {
    ULONGLONG tick;
    UINT64 addr;
    int addrType;
    int rssi;
    std::vector<BYTE> data;  // after the 2-byte UUID
};

std::atomic<bool> g_diagOn{ false };
SRWLOCK g_diagLock = SRWLOCK_INIT;
std::vector<DiagRecord> g_diag;

std::wstring Addr(UINT64 a) {
    wchar_t b[24];
    swprintf(b, 24, L"%02X:%02X:%02X:%02X:%02X:%02X", (unsigned)(a >> 40) & 0xFF, (unsigned)(a >> 32) & 0xFF,
             (unsigned)(a >> 24) & 0xFF, (unsigned)(a >> 16) & 0xFF, (unsigned)(a >> 8) & 0xFF, (unsigned)a & 0xFF);
    return b;
}

// Random addresses: the top two bits tell a fixed one from a rotating (resolvable private) one.
std::wstring AddrType(int t, UINT64 a) {
    switch (t) {
        case 0: return L"public";
        case 1: {
            unsigned top = (unsigned)(a >> 46) & 0x3;
            return top == 3 ? L"random (static)" : top == 1 ? L"random (resolvable private, rotates)"
                                                 : top == 0 ? L"random (non-resolvable private)" : L"random (reserved)";
        }
        case 2: return L"unspecified";
        default: return L"type unknown";
    }
}

std::wstring Hex(const std::vector<BYTE>& d) {
    std::wstring s;
    wchar_t b[4];
    for (size_t i = 0; i < d.size(); i++) {
        swprintf(b, 4, L"%02X", d[i]);
        if (i) s += L' ';
        s += b;
    }
    return s.empty() ? L"(empty)" : s;
}

std::wstring Result(const std::vector<BYTE>& d) {
    FastPair::Levels b;
    const wchar_t* why = L"";
    if (!FastPair::Parse(d.data(), d.size(), b, &why)) return std::wstring(L"parse failed: ") + why;
    auto part = [](const wchar_t* label, int pct, bool chg) {
        return std::wstring(label) + (pct < 0 ? L"?" : std::to_wstring(pct) + L"%") + (chg ? L" (charging)" : L"");
    };
    return part(L"left ", b.left, b.leftChg) + L", " + part(L"right ", b.right, b.rightChg) + L", " +
           part(L"case ", b.box, b.boxChg);
}

}  // namespace

void FastPairOnAdvert(UINT64 addr, int addrType, const BYTE* data, UINT32 len) {
    FastPairBattery b;
    if (!FastPair::Parse(data, len, b)) return;
    b.tick = GetTickCount64();
    b.addrType = addrType;
    AcquireSRWLockExclusive(&g_lock);
    g_battery[addr] = b;
    ReleaseSRWLockExclusive(&g_lock);
}

bool FastPairLookup(UINT64 addr, FastPairBattery& out) {
    ULONGLONG now = GetTickCount64();
    AcquireSRWLockShared(&g_lock);
    auto it = g_battery.find(addr);
    bool ok = it != g_battery.end() && now - it->second.tick < 5 * 60 * 1000;
    if (ok) out = it->second;
    ReleaseSRWLockShared(&g_lock);
    return ok;
}

std::vector<std::pair<UINT64, FastPairBattery>> FastPairRecent(ULONGLONG maxAgeMs) {
    std::vector<std::pair<UINT64, FastPairBattery>> out;
    ULONGLONG now = GetTickCount64();
    AcquireSRWLockShared(&g_lock);
    for (const auto& [addr, b] : g_battery)
        if (now - b.tick < maxAgeMs) out.push_back({ addr, b });
    ReleaseSRWLockShared(&g_lock);
    return out;
}

bool FastPairDiagActive() { return g_diagOn.load(std::memory_order_relaxed); }

void FastPairDiagRecord(UINT64 addr, int addrType, int rssi, const BYTE* data, UINT32 len) {
    DiagRecord r{ GetTickCount64(), addr, addrType, rssi, std::vector<BYTE>(data, data + len) };
    AcquireSRWLockExclusive(&g_diagLock);
    if (g_diagOn) g_diag.push_back(std::move(r));
    ReleaseSRWLockExclusive(&g_diagLock);
}

std::wstring DumpFastPairAdverts() {
    constexpr DWORD kListenMs = 10000;
    std::wstring out = L"\nGoogle Fast Pair broadcasts (service data UUID 0xFE2C) heard in 10 s:\n";
    AcquireSRWLockExclusive(&g_diagLock);
    g_diag.clear();
    g_diagOn = true;
    ReleaseSRWLockExclusive(&g_diagLock);
    ULONGLONG start = GetTickCount64();
    bool started = EnsureBleWatcher();
    Sleep(kListenMs);
    AcquireSRWLockExclusive(&g_diagLock);
    g_diagOn = false;
    std::vector<DiagRecord> recs = std::move(g_diag);
    g_diag.clear();
    ReleaseSRWLockExclusive(&g_diagLock);

    static const wchar_t* kStatus[] = { L"created", L"started", L"stopping", L"stopped", L"aborted (Bluetooth off?)" };
    int st = BleWatcherStatus();
    out += std::wstring(L"  BLE watcher: ") + (started ? L"started" : L"could not be started") + L", status after listening: " +
           (st < 0 ? L"none (WinRT watcher unavailable)" : st <= 4 ? kStatus[st] : std::to_wstring(st).c_str()) + L"\n";
    if (recs.empty()) out += L"  (none heard)\n";

    std::map<UINT64, std::pair<int, std::wstring>> perAddr;  // address -> count, latest result
    for (const auto& r : recs) {
        wchar_t head[64];
        swprintf(head, 64, L"  +%5.2fs  ", (double)(r.tick - start) / 1000.0);
        std::wstring res = Result(r.data);
        out += head + Addr(r.addr) + L"  " + AddrType(r.addrType, r.addr) + L"  RSSI " + std::to_wstring(r.rssi) +
               L" dBm  data[" + std::to_wstring(r.data.size()) + L"]: " + Hex(r.data) + L"  ->  " + res + L"\n";
        auto& p = perAddr[r.addr];
        p.first++;
        p.second = res;
    }
    if (!perAddr.empty()) {
        out += L"\n  Per address (count, latest result):\n";
        for (const auto& [a, p] : perAddr) out += L"    " + Addr(a) + L"  x" + std::to_wstring(p.first) + L"  " + p.second + L"\n";
    }

    out += L"\nPaired Bluetooth devices (name  address  transport  connected  0xFE2C broadcasts heard from that address):\n";
    auto paired = PairedBluetoothDevices();
    if (paired.empty()) out += L"  (none)\n";
    for (const auto& d : paired) {
        auto it = perAddr.find(d.addr);
        out += L"  " + (d.name.empty() ? std::wstring(L"(no name)") : d.name) + L"  " + Addr(d.addr) + L"  " +
               (d.le ? L"LE" : L"Classic") + L"  " +
               (d.connected < 0 ? L"connected ?" : d.connected ? L"connected" : L"not connected") + L"  " +
               std::to_wstring(it == perAddr.end() ? 0 : it->second.first) + L"\n";
    }
    return out;
}
