#pragma once

#include <string>
#include <string_view>

namespace sony::transport {

class DeviceAddress {
public:
    DeviceAddress() = default;
    /* implicit */ DeviceAddress(std::string address) : _address(std::move(address)) {}
    /* implicit */ DeviceAddress(std::string_view address) : _address(address) {}
    /* implicit */ DeviceAddress(const char* address) : _address(address ? address : "") {}

    [[nodiscard]] const std::string& str() const noexcept { return _address; }
    [[nodiscard]] bool empty() const noexcept { return _address.empty(); }

    bool operator==(const DeviceAddress& other) const noexcept { return _address == other._address; }
    bool operator<(const DeviceAddress& other) const noexcept { return _address < other._address; }

private:
    std::string _address;
};

} // namespace sony::transport
