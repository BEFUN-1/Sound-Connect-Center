#pragma once

#include "ReplyTransport.h"

// Audio/OS connectivity stays up while control requests stop completing.
// Opening a fresh control connection restores the simulated device's replies.
class CallHandoffTransport final : public ReplyTransport {
public:
    enum class Failure { None, Silent, AckOnly, NotificationsOnly, AckAndNotifications };
    std::atomic<Failure> failure{Failure::None};
    std::atomic<unsigned> connections{0};
    std::atomic<unsigned> closedConnections{0};
    std::atomic<unsigned> reopenedWithoutClose{0};

    void connect(const sony::transport::DeviceAddress& address) override {
        if (isConnected()) ++reopenedWithoutClose;
        ReplyTransport::connect(address);
        failure.store(Failure::None);
        ++connections;
    }

    void disconnect() noexcept override {
        if (isConnected()) ++closedConnections;
        ReplyTransport::disconnect();
    }

    size_t send(std::span<const std::byte> data) override {
        using namespace sony::protocol;
        std::vector<uint8_t> bytes;
        for (const auto byte : data) bytes.push_back(static_cast<uint8_t>(byte));
        const auto frame = FrameCodec::decode(bytes);
        if (frame.type != DataType::DataMdr && frame.type != DataType::DataMdrNo2)
            return ReplyTransport::send(data);

        const auto mode = failure.load();
        if (mode != Failure::None) {
            const auto count = FakeTransport::send(data);
            if (mode == Failure::AckOnly || mode == Failure::AckAndNotifications)
                queueAck(frame);
            if (mode == Failure::NotificationsOnly || mode == Failure::AckAndNotifications)
                notify({0x25, 0, 85, 0});
            return count;
        }

        const auto& payload = frame.payload;
        if (frame.type == DataType::DataMdrNo2) {
            const auto count = FakeTransport::send(data);
            queueAck(frame);
            if (payload == std::vector<uint8_t>{6, 0}) {
                notify({7, 0, 1, 0x30, 0}, DataType::DataMdrNo2);
            } else if (payload == std::vector<uint8_t>{0x30, 0}) {
                notify({0x31, 0, 8, 2, 0}, DataType::DataMdrNo2);
            } else if (payload == std::vector<uint8_t>{0x36, 0}) {
                notify({0x37, 0, 0, 0}, DataType::DataMdrNo2);
            }
            return count;
        }
        return ReplyTransport::send(data);
    }

private:
    void queueAck(const sony::protocol::SonyFrame& request) {
        queueIncoming(sony::protocol::FrameCodec::encode({
            sony::protocol::DataType::Ack,
            static_cast<uint8_t>(1 - (request.sequence & 1)), {}}));
    }
};
