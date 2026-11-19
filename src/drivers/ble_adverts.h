#pragma once
// One BLE advertisement watcher (WinRT BluetoothLEAdvertisementWatcher, passive scan) shared by the
// drivers that read battery levels from broadcasts. It only listens; nothing is ever sent.
// Received messages are handed to the parsers below on a WinRT thread pool thread, so they must
// lock whatever they store.
#include <windows.h>

#include <utility>
#include <vector>

#include "fastpair_protocol.h"

// Starts the watcher, or restarts it after the radio was switched off. Returns true when it was just
// started, so the caller can give devices a moment (they broadcast about every 1-2 s) to be heard.
bool EnsureBleWatcher();

// Apple manufacturer data (company 0x004C), see airpods.cpp.
void AirPodsOnAdvert(UINT64 addr, int rssi, const BYTE* data, UINT32 len);
// Google Fast Pair service data (16-bit UUID 0xFE2C, bytes after the UUID), see fastpair.cpp.
// addrType: -1 unknown, 0 public, 1 random, 2 unspecified (WinRT BluetoothAddressType).
void FastPairOnAdvert(UINT64 addr, int addrType, const BYTE* data, UINT32 len);

// Battery split from the latest Fast Pair broadcast (percent, -1 unknown).
struct FastPairBattery : FastPair::Levels {
    ULONGLONG tick = 0;
    int addrType = -1;
};
// Latest broadcast of the device with this Bluetooth address, if heard in the last 5 minutes.
bool FastPairLookup(UINT64 addr, FastPairBattery& out);
// Latest broadcast of every address heard in the last `maxAgeMs`, for matching by level.
std::vector<std::pair<UINT64, FastPairBattery>> FastPairRecent(ULONGLONG maxAgeMs);

// `--dump` diagnostics (fastpair.cpp). Only while active is every 0xFE2C service data section kept,
// including ones the parser rejects; the normal path above is untouched.
// addrType: -1 unknown, 0 public, 1 random, 2 unspecified (WinRT BluetoothAddressType).
bool FastPairDiagActive();
void FastPairDiagRecord(UINT64 addr, int addrType, int rssi, const BYTE* data, UINT32 len);
// Watcher status: -1 never created, else WinRT BluetoothLEAdvertisementWatcherStatus
// (0 Created, 1 Started, 2 Stopping, 3 Stopped, 4 Aborted).
int BleWatcherStatus();
