#pragma once
#include "SemanticTypes.h"
#include "SonyError.h"
#include <algorithm>
#include <cctype>
#include <set>
#include <span>
#include <string_view>

namespace sony::protocol::multipoint {
inline std::string address(std::string_view input) {
    if (input.size() != 17) throw std::invalid_argument("Expected Bluetooth address AA:BB:CC:DD:EE:FF");
    std::string result(input);
    for (size_t i = 0; i < result.size(); ++i) {
        const auto c = static_cast<unsigned char>(result[i]);
        if (i % 3 == 2 ? c != ':' : !std::isxdigit(c)) throw std::invalid_argument("Invalid Bluetooth address");
        result[i] = static_cast<char>(std::toupper(c));
    }
    return result;
}
inline void invalid() { throw SonyException(SonyErrorCode::InvalidResponse, "Malformed multipoint response"); }

// Sound Connect 13.3.0: jo0.b, oo0.s/t and po0.b. Table 2 is DataMdrNo2.
inline std::set<uint8_t> functions(std::span<const uint8_t> bytes) {
    if (bytes.size() < 3 || bytes[0] != 0x07 || bytes[1] != 0 || bytes.size() != 3u + bytes[2] * 2u) invalid();
    std::set<uint8_t> result;
    for (size_t i = 3; i < bytes.size(); i += 2) result.insert(bytes[i]);
    return result;
}
inline MultipointState devices(std::span<const uint8_t> bytes, MultipointState state = {}) {
    if (bytes.size() < 4 || (bytes[0] != 0x37 && bytes[0] != 0x39) || (bytes[1] != 0 && bytes[1] != 2)) invalid();
    const bool withClass = bytes[1] == 2;
    size_t offset = 3;
    state.devices.clear();
    std::set<std::string> addresses;
    std::set<int> indices;
    for (size_t i = 0; i < bytes[2]; ++i) {
        const size_t fixed = withClass ? 22 : 19;
        if (bytes.size() - offset < fixed + 1) invalid();
        MultipointDevice device;
        try { device.address = address(std::string_view(reinterpret_cast<const char*>(bytes.data() + offset), 17)); }
        catch (const std::invalid_argument&) { invalid(); }
        if (!addresses.insert(device.address).second) invalid();
        offset += 17;
        device.connectionIndex = bytes[offset++];
        device.connected = device.connectionIndex > 0;
        if (device.connected && !indices.insert(device.connectionIndex).second) invalid();
        device.deviceClass = 0xFFFFFF;
        if (withClass) {
            device.deviceClass = (bytes[offset] << 16) | (bytes[offset + 1] << 8) | bytes[offset + 2];
            offset += 3;
        }
        const size_t length = bytes[offset++];
        if (length == 0 || length > 128 || bytes.size() - offset < length + 1) invalid();
        device.name.assign(reinterpret_cast<const char*>(bytes.data() + offset), length);
        offset += length;
        state.devices.push_back(std::move(device));
    }
    if (offset + 1 != bytes.size()) invalid();
    state.playbackIndex = bytes[offset];
    if (state.playbackIndex != 0 && indices.find(state.playbackIndex) == indices.end()) invalid();
    for (auto& device : state.devices) device.playing = device.connected && device.connectionIndex == state.playbackIndex;
    std::stable_sort(state.devices.begin(), state.devices.end(), [](const auto& a, const auto& b) {
        if (a.connected != b.connected) return a.connected;
        return a.connected && a.connectionIndex < b.connectionIndex;
    });
    state.supported = true;
    state.inquiredType = bytes[1];
    state.error.clear();
    return state;
}
} // namespace sony::protocol::multipoint
