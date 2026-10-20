#include <catch2/catch_test_macros.hpp>
#include "../../apps/sonyctl/DesktopSession.h"
#include "sony/core/DeviceService.h"
#include "sony/core/JsonProtocol.h"
#include "../support/ReplyTransport.h"
#include <algorithm>
#include <atomic>
#include <thread>

using namespace sony;
using namespace sony::core;
using namespace sony::protocol;
using namespace sony::transport;
namespace {
const std::string headset = "11:22:33:44:55:66";
const std::string peer = "AA:BB:CC:DD:EE:FF";
class SessionTransport : public ReplyTransport {
public:
    uint8_t sequence{0};
    bool disconnectOnNoiseRead{false};
    bool disconnectOnNoiseWrite{false};
    size_t send(std::span<const std::byte> data) override {
        std::vector<uint8_t> bytes;
        for (auto b : data) bytes.push_back(static_cast<uint8_t>(b));
        const auto frame = FrameCodec::decode(bytes);
        if (disconnectOnNoiseWrite && frame.type == DataType::DataMdr && !frame.payload.empty() && frame.payload[0] == 0x68) {
            disconnectOnNoiseWrite = false;
            const auto count = FakeTransport::send(data);
            simulateDisconnect();
            return count;
        }
        if (disconnectOnNoiseRead && frame.type == DataType::DataMdr && !frame.payload.empty() && frame.payload[0] == 0x66) {
            disconnectOnNoiseRead = false;
            const auto count = FakeTransport::send(data);
            simulateDisconnect();
            return count;
        }
        const auto count = ReplyTransport::send(data);
        if (frame.type != DataType::DataMdrNo2) return count;
        const auto& p = frame.payload;
        std::vector<uint8_t> reply;
        if (p[0] == 6) reply = {7,0,2,0x32,0,0x31,0};
        else if (p[0] == 0x30) reply = {0x31,2,8,2,0};
        else if (p[0] == 0x36 && p[1] == 1) reply = {0x37,1,1};
        else if (p[0] == 0x36) {
            reply = {0x37,2,1};
            reply.insert(reply.end(), peer.begin(), peer.end());
            reply.insert(reply.end(), {1,0,1,0x0c,2,'P','C',0});
        } else if (p[0] == 0x38) reply = {0x39,1,p[2],0};
        else if (p[0] == 0x3c) {
            reply = {0x3d,p[1]};
            if (p[1] == 1) reply.push_back(0);
            else reply.insert(reply.end(), {p[2],static_cast<uint8_t>(p[2] << 4)});
            reply.insert(reply.end(), peer.begin(), peer.end());
        }
        queueIncoming(FrameCodec::encode({DataType::DataMdrNo2,static_cast<uint8_t>(sequence++ & 1),reply}));
        return count;
    }
};
struct Fixture {
    std::shared_ptr<SessionTransport> transport = std::make_shared<SessionTransport>();
    std::shared_ptr<FakeDeviceDiscovery> discovery = std::make_shared<FakeDeviceDiscovery>();
    DeviceService service{transport, discovery, [] { return DeviceService::Clock::now(); }, InitialRefresh::None};
    DesktopSession session{service};
    Fixture() {
        discovery->addDevice({"WH-1000XM5",DeviceAddress(headset),true,true});
        discovery->addDevice({"WH-1000XM3",DeviceAddress(peer),true,false});
    }
    IpcResponse run(const std::string& command, const std::string& target = headset) { return session.execute(command,target); }
};
}
TEST_CASE("Desktop session reuses one connection and battery refresh reads no settings", "[core][desktop]") {
    Fixture f;
    for (int i = 0; i < 20; ++i) {
        const auto response = f.run("refresh battery");
        REQUIRE(response.success);
        const auto snapshot = JsonProtocol::Json::parse(response.data);
        CHECK(snapshot["battery"]["main"] == 85);
        CHECK(snapshot["features"]["noiseControl"]["availability"] == "unknown");
    }
    REQUIRE(f.transport->attempts.size() == 1);
    for (const auto& raw : f.transport->sentFrames()) {
        const auto frame = FrameCodec::decode(raw);
        if (frame.type == DataType::DataMdr && !frame.payload.empty())
            CHECK((frame.payload[0] == 0 || frame.payload[0] == 0x22));
    }
    f.run("snapshot");
    CHECK(f.transport->attempts.size() == 1);
}
TEST_CASE("Desktop session recovers from disconnect and unavailable headset without restarting", "[core][desktop]") {
    Fixture f;
    REQUIRE(f.run("refresh battery").success);
    f.transport->simulateDisconnect();
    f.transport->failAddress = headset;
    CHECK_FALSE(f.run("refresh battery").success);
    f.transport->failAddress.clear();
    REQUIRE(f.run("refresh battery").success);
    CHECK(f.transport->attempts.size() == 3);
    CHECK_FALSE(f.run("refresh battery", "00:00:00:00:00:00").success);
    REQUIRE(f.run("refresh battery").success);
}
TEST_CASE("Desktop session preserves all noise EQ DSEE multipoint and power commands", "[core][desktop]") {
    Fixture f;
    for (const auto& command : {"anc on", "ambient 12", "anc off", "eq custom 3 1 2 3 4 5", "dsee on", "dsee off",
             "multipoint source AA:BB:CC:DD:EE:FF", "multipoint default AA:BB:CC:DD:EE:FF", "multipoint keep off",
             "multipoint connect AA:BB:CC:DD:EE:FF", "multipoint disconnect AA:BB:CC:DD:EE:FF",
             "multipoint unpair AA:BB:CC:DD:EE:FF"}) {
        INFO(command);
        REQUIRE(f.run(command).success);
    }
    CHECK(f.transport->attempts.size() == 1);
    REQUIRE(f.run("poweroff").success);
    CHECK_FALSE(f.service.isConnected());
}
TEST_CASE("Desktop session switches protocol generation and resets old cached state", "[core][desktop][v1]") {
    Fixture f;
    REQUIRE(f.run("refresh battery").success);
    REQUIRE(f.run("anc on",peer).success);
    REQUIRE(f.service.activeDevice()->protocolVersion() == SonyProtocolVersion::V1);
    CHECK_FALSE(f.service.snapshot()->battery.main.has_value());
    REQUIRE(f.run("refresh battery").success);
    CHECK(f.service.activeDevice()->protocolVersion() == SonyProtocolVersion::V2);
    CHECK(f.transport->attempts.size() == 3);
}
TEST_CASE("Desktop notifications signal cached changes without service-lock reentry", "[core][desktop][events]") {
    Fixture f;
    std::atomic<int> notifications{0};
    DesktopSession session(f.service, [&](const auto& address, const auto&) { if (address == headset) ++notifications; });
    REQUIRE(session.execute("refresh battery",headset).success);
    const auto previous = notifications.load();
    f.transport->notify({0x25,0,73,0});
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (notifications.load() == previous && std::chrono::steady_clock::now() < until)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    REQUIRE(notifications.load() > previous);
    CHECK(JsonProtocol::Json::parse(session.execute("snapshot",headset).data)["battery"]["main"] == 73);
}

TEST_CASE("Desktop session tick reconnects a lost headset and publishes connection state", "[core][desktop][recovery]") {
    Fixture f;
    std::atomic<int> connectedEvents{0};
    DesktopSession session(f.service, [&](const auto& address, const auto& json) {
        const auto state = JsonProtocol::Json::parse(json);
        if (address == headset && state.value("connected", false)) ++connectedEvents;
    });
    REQUIRE(session.execute("refresh battery", headset).success);
    const auto eventsBeforeReconnect = connectedEvents.load();
    const auto attempts = f.transport->attempts.size();
    f.transport->simulateDisconnect();
    session.tick();
    REQUIRE(f.service.isConnected());
    CHECK(f.transport->attempts.size() == attempts + 1);
    CHECK(connectedEvents.load() > eventsBeforeReconnect);
}

TEST_CASE("Desktop session reconnects after noise-state preflight and still runs the requested action", "[core][desktop][recovery]") {
    Fixture f;
    REQUIRE(f.run("refresh battery").success);
    f.transport->disconnectOnNoiseRead = true;

    const auto response = f.run("anc on");

    REQUIRE(response.success);
    CHECK(f.service.isConnected());
    CHECK(f.transport->attempts.size() == 2);
    const auto frames = f.transport->sentFrames();
    CHECK(std::any_of(frames.begin(), frames.end(), [](const auto& raw) {
        const auto frame = FrameCodec::decode(raw);
        return frame.type == DataType::DataMdr && !frame.payload.empty() && frame.payload[0] == 0x68;
    }));
}

TEST_CASE("Desktop session retries an idempotent state command after losing its ACK", "[core][desktop][recovery]") {
    Fixture f;
    REQUIRE(f.run("refresh battery").success);
    f.transport->disconnectOnNoiseWrite = true;

    const auto response = f.run("anc on");

    REQUIRE(response.success);
    CHECK(f.service.isConnected());
    CHECK(f.transport->attempts.size() == 2);
    const auto frames = f.transport->sentFrames();
    const auto noiseWrites = std::count_if(frames.begin(), frames.end(), [](const auto& raw) {
        const auto frame = FrameCodec::decode(raw);
        return frame.type == DataType::DataMdr && !frame.payload.empty() && frame.payload[0] == 0x68;
    });
    CHECK(noiseWrites == 2);
}
