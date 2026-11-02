#pragma once
#include "sony/transport/FakeTransport.h"
#include "sony/protocol/FrameCodec.h"
#include <atomic>
#include <map>
// Alternating sequence numbers and subtype-specific replies exercise real
// request matching rather than bypassing the session machinery.
class ReplyTransport : public sony::transport::FakeTransport {
public:
    std::vector<std::string> attempts;
    std::string failAddress;
    uint8_t ambientLevel = 0;
    bool acceptAutoPowerOff = true;
    bool acceptSettings = true;
    uint8_t autoPowerOffMode = 0x11;
    uint8_t autoPowerOffTimer = 0;
    std::vector<uint8_t> autoPowerOffModes{0x00, 0x01, 0x02, 0x03, 0x04, 0x10, 0x11};
    void connect(const sony::transport::DeviceAddress& address) override {
        attempts.push_back(address.str());
        if (address.str() == failAddress) throw sony::SonyException(sony::SonyErrorCode::TransportFailure, "Device is off");
        seq = 0; clearIncoming(); FakeTransport::connect(address);
    }
    size_t send(std::span<const std::byte> data) override {
        const auto count = FakeTransport::send(data);
        std::vector<uint8_t> bytes;
        for (auto b : data) bytes.push_back(static_cast<uint8_t>(b));
        auto frame = sony::protocol::FrameCodec::decode(bytes);
        using namespace sony::protocol;
        if (frame.type != DataType::DataMdr) return count;
        queueIncoming(FrameCodec::encode({DataType::Ack, static_cast<uint8_t>(1 - (frame.sequence & 1)), {}}));
        const auto& p = frame.payload;
        if (p.empty()) return count;
        std::vector<uint8_t> reply;
        if (acceptSettings && (p[0] == 0x68 || p[0] == 0x58 || p[0] == 0xe8 || p[0] == 0xf8)) {
            auto value = p;
            value[0] = static_cast<uint8_t>(p[0] - 1);
            if (p[0] == 0x58 && p.size() == 4 && p[3] == 0) {
                value = equalizerReply(p[1]);
                value[2] = p[2];
            }
            _settings[{value[0], p[1]}] = std::move(value);
        }
        if (p[0] == 0) reply = {1,0};
        if (p[0] == 0x22) {
            if (p[1] == 9) reply = {0x23,9,81,0,79,0};
            else reply = {0x23,p[1],85,0};
        }
        if (p[0] == 0x10) reply = {0x11,p[1],85,0};
        if (p[0] == 0x66) {
            reply = {0x67,p[1],1,1,0,0,ambientLevel};
            if (p[1] == 2) reply = {0x67,2,0x11,1,2,1,0,ambientLevel};
            if (p[1] == 0x19) { reply.push_back(1); reply.push_back(0); }
            if (const auto it = _settings.find({0x67, p[1]}); it != _settings.end()) reply = it->second;
        }
        if (p[0] == 0x56) reply = equalizerReply(p[1]);
        if (p[0] == 0xe6 || p[0] == 0xf6) {
            const auto opcode = static_cast<uint8_t>(p[0] + 1);
            reply = {opcode, p[1], static_cast<uint8_t>(p[0] == 0xf6)};
            if (const auto it = _settings.find({opcode, p[1]}); it != _settings.end()) reply = it->second;
        }
        if (p[0] == 4) reply = {5,2,5,'1','.','0','.','0'};
        if (p[0] == 0x18) reply = {0x19,0,0};
        if (p[0] == 0x20) {
            reply = {0x21, p[1], static_cast<uint8_t>(autoPowerOffModes.size())};
            reply.insert(reply.end(), autoPowerOffModes.begin(), autoPowerOffModes.end());
        }
        if (p[0] == 0x26) reply = {0x27, p[1], autoPowerOffMode, autoPowerOffTimer};
        if (p[0] == 0x28 && acceptAutoPowerOff) {
            autoPowerOffMode = p[2];
            autoPowerOffTimer = p[3];
        }
        if (!reply.empty()) notify(reply);
        return count;
    }
    void notify(std::vector<uint8_t> payload,
                sony::protocol::DataType type = sony::protocol::DataType::DataMdr) {
        queueIncoming(sony::protocol::FrameCodec::encode({type,
            static_cast<uint8_t>(seq++ & 1), std::move(payload)}));
    }
private:
    std::atomic<unsigned> seq{0};
    std::map<std::pair<uint8_t, uint8_t>, std::vector<uint8_t>> _settings;
    std::vector<uint8_t> equalizerReply(uint8_t type) {
        if (const auto it = _settings.find({0x57, type}); it != _settings.end()) return it->second;
        if (type == 4) {
            std::vector<uint8_t> reply{0x57,4,0,10};
            reply.insert(reply.end(), 10, 6);
            return reply;
        }
        return {0x57,type,0,6,10,11,12,13,14,15};
    }
};
