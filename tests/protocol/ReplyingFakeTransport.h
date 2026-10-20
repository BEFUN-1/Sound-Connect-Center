#pragma once

#include "sony/protocol/FrameCodec.h"
#include "sony/protocol/SonyFrame.h"
#include "sony/transport/FakeTransport.h"

#include <cstddef>
#include <deque>
#include <initializer_list>
#include <mutex>
#include <span>
#include <vector>

namespace sony::test {

// A FakeTransport that holds each scripted reply until the host sends the
// request that the reply answers.
//
// The session discards an ACK that arrives while no send is outstanding. A
// reply that sits in the incoming queue before the send is therefore a race:
// the reader thread can consume the ACK first, and the send then times out.
// This transport releases one reply group per outgoing request frame, which is
// the order that real hardware produces.
class ReplyingFakeTransport : public sony::transport::FakeTransport {
public:
    // Queue the frames that answer one request. The frames become readable
    // when the host sends its next request frame.
    void queueReply(std::initializer_list<sony::protocol::SonyFrame> frames) {
        std::vector<uint8_t> encoded;
        for (const auto& frame : frames) {
            auto bytes = sony::protocol::FrameCodec::encode(frame);
            encoded.insert(encoded.end(), bytes.begin(), bytes.end());
        }
        std::lock_guard lock(_replyMutex);
        _replies.push_back(std::move(encoded));
    }

    size_t send(std::span<const std::byte> data) override {
        const size_t written = sony::transport::FakeTransport::send(data);
        if (!_isRequest(data)) {
            return written;
        }
        std::vector<uint8_t> reply;
        {
            std::lock_guard lock(_replyMutex);
            if (_replies.empty()) {
                return written;
            }
            reply = std::move(_replies.front());
            _replies.pop_front();
        }
        queueIncoming(reply);
        return written;
    }

private:
    // True for a host request frame. The ACK that the session returns for an
    // incoming frame must not consume a scripted reply.
    static bool _isRequest(std::span<const std::byte> data) {
        std::vector<uint8_t> bytes;
        bytes.reserve(data.size());
        for (const auto b : data) {
            bytes.push_back(static_cast<uint8_t>(b));
        }
        try {
            const auto type = sony::protocol::FrameCodec::decode(bytes).type;
            return type == sony::protocol::DataType::DataMdr || type == sony::protocol::DataType::DataMdrNo2;
        } catch (...) {
            return false;
        }
    }

    std::mutex _replyMutex;
    std::deque<std::vector<uint8_t>> _replies;
};

} // namespace sony::test
