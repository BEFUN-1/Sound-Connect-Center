#pragma once

#include "DeviceAddress.h"
#include "sony/protocol/DeviceProfile.h"
#include <cstddef>
#include <optional>
#include <span>

namespace sony::transport {

class ITransport {
public:
    virtual ~ITransport() = default;

    virtual void connect(const DeviceAddress& address) = 0;
    // Interrupt blocking send/receive work before disconnect() joins its reader.
    // Implementations must make this safe to call from another thread.
    virtual void cancelPendingIo() noexcept {}
    virtual void disconnect() noexcept = 0;
    [[nodiscard]] virtual bool isConnected() const noexcept = 0;
    virtual size_t send(std::span<const std::byte> data) = 0;
    virtual size_t receive(std::span<std::byte> buffer) = 0;

    // Platform Bluetooth transports learn the generation from the service UUID
    // during connect(). Generic and fake transports may not know it.
    [[nodiscard]] virtual std::optional<protocol::SonyProtocolVersion> protocolVersion() const noexcept {
        return std::nullopt;
    }
};

} // namespace sony::transport
