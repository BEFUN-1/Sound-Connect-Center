#pragma once

#include "sony/transport/IDeviceDiscovery.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace sony::transport {

/// Gives the first three octets (the OUI) of a Bluetooth address as a 24-bit value.
/// Accepts ':' or '-' separators and hex digits in upper or lower case, so that
/// "AC:80:0A:12:34:56" (Linux), "ac:80:0a:12:34:56" (Windows), and
/// "ac-80-0a-12-34-56" (macOS) give the same value. Gives std::nullopt for other text.
std::optional<std::uint32_t> addressOui(std::string_view address);

/// True when the IEEE registry assigns the address prefix to a Sony audio or
/// electronics company. Sony Interactive Entertainment (PlayStation) is excluded.
bool hasSonyOui(std::string_view address);

/// True when the name contains a Sony headphone model prefix such as "WH-".
bool hasSonyName(std::string_view name);

/// True for a valid address whose name identifies a Sony headphone model. OUIs are not
/// sufficient because Sony uses them across televisions, cameras, speakers,
/// and other Bluetooth products.
bool isSonyCandidate(const DiscoveredDevice& device);

/// Wraps a platform discovery and keeps only the devices that isSonyCandidate()
/// accepts. This identifies candidates only; DeviceService still requires an
/// explicitly selected address before opening a control connection.
class SonyDeviceDiscovery : public IDeviceDiscovery {
public:
    explicit SonyDeviceDiscovery(std::unique_ptr<IDeviceDiscovery> inner);

    std::vector<DiscoveredDevice> discover() override;
    std::vector<std::string> localAddresses() override;

private:
    std::unique_ptr<IDeviceDiscovery> _inner;
};

} // namespace sony::transport
