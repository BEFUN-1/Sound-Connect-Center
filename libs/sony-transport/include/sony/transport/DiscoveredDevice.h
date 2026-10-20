#pragma once
#include <optional>

#include "DeviceAddress.h"
#include <string>

namespace sony::transport {

struct DiscoveredDevice {
    std::string name;
    DeviceAddress address;
    std::optional<bool> paired;
    std::optional<bool> connected;

    bool operator==(const DiscoveredDevice& other) const {
        return name == other.name && address == other.address && paired == other.paired && connected == other.connected;
    }
};

} // namespace sony::transport
