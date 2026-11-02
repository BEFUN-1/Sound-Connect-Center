#include <catch2/catch_test_macros.hpp>
#include "sony/core/DeviceService.h"
#include "sony/core/JsonProtocol.h"
#include "sony/core/IpcServer.h"
#include "sony/core/IpcClient.h"
#include "../support/PrivateSocket.h"
#include "../support/ReplyTransport.h"
#include "../support/CallHandoffTransport.h"
#include <future>
#include <fstream>
#ifndef _WIN32
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <cstring>
#endif
using namespace sony;
using namespace sony::core;
using namespace sony::transport;
using Json = JsonProtocol::Json;

TEST_CASE("Automatic connection requires a selected address", "[core][selection][security]") {
    auto transport = std::make_shared<ReplyTransport>();
    auto discovery = std::make_shared<FakeDeviceDiscovery>();
    discovery->setDevices({
        {"WH-1000XM6", DeviceAddress("11:22:33:44:55:66"), true, true},
        {"WH-1000XM5", DeviceAddress("22:22:33:44:55:66"), true, true}});
    DeviceService service(transport, discovery, [] { return DeviceService::Clock::now(); },
                          InitialRefresh::None, false);
    service.startAutoConnect("");
    service.tick();
    CHECK_FALSE(service.isConnected());
    CHECK(service.selectedAddress().empty());
    CHECK(transport->attempts.empty());
    CHECK_FALSE(JsonProtocol::execute(
        {{"version", 1}, {"id", 1}, {"method", "powerOff"}}, service)["ok"].get<bool>());
}

TEST_CASE("Changing the automatic target invalidates the previous control session",
          "[core][selection][security]") {
    auto transport = std::make_shared<ReplyTransport>();
    auto discovery = std::make_shared<FakeDeviceDiscovery>();
    discovery->addDevice({"WH-1000XM6", DeviceAddress("11:22:33:44:55:66"), true, true});
    DeviceService service(transport, discovery, [] { return DeviceService::Clock::now(); },
                          InitialRefresh::None, false);
    service.connect(DeviceAddress("11:22:33:44:55:66"), "WH-1000XM6");
    service.startAutoConnect("22:22:33:44:55:66");
    const auto sent = transport->sentFrames().size();
    CHECK_FALSE(JsonProtocol::execute(
        {{"version", 1}, {"id", 1}, {"method", "powerOff"}}, service)["ok"].get<bool>());
    CHECK_FALSE(IpcProtocol::execute(IpcProtocol::parseCommand("anc on"), service).success);
    CHECK(transport->sentFrames().size() == sent);
    service.tick();
    CHECK_FALSE(service.isConnected());
    CHECK(transport->attempts.size() == 1);
}

TEST_CASE("Queued writes must match the selected headset", "[core][selection][security]") {
    auto transport = std::make_shared<ReplyTransport>();
    DeviceService service(transport, {}, [] { return DeviceService::Clock::now(); },
                          InitialRefresh::None, false);
    service.connect(DeviceAddress("11:22:33:44:55:66"), "WH-1000XM6");
    const auto sent = transport->sentFrames().size();
    const auto response = JsonProtocol::execute(
        {{"version", 1}, {"id", 1}, {"method", "powerOff"},
         {"params", {{"targetAddress", "22:22:33:44:55:66"}}}}, service);
    CHECK_FALSE(response["ok"].get<bool>());
    CHECK(transport->sentFrames().size() == sent);
    CHECK(service.isConnected());
    REQUIRE_NOTHROW(service.validateSelectedTarget("11-22-33-44-55-66"));
    CHECK_THROWS(service.startAutoConnect("bad address"));
    CHECK(service.isConnected());
}

TEST_CASE("Powering the XM6 back on restores JSON and CLI control without an app restart",
          "[core][poweroff][regression]") {
    for (const bool json : {false, true}) {
        CAPTURE(json);
        auto now = DeviceService::Clock::time_point{};
        auto transport = std::make_shared<ReplyTransport>();
        auto discovery = std::make_shared<FakeDeviceDiscovery>();
        const auto setSystemConnection = [&](bool connected) {
            discovery->setDevices({
                {"WH-1000XM6", DeviceAddress("11:22:33:44:55:66"), true, connected}});
        };
        setSystemConnection(true);
        DeviceService service(transport, discovery, [&] { return now; }, InitialRefresh::None, false);
        service.startAutoConnect("11:22:33:44:55:66");
        service.tick();
        REQUIRE(service.isConnected());
        if (json) {
            REQUIRE(JsonProtocol::execute(
                {{"version", 1}, {"id", 1}, {"method", "powerOff"}}, service)["ok"] == true);
        } else {
            REQUIRE(IpcProtocol::execute(IpcProtocol::parseCommand("poweroff"), service).success);
        }
        setSystemConnection(false);
        now += std::chrono::seconds(30);
        service.tick();
        REQUIRE_FALSE(service.isConnected());
        REQUIRE(transport->attempts.size() == 1);

        setSystemConnection(true);
        now += std::chrono::seconds(1);
        service.tick();
        CHECK(service.isConnected());
        CHECK(JsonProtocol::snapshot(service)["connected"] == true);
        CHECK(transport->attempts.size() == 2);
        if (json) {
            CHECK(JsonProtocol::execute(
                {{"version", 1}, {"id", 2}, {"method", "anc"},
                 {"params", {{"enabled", true}}}}, service)["ok"] == true);
        } else {
            CHECK(IpcProtocol::execute(IpcProtocol::parseCommand("anc on"), service).success);
        }
    }
}

TEST_CASE("Intentional power off waits for the same headset instead of reconnecting immediately",
          "[core][poweroff][regression]") {
    for (const bool json : {false, true}) {
        CAPTURE(json);
        auto now = DeviceService::Clock::time_point{};
        auto transport = std::make_shared<ReplyTransport>();
        auto discovery = std::make_shared<FakeDeviceDiscovery>();
        discovery->addDevice({"WH-1000XM6", DeviceAddress("11:22:33:44:55:66"), true, true});
        DeviceService service(transport, discovery, [&] { return now; }, InitialRefresh::None, false);
        service.startAutoConnect("11:22:33:44:55:66");
        service.tick();
        REQUIRE(service.isConnected());
        REQUIRE(transport->attempts.size() == 1);

        if (json) {
            const auto response = JsonProtocol::execute(
                {{"version", 1}, {"id", 1}, {"method", "powerOff"}}, service);
            REQUIRE(response["ok"] == true);
            CHECK(response["data"]["connected"] == false);
            CHECK(response["data"]["connectionState"] == "waiting_for_power_on");
        } else {
            REQUIRE(IpcProtocol::execute(IpcProtocol::parseCommand("poweroff"), service).success);
        }
        CHECK_FALSE(service.isConnected());
        CHECK(service.connectionState() == "waiting_for_power_on");
        now += std::chrono::seconds(1);
        service.tick();
        CHECK(transport->attempts.size() == 1);
        discovery->setDevices({
            {"WH-1000XM6", DeviceAddress("11:22:33:44:55:66"), true, false},
            {"WH-1000XM5", DeviceAddress("22:22:33:44:55:66"), true, true}});
        for (const auto delay : {30, 3600}) {
            now += std::chrono::seconds(delay);
            service.tick();
            CHECK_FALSE(service.isConnected());
            CHECK(transport->attempts.size() == 1);
        }

        service.startAutoConnect("11:22:33:44:55:66");
        service.tick();
        CHECK(service.isConnected());
        CHECK(transport->attempts.size() == 2);
    }
}

TEST_CASE("Power off protects shutdown and handles a missed system disconnect event",
          "[core][poweroff][regression]") {
    auto now = DeviceService::Clock::time_point{};
    auto transport = std::make_shared<ReplyTransport>();
    auto discovery = std::make_shared<FakeDeviceDiscovery>();
    discovery->addDevice({"WH-1000XM6", DeviceAddress("11:22:33:44:55:66"), true, true});
    DeviceService service(transport, discovery, [&] { return now; }, InitialRefresh::None, false);
    service.startAutoConnect("11:22:33:44:55:66");
    service.tick();
    REQUIRE(IpcProtocol::execute(IpcProtocol::parseCommand("poweroff"), service).success);
    now += std::chrono::milliseconds(4999);
    service.tick();
    CHECK_FALSE(service.isConnected());
    CHECK(transport->attempts.size() == 1);
    now += std::chrono::milliseconds(1);
    service.tick();
    CHECK(service.isConnected());
    CHECK(transport->attempts.size() == 2);
}

TEST_CASE("Manual disconnect cancels waiting for a powered off headset",
          "[core][poweroff][regression]") {
    auto now = DeviceService::Clock::time_point{};
    auto transport = std::make_shared<ReplyTransport>();
    auto discovery = std::make_shared<FakeDeviceDiscovery>();
    discovery->addDevice({"WH-1000XM6", DeviceAddress("11:22:33:44:55:66"), true, true});
    DeviceService service(transport, discovery, [&] { return now; }, InitialRefresh::None, false);
    service.startAutoConnect("11:22:33:44:55:66");
    service.tick();
    REQUIRE(IpcProtocol::execute(IpcProtocol::parseCommand("poweroff"), service).success);
    service.disconnect();
    now += std::chrono::seconds(30);
    service.tick();
    CHECK_FALSE(service.isConnected());
    CHECK(service.connectionState() == "manually_disconnected");
    CHECK(transport->attempts.size() == 1);
}

TEST_CASE("Power on recovery retries when the system link precedes the control service",
          "[core][poweroff][regression]") {
    auto now = DeviceService::Clock::time_point{};
    auto transport = std::make_shared<ReplyTransport>();
    auto discovery = std::make_shared<FakeDeviceDiscovery>();
    discovery->addDevice({"WH-1000XM6", DeviceAddress("11:22:33:44:55:66"), true, true});
    DeviceService service(transport, discovery, [&] { return now; }, InitialRefresh::None, false);
    service.startAutoConnect("11:22:33:44:55:66");
    service.tick();
    REQUIRE(IpcProtocol::execute(IpcProtocol::parseCommand("poweroff"), service).success);
    transport->failAddress = "11:22:33:44:55:66";
    now += std::chrono::seconds(5);
    service.tick();
    CHECK_FALSE(service.isConnected());
    CHECK(transport->attempts.size() == 2);
    transport->failAddress.clear();
    now += std::chrono::seconds(1);
    service.tick();
    CHECK(service.isConnected());
    CHECK(transport->attempts.size() == 3);
}

TEST_CASE("Failed power off preserves automatic connection recovery",
          "[core][poweroff][regression]") {
    class RejectPowerOffTransport : public ReplyTransport {
        size_t send(std::span<const std::byte> data) override {
            std::vector<uint8_t> bytes;
            for (const auto byte : data) bytes.push_back(static_cast<uint8_t>(byte));
            const auto frame = protocol::FrameCodec::decode(bytes);
            if (frame.type == protocol::DataType::DataMdr &&
                frame.payload == std::vector<uint8_t>{0x24, 3, 1}) {
                throw SonyException(SonyErrorCode::TransportFailure, "Power off request rejected");
            }
            return ReplyTransport::send(data);
        }
    };
    for (const bool json : {false, true}) {
        CAPTURE(json);
        auto transport = std::make_shared<RejectPowerOffTransport>();
        auto discovery = std::make_shared<FakeDeviceDiscovery>();
        discovery->addDevice({"WH-1000XM6", DeviceAddress("11:22:33:44:55:66"), true, true});
        DeviceService service(transport, discovery, [] { return DeviceService::Clock::now(); },
                              InitialRefresh::None, false);
        service.connect(DeviceAddress("11:22:33:44:55:66"), "WH-1000XM6");
        if (json) {
            CHECK(JsonProtocol::execute(
                {{"version", 1}, {"id", 1}, {"method", "powerOff"}}, service)["ok"] == false);
        } else {
            CHECK_FALSE(IpcProtocol::execute(IpcProtocol::parseCommand("poweroff"), service).success);
        }
        service.tick();
        CHECK(service.isConnected());
        CHECK(service.connectionState() == "connected");
        CHECK(transport->attempts.size() == 2);
    }
}

TEST_CASE("Auto power off does not claim success when XM6 ignores the setting",
          "[core][autopoweroff][regression]") {
    auto transport = std::make_shared<ReplyTransport>();
    transport->acceptAutoPowerOff = false;
    SonyDevice device(transport);
    device.connect(DeviceAddress("11:22:33:44:55:66"), "WH-1000XM6", InitialRefresh::None);
    CHECK_THROWS_AS(device.setAutoPowerOff(5), SonyException);
    CHECK(device.snapshot()->autoPowerOff != 5);
}

TEST_CASE("XM6 auto-power capability limits choices and preserves the remembered timer",
          "[core][autopoweroff][regression]") {
    auto transport = std::make_shared<ReplyTransport>();
    transport->autoPowerOffModes = {0x10, 0x11};
    transport->autoPowerOffMode = 0x11;
    transport->autoPowerOffTimer = 3;
    SonyDevice device(transport);
    device.connect(DeviceAddress("11:22:33:44:55:66"), "WH-1000XM6", InitialRefresh::None);

    device.setAutoPowerOff(5);
    CHECK(device.snapshot()->autoPowerOff == 5);
    CHECK(device.snapshot()->autoPowerOffOptions == std::vector<int>{0, 5});
    CHECK(transport->autoPowerOffTimer == 3);
    CHECK_THROWS_AS(device.setAutoPowerOff(2), SonyException);
    CHECK(transport->autoPowerOffMode == 0x10);
    for (const auto& bytes : transport->sentFrames()) {
        const auto frame = protocol::FrameCodec::decode(bytes);
        if (frame.type != protocol::DataType::DataMdr || frame.payload.empty()) continue;
        CHECK(frame.payload[0] != 0xf8); // Removing-to-pause is a separate system command.
        if (frame.payload[0] == 0x28)
            CHECK(frame.payload == std::vector<uint8_t>{0x28, 5, 0x10, 3});
    }
}

TEST_CASE("XM6 advertised timers are confirmed by a fresh device readback",
          "[core][autopoweroff][regression]") {
    auto transport = std::make_shared<ReplyTransport>();
    SonyDevice device(transport);
    device.connect(DeviceAddress("11:22:33:44:55:66"), "WH-1000XM6", InitialRefresh::None);
    for (const auto index : {1, 2, 3, 4, 6, 5, 0}) {
        device.setAutoPowerOff(index);
        CHECK(device.snapshot()->autoPowerOff == index);
        CHECK(device.snapshot()->features.at("autoPowerOff").availability == "valid");
    }
}

TEST_CASE("Lifecycle retries with backoff and rediscovers without a restart", "[core][recovery]") {
    auto now = DeviceService::Clock::time_point{};
    auto transport = std::make_shared<ReplyTransport>();
    auto discovery = std::make_shared<FakeDeviceDiscovery>();
    DeviceService service(transport, discovery, [&] { return now; });
    service.startAutoConnect("11:22:33:44:55:66"); service.tick();
    CHECK(service.connectionState() == "retrying");
    discovery->addDevice({"WH-1000XM3", DeviceAddress("11:22:33:44:55:66"), true, true});
    service.tick(); CHECK(transport->attempts.empty());
    now += std::chrono::seconds(1); service.tick();
    REQUIRE(service.isConnected());
    CHECK(service.activeDevice()->protocolVersion() == SonyProtocolVersion::V1);
    for (const auto* feature : {"battery", "noiseControl"})
        CHECK(service.snapshot()->features.at(feature).availability == "valid");
    CHECK(service.snapshot()->features.at("equalizer").availability == "unsupported");
    CHECK(transport->attempts.size() == 1);
    transport->simulateDisconnect(); service.tick();
    REQUIRE(service.isConnected());
    CHECK(transport->attempts.size() == 2);
    service.disconnect(); now += std::chrono::hours(1); service.tick();
    CHECK_FALSE(service.isConnected()); CHECK(transport->attempts.size() == 2);
    CHECK(service.connectionState() == "manually_disconnected");
    for (const auto& raw : transport->sentFrames()) {
        const auto frame = protocol::FrameCodec::decode(raw);
        if (!frame.payload.empty()) CHECK(frame.payload[0] != 0x22);
    }
}

TEST_CASE("Windows startup waits for the selected system link without persisting a disconnect",
          "[core][recovery][system-observation]") {
    auto now = DeviceService::Clock::time_point{};
    auto transport = std::make_shared<ReplyTransport>();
    auto discovery = std::make_shared<FakeDeviceDiscovery>();
    SECTION("Disconnected at startup") {
        discovery->setDevices({{"WH-1000XM6", DeviceAddress("11:22:33:44:55:66"), true, false}});
    }
    SECTION("Unknown at startup") {
        discovery->setDevices({{"WH-1000XM6", DeviceAddress("11:22:33:44:55:66"), true, std::nullopt}});
    }
    SECTION("Missing at startup") {}
    DeviceService service(transport, discovery, [&] { return now; },
                          InitialRefresh::None, false,
                          DeviceService::RecoveryPolicy::RequireSystemConnection);
    service.restoreConnectionPreferences({"11:22:33:44:55:66", {}, {}});
    for (int tick = 0; tick < 10; ++tick) {
        now += std::chrono::seconds(1);
        service.tick();
    }
    CHECK(transport->attempts.empty());
    CHECK(service.connectionPreferences().pausedAddresses.empty());
    CHECK(service.connectionIntent() == ConnectionIntent::Active);
    discovery->setDevices({{"WH-1000XM6", DeviceAddress("11:22:33:44:55:66"), true, true}});
    now += std::chrono::seconds(1);
    service.tick();
    CHECK(service.isConnected());
    CHECK(transport->attempts.size() == 1);
}

TEST_CASE("Uncertain Windows discovery preserves a healthy control session",
          "[core][recovery][system-observation]") {
    class UncertainDiscovery : public FakeDeviceDiscovery {
    public:
        bool fail{false};
        std::vector<transport::DiscoveredDevice> discover() override {
            if (fail) throw std::runtime_error("Temporary Bluetooth enumeration failure");
            return FakeDeviceDiscovery::discover();
        }
    };
    auto now = DeviceService::Clock::time_point{};
    auto transport = std::make_shared<ReplyTransport>();
    auto discovery = std::make_shared<UncertainDiscovery>();
    discovery->setDevices({{"WH-1000XM6", DeviceAddress("11:22:33:44:55:66"), true, true}});
    DeviceService service(transport, discovery, [&] { return now; },
                          InitialRefresh::None, false,
                          DeviceService::RecoveryPolicy::RequireSystemConnection);
    service.connect(DeviceAddress("11:22:33:44:55:66"), "WH-1000XM6");
    SECTION("Unknown metadata") {
        discovery->setDevices({{"WH-1000XM6", DeviceAddress("11:22:33:44:55:66"), true, std::nullopt}});
    }
    SECTION("Missing from an incomplete enumeration") { discovery->clear(); }
    SECTION("Enumeration throws") { discovery->fail = true; }
    now += std::chrono::seconds(10);
    service.tick();
    CHECK(service.isConnected());
    CHECK(service.connectionPreferences().pausedAddresses.empty());
    CHECK(service.connectionIntent() == ConnectionIntent::Active);
    CHECK(transport->attempts.size() == 1);
    discovery->fail = false;
    discovery->setDevices({{"WH-1000XM6", DeviceAddress("11:22:33:44:55:66"), true, true}});
    now += std::chrono::seconds(1);
    service.tick();
    CHECK(service.isConnected());
    CHECK(transport->attempts.size() == 1);
}

TEST_CASE("Uncertain discovery cannot authorize recovery of a lost control session",
          "[core][recovery][system-observation][no-steal]") {
    auto now = DeviceService::Clock::time_point{};
    auto transport = std::make_shared<ReplyTransport>();
    auto discovery = std::make_shared<FakeDeviceDiscovery>();
    discovery->setDevices({{"WH-1000XM6", DeviceAddress("11:22:33:44:55:66"), true, true}});
    DeviceService service(transport, discovery, [&] { return now; }, InitialRefresh::None, false,
                          DeviceService::RecoveryPolicy::RequireSystemConnection);
    service.connect(DeviceAddress("11:22:33:44:55:66"), "WH-1000XM6");
    discovery->setDevices({{"WH-1000XM6", DeviceAddress("11:22:33:44:55:66"), true, std::nullopt}});
    transport->simulateDisconnect();
    service.tick();
    CHECK(service.connectionIntent() == ConnectionIntent::PausedAfterSystemDisconnect);
    CHECK(transport->attempts.size() == 1);
    now += std::chrono::hours(1);
    service.tick();
    CHECK_FALSE(service.isConnected());
    CHECK(transport->attempts.size() == 1);
    discovery->setDevices({{"WH-1000XM6", DeviceAddress("11:22:33:44:55:66"), true, true}});
    now += std::chrono::seconds(1);
    service.tick();
    CHECK(service.isConnected());
    CHECK(transport->attempts.size() == 2);
    CHECK(service.connectionPreferences().pausedAddresses.empty());
}

TEST_CASE("System disconnect pauses maintenance instead of reclaiming the headset",
          "[core][recovery][no-steal][system-resume]") {
    auto now = DeviceService::Clock::time_point{};
    auto transport = std::make_shared<ReplyTransport>();
    auto discovery = std::make_shared<FakeDeviceDiscovery>();
    discovery->setDevices({
        {"WH-1000XM6", DeviceAddress("11:22:33:44:55:66"), true, true}});
    DeviceService service(transport, discovery, [&] { return now; },
                          InitialRefresh::None, false,
                          DeviceService::RecoveryPolicy::RequireSystemConnection);
    service.connect(DeviceAddress("11:22:33:44:55:66"), "WH-1000XM6");
    REQUIRE(service.isConnected());
    const auto attempts = transport->attempts.size();
    discovery->setDevices({
        {"WH-1000XM6", DeviceAddress("11:22:33:44:55:66"), true, false}});
    transport->simulateDisconnect();
    for (int tick = 0; tick < 120; ++tick) {
        now += std::chrono::seconds(1);
        service.tick();
    }
    CHECK(transport->attempts.size() == attempts);
    CHECK_FALSE(service.isConnected());
    CHECK(service.connectionState() == "paused_after_system_disconnect");
    const auto preferences = service.connectionPreferences();
    CHECK(preferences.pausedAddresses == std::vector<std::string>{"11:22:33:44:55:66"});
    service.close();
    auto restartedTransport = std::make_shared<ReplyTransport>();
    DeviceService restarted(restartedTransport, discovery, [&] { return now; },
                            InitialRefresh::None, false,
                            DeviceService::RecoveryPolicy::RequireSystemConnection);
    restarted.restoreConnectionPreferences(preferences);
    for (int tick = 0; tick < 120; ++tick) {
        now += std::chrono::seconds(1);
        restarted.tick();
    }
    CHECK(restartedTransport->attempts.empty());
    CHECK_FALSE(restarted.isConnected());
    discovery->setDevices({
        {"WH-1000XM6", DeviceAddress("11:22:33:44:55:66"), true, true}});
    now += std::chrono::seconds(1);
    restarted.tick();
    CHECK(restarted.isConnected());
    CHECK(restartedTransport->attempts.size() == 1);
    CHECK(restarted.connectionPreferences().pausedAddresses.empty());
    CHECK(restarted.systemConnected() == true);
}

TEST_CASE("Manual pause waits for a new Windows connection before resuming",
          "[core][recovery][no-steal][system-resume]") {
    auto now = DeviceService::Clock::time_point{};
    auto transport = std::make_shared<ReplyTransport>();
    auto discovery = std::make_shared<FakeDeviceDiscovery>();
    const auto systemLink = [&](std::optional<bool> connected) {
        discovery->setDevices({
            {"WH-1000XM6", DeviceAddress("11:22:33:44:55:66"), true, connected},
            {"WH-1000XM5", DeviceAddress("22:22:33:44:55:66"), true, true}});
    };
    systemLink(true);
    DeviceService service(transport, discovery, [&] { return now; }, InitialRefresh::None, false,
                          DeviceService::RecoveryPolicy::RequireSystemConnection);
    service.connect(DeviceAddress("11:22:33:44:55:66"), "WH-1000XM6");
    service.disconnect();
    for (int tick = 0; tick < 10; ++tick) {
        now += std::chrono::seconds(1);
        service.tick();
    }
    REQUIRE_FALSE(service.isConnected());
    REQUIRE(transport->attempts.size() == 1);
    auto restartedTransport = std::make_shared<ReplyTransport>();
    DeviceService restarted(restartedTransport, discovery, [&] { return now; }, InitialRefresh::None, false,
                            DeviceService::RecoveryPolicy::RequireSystemConnection);
    restarted.restoreConnectionPreferences(service.connectionPreferences());
    restarted.tick();
    REQUIRE_FALSE(restarted.isConnected());
    REQUIRE(restartedTransport->attempts.empty());
    systemLink(std::nullopt);
    restarted.tick();
    systemLink(true);
    restarted.tick();
    CHECK(restartedTransport->attempts.empty());
    systemLink(false);
    restarted.tick();
    const auto preferences = restarted.connectionPreferences();
    restarted.close();
    DeviceService afterSystemDisconnect(restartedTransport, discovery, [&] { return now; },
        InitialRefresh::None, false, DeviceService::RecoveryPolicy::RequireSystemConnection);
    afterSystemDisconnect.restoreConnectionPreferences(preferences);
    for (int tick = 0; tick < 10; ++tick) {
        now += std::chrono::seconds(1);
        afterSystemDisconnect.tick();
    }
    REQUIRE(restartedTransport->attempts.empty());
    systemLink(true);
    now += std::chrono::seconds(1);
    afterSystemDisconnect.tick();
    CHECK(afterSystemDisconnect.isConnected());
    CHECK(restartedTransport->attempts == std::vector<std::string>{"11:22:33:44:55:66"});
    CHECK(afterSystemDisconnect.connectionPreferences().pausedAddresses.empty());
}

TEST_CASE("Windows recovery requires a positive system link including healthy control sessions",
          "[core][recovery][no-steal]") {
    auto now = DeviceService::Clock::time_point{};
    auto transport = std::make_shared<ReplyTransport>();
    auto discovery = std::make_shared<FakeDeviceDiscovery>();
    discovery->setDevices({{"WH-1000XM6", DeviceAddress("11:22:33:44:55:66"), true, true}});
    DeviceService service(transport, discovery, [&] { return now; }, InitialRefresh::None, false,
                          DeviceService::RecoveryPolicy::RequireSystemConnection);
    SECTION("System disconnect takes priority over a control socket still marked connected") {
        service.connect(DeviceAddress("11:22:33:44:55:66"), "WH-1000XM6");
        discovery->setDevices({{"WH-1000XM6", DeviceAddress("11:22:33:44:55:66"), true, false}});
        service.tick();
        CHECK_FALSE(service.isConnected());
        CHECK(transport->attempts.size() == 1);
        CHECK(service.connectionIntent() == ConnectionIntent::PausedAfterSystemDisconnect);
    }
    SECTION("Unknown link state never opens RFCOMM") {
        discovery->setDevices({{"WH-1000XM6", DeviceAddress("11:22:33:44:55:66"), true, std::nullopt}});
        service.startAutoConnect("11:22:33:44:55:66");
        service.tick();
        CHECK(transport->attempts.empty());
        CHECK(service.connectionIntent() == ConnectionIntent::Active);
    }
    SECTION("Missing target never falls through to another headset") {
        discovery->setDevices({{"WH-1000XM5", DeviceAddress("22:22:33:44:55:66"), true, true}});
        service.startAutoConnect("11:22:33:44:55:66");
        service.tick();
        CHECK(transport->attempts.empty());
        CHECK(service.connectionIntent() == ConnectionIntent::Active);
    }
}

TEST_CASE("Explicit selection clears only that target's persistent pause",
          "[core][recovery][no-steal]") {
    auto transport = std::make_shared<ReplyTransport>();
    auto discovery = std::make_shared<FakeDeviceDiscovery>();
    discovery->setDevices({
        {"WH-1000XM6", DeviceAddress("11:22:33:44:55:66"), true, true},
        {"WH-1000XM5", DeviceAddress("22:22:33:44:55:66"), true, true}});
    DeviceService service(transport, discovery, [] { return DeviceService::Clock::now(); },
                          InitialRefresh::None, false,
                          DeviceService::RecoveryPolicy::RequireSystemConnection);
    service.restoreConnectionPreferences({"11:22:33:44:55:66",
        {"11-22-33-44-55-66", "22:22:33:44:55:66"}, {}});
    service.tick();
    CHECK(transport->attempts.empty());
    service.connect(DeviceAddress("11:22:33:44:55:66"), "WH-1000XM6");
    CHECK(service.connectionIntent() == ConnectionIntent::Active);
    CHECK(service.connectionPreferences().pausedAddresses ==
          std::vector<std::string>{"22:22:33:44:55:66"});
    service.close();
    CHECK(service.connectionPreferences().pausedAddresses ==
          std::vector<std::string>{"22:22:33:44:55:66"});
}

TEST_CASE("Persisted software shutdown observes Windows without opening a disconnected or unknown link",
          "[core][recovery][poweroff][no-steal]") {
    auto now = DeviceService::Clock::time_point{};
    auto transport = std::make_shared<ReplyTransport>();
    auto discovery = std::make_shared<FakeDeviceDiscovery>();
    discovery->setDevices({{"WH-1000XM6", DeviceAddress("11:22:33:44:55:66"), true, true}});
    DeviceService service(transport, discovery, [&] { return now; }, InitialRefresh::None, false,
                          DeviceService::RecoveryPolicy::RequireSystemConnection);
    service.connect(DeviceAddress("11:22:33:44:55:66"), "WH-1000XM6");
    service.powerOff();
    auto preferences = service.connectionPreferences();
    CHECK(preferences.pausedAddresses.empty());
    CHECK(preferences.waitingForPowerOnAddress == "11:22:33:44:55:66");
    auto restartedTransport = std::make_shared<ReplyTransport>();
    DeviceService restarted(restartedTransport, discovery, [&] { return now; },
                            InitialRefresh::None, false,
                            DeviceService::RecoveryPolicy::RequireSystemConnection);
    restarted.restoreConnectionPreferences(preferences);
    now += std::chrono::seconds(4);
    restarted.tick();
    CHECK(restartedTransport->attempts.empty());
    for (const auto system : {std::optional<bool>{false}, std::optional<bool>{}}) {
        discovery->setDevices({{"WH-1000XM6", DeviceAddress("11:22:33:44:55:66"), true, system}});
        now += std::chrono::seconds(10);
        restarted.tick();
        CHECK(restartedTransport->attempts.empty());
        CHECK(restarted.connectionIntent() == ConnectionIntent::WaitingForPowerOn);
    }
    discovery->setDevices({{"WH-1000XM6", DeviceAddress("11:22:33:44:55:66"), true, true}});
    now += std::chrono::seconds(1);
    restarted.tick();
    CHECK(restarted.isConnected());
    CHECK(restarted.connectionPreferences().waitingForPowerOnAddress.empty());
}

TEST_CASE("Battery deadline follows valid notifications and precedes settings",
          "[core][maintenance][battery]") {
    auto now = DeviceService::Clock::time_point{};
    auto transport = std::make_shared<ReplyTransport>();
    DeviceService service(transport, {}, [&] { return now; });
    service.connect(DeviceAddress("11:22:33:44:55:66"), "WH-1000XM5");
    auto batteryRequests = [&] {
        size_t count = 0;
        for (const auto& bytes : transport->sentFrames()) {
            const auto frame = protocol::FrameCodec::decode(bytes);
            if (frame.type == protocol::DataType::DataMdr &&
                frame.payload == std::vector<uint8_t>{0x22, 0}) ++count;
        }
        return count;
    };
    const auto initial = batteryRequests();
    now += std::chrono::seconds(29);
    service.serviceBattery();
    CHECK(batteryRequests() == initial);
    now += std::chrono::seconds(1);
    const auto beforeTick = transport->sentFrames().size();
    service.tick();
    CHECK(batteryRequests() == initial + 1);
    CHECK(protocol::FrameCodec::decode(transport->sentFrames()[beforeTick]).payload ==
          std::vector<uint8_t>{0x22, 0});
    const auto revision = service.snapshot()->revision;
    transport->notify({0x25,0,84,0});
    for (int wait = 0; wait < 100 && service.snapshot()->revision == revision; ++wait)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    REQUIRE(service.snapshot()->battery.main == 84);
    now += std::chrono::seconds(20);
    service.serviceBattery();
    now += std::chrono::seconds(9);
    service.serviceBattery();
    CHECK(batteryRequests() == initial + 1);
    now += std::chrono::seconds(1);
    service.serviceBattery();
    CHECK(batteryRequests() == initial + 2);
}

TEST_CASE("Ignored acknowledged settings never replace confirmed state",
          "[core][state][confirmation]") {
    auto transport = std::make_shared<ReplyTransport>();
    SonyDevice device(transport);
    device.connect(DeviceAddress("11:22:33:44:55:66"), "WH-1000XM5");
    const auto before = device.snapshot();
    transport->acceptSettings = false;
    CHECK_THROWS_AS(device.setAnc(false), SonyException);
    const auto after = device.snapshot();
    CHECK(after->noiseControl.mode == before->noiseControl.mode);
    CHECK(after->features.at("noiseControl").availability == "stale");
    CHECK(after->features.at("noiseControl").lastSuccessMs ==
          before->features.at("noiseControl").lastSuccessMs);
    CHECK(after->revision > before->revision);
    CHECK(after->sessionId == before->sessionId);
    CHECK(device.isConnected());
}

TEST_CASE("An older cached inquiry reply cannot confirm a later setting",
          "[core][state][confirmation][late-response]") {
    auto transport = std::make_shared<ReplyTransport>();
    SonyDevice device(transport);
    device.connect(DeviceAddress("11:22:33:44:55:66"), "WH-1000XM5");
    const auto previous = device.snapshot()->revision;
    transport->notify({0x67, 0x17, 1, 0, 0, 0, 0});
    for (int wait = 0; wait < 200 && device.snapshot()->revision == previous; ++wait)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    REQUIRE(device.snapshot()->revision > previous);
    transport->acceptSettings = false;
    CHECK_THROWS_AS(device.setAnc(false), SonyException);
    CHECK(device.snapshot()->features.at("noiseControl").availability == "stale");
}

TEST_CASE("Failed device switching never relabels the previous headset state", "[core][state][selection]") {
    auto transport = std::make_shared<ReplyTransport>();
    DeviceService service(transport);
    service.connect(DeviceAddress("11:22:33:44:55:66"), "WH-1000XM5");
    const auto old = service.snapshot();
    REQUIRE(old->battery.main.has_value());
    transport->failAddress = "22:22:33:44:55:66";
    CHECK_THROWS(service.connect(DeviceAddress(transport->failAddress), "WH-1000XM6"));
    CHECK(service.selectedAddress() == transport->failAddress);
    CHECK(service.snapshot()->sessionId > old->sessionId);
    CHECK_FALSE(service.snapshot()->battery.main.has_value());
    CHECK(service.snapshot()->features.empty());
}

TEST_CASE("Legacy headphone controls and battery notifications remain observable", "[core][state][v1]") {
    auto transport = std::make_shared<ReplyTransport>();
    SonyDevice device(transport);
    device.connect(DeviceAddress("11:22:33:44:55:66"), "WH-1000XM3", InitialRefresh::None);
    transport->notify({0x69,2,0x11,1,0,1,1,14});
    for (int i = 0; i < 100 && device.snapshot()->noiseControl.ambientLevel != 14; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(device.snapshot()->noiseControl.mode == protocol::NoiseControlMode::Ambient);
    CHECK(device.snapshot()->noiseControl.focusOnVoice);
    CHECK(device.snapshot()->features.at("noiseControl").availability == "valid");
    transport->notify({0x13,0,73,0});
    for (int i = 0; i < 100 && device.snapshot()->battery.main != 73; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(device.snapshot()->battery.main == 73);
}

TEST_CASE("Firmware is cached per connection while setting fallbacks keep running",
          "[core][maintenance][firmware]") {
    auto now = DeviceService::Clock::time_point{};
    auto transport = std::make_shared<ReplyTransport>();
    DeviceService service(transport, {}, [&] { return now; });
    auto firmwareRequests = [&] {
        size_t count = 0;
        for (const auto& bytes : transport->sentFrames()) {
            const auto frame = protocol::FrameCodec::decode(bytes);
            if (frame.type == protocol::DataType::DataMdr &&
                frame.payload == std::vector<uint8_t>{4, 2}) ++count;
        }
        return count;
    };
    service.connect(DeviceAddress("11:22:33:44:55:66"), "WH-1000XM5");
    const auto session = service.snapshot()->sessionId;
    for (int tick = 0; tick < 27; ++tick) {
        now += std::chrono::seconds(5);
        service.tick();
    }
    CHECK(firmwareRequests() == 1);
    CHECK(service.snapshot()->features.at("firmware").availability == "valid");
    service.connect(DeviceAddress("11:22:33:44:55:66"), "WH-1000XM5");
    CHECK(service.snapshot()->sessionId > session);
    for (int tick = 0; tick < 9; ++tick) {
        now += std::chrono::seconds(5);
        service.tick();
    }
    CHECK(firmwareRequests() == 2);
}

TEST_CASE("V2 initialization timeout is reported as a failed connection", "[core][recovery]") {
    auto transport = std::make_shared<FakeTransport>();
    SonyDevice device(transport);

    CHECK_THROWS_AS(
        device.connect(DeviceAddress("11:22:33:44:55:66"), "WH-1000XM5", InitialRefresh::None),
        SonyException);
    CHECK_FALSE(device.isConnected());
}

TEST_CASE("Phone call handoff recovers the same control target while the OS link stays connected",
          "[core][recovery][multipoint][regression]") {
    auto now = DeviceService::Clock::time_point{};
    auto transport = std::make_shared<CallHandoffTransport>();
    auto discovery = std::make_shared<FakeDeviceDiscovery>();
    discovery->setDevices({
        {"WH-1000XM5", DeviceAddress("22:22:33:44:55:66"), true, true},
        {"WH-1000XM6", DeviceAddress("11:22:33:44:55:66"), true, true}});
    DeviceService service(transport, discovery, [&] { return now; }, InitialRefresh::None, true,
                          DeviceService::RecoveryPolicy::RequireSystemConnection);
    service.startAutoConnect("11:22:33:44:55:66");
    service.tick();
    REQUIRE(service.isConnected());

    SECTION("No control traffic returns, including the power-off ACK") {
        transport->failure = CallHandoffTransport::Failure::Silent;
        const auto result = JsonProtocol::execute(
            {{"version", 1}, {"id", 1}, {"method", "powerOff"},
             {"params", {{"targetAddress", "11:22:33:44:55:66"}}}}, service);
        REQUIRE(result["ok"] == false);
        CHECK(result["error"]["message"] == "Timeout waiting for ACK");
    }
    SECTION("Only ACKs return") {
        transport->failure = CallHandoffTransport::Failure::AckOnly;
        now += std::chrono::seconds(5);
        service.tick();
        REQUIRE(service.isConnected());
        now += std::chrono::seconds(5);
        service.tick();
    }
    SECTION("Only unrelated notifications return") {
        transport->failure = CallHandoffTransport::Failure::NotificationsOnly;
        now += std::chrono::seconds(5);
        service.tick();
        REQUIRE(service.isConnected());
        now += std::chrono::seconds(5);
        service.tick();
    }
    SECTION("ACKs and unrelated notifications return") {
        transport->failure = CallHandoffTransport::Failure::AckAndNotifications;
        now += std::chrono::seconds(5);
        service.tick();
        REQUIRE(service.isConnected());
        now += std::chrono::seconds(5);
        service.tick();
    }

    REQUIRE_FALSE(service.isConnected());
    REQUIRE(transport->isConnected());
    CHECK(service.connectionState() == "retrying");
    const auto sentBeforeGuard = transport->sentCount();
    CHECK_FALSE(JsonProtocol::execute(
        {{"version", 1}, {"id", 2}, {"method", "anc"},
         {"params", {{"enabled", true}, {"targetAddress", "11:22:33:44:55:66"}}}}, service)["ok"].get<bool>());
    CHECK(transport->sentCount() == sentBeforeGuard);

    service.tick();
    REQUIRE(service.isConnected());
    CHECK(service.selectedAddress() == "11:22:33:44:55:66");
    CHECK(service.activeDevice()->protocolVersion() == SonyProtocolVersion::V2);
    CHECK(transport->attempts == std::vector<std::string>{
        "11:22:33:44:55:66", "11:22:33:44:55:66"});
    CHECK(transport->closedConnections.load() == 1);
    CHECK(transport->reopenedWithoutClose.load() == 0);
    service.activeDevice()->refreshAll();
    service.activeDevice()->refreshMultipoint();
    for (const auto* feature : {"battery", "noiseControl", "equalizer", "dsee", "multipoint"}) {
        CAPTURE(feature);
        CHECK(service.snapshot()->features.at(feature).availability == "valid");
        CHECK(service.snapshot()->features.at(feature).error.empty());
    }
    REQUIRE(JsonProtocol::execute(
        {{"version", 1}, {"id", 3}, {"method", "anc"},
         {"params", {{"enabled", true}, {"targetAddress", "11:22:33:44:55:66"}}}}, service)["ok"].get<bool>());
    CHECK(service.snapshot()->noiseControl.mode == protocol::NoiseControlMode::NoiseCancelling);
    REQUIRE(JsonProtocol::execute(
        {{"version", 1}, {"id", 4}, {"method", "powerOff"},
         {"params", {{"targetAddress", "11:22:33:44:55:66"}}}}, service)["ok"].get<bool>());
    CHECK_FALSE(service.isConnected());
    CHECK(service.connectionState() == "waiting_for_power_on");
}

TEST_CASE("Automatic recovery never falls through to another paired headset", "[core][recovery][selection]") {
    auto transport = std::make_shared<ReplyTransport>();
    auto discovery = std::make_shared<FakeDeviceDiscovery>();
    discovery->setDevices({{"WH-1000XM3",DeviceAddress("11:22:33:44:55:66"),true,false},
        {"WH-1000XM3",DeviceAddress("22:22:33:44:55:66"),true,true}});
    DeviceService service(transport, discovery);
    SECTION("Selected device wins over the connected candidate") {
        service.startAutoConnect("11:22:33:44:55:66"); service.tick();
        CHECK(service.selectedAddress() == "11:22:33:44:55:66");
        CHECK(transport->attempts == std::vector<std::string>{"11:22:33:44:55:66"});
    }
    SECTION("Failed selected device does not fall through") {
        transport->failAddress = "22:22:33:44:55:66";
        service.startAutoConnect(transport->failAddress); service.tick();
        CHECK_FALSE(service.isConnected());
        CHECK(transport->attempts == std::vector<std::string>{transport->failAddress});
    }
    SECTION("Missing selected device does not connect another headset") {
        service.startAutoConnect("AA:BB:CC:DD:EE:FF"); service.tick();
        CHECK_FALSE(service.isConnected());
        CHECK(transport->attempts.empty());
    }
}

TEST_CASE("Explicit Bluetooth connections require a discovered paired device", "[core][security]") {
    auto transport = std::make_shared<ReplyTransport>();
    auto discovery = std::make_shared<FakeDeviceDiscovery>();
    discovery->addDevice({"WH-1000XM5", DeviceAddress("11:22:33:44:55:66"), true, false});
    DeviceService service(transport, discovery, [] { return DeviceService::Clock::now(); },
                          InitialRefresh::None, false);

    CHECK_THROWS_AS(
        service.connect(DeviceAddress("AA:BB:CC:DD:EE:FF"), "WH-1000XM5"),
        SonyException);
    CHECK(transport->attempts.empty());

    REQUIRE_NOTHROW(
        service.connect(DeviceAddress("11:22:33:44:55:66"), "WH-1000XM5"));
    CHECK(transport->attempts == std::vector<std::string>{"11:22:33:44:55:66"});
}

TEST_CASE("Retry delay doubles and is capped at thirty seconds", "[core][recovery]") {
    auto now = DeviceService::Clock::time_point{};
    auto transport = std::make_shared<ReplyTransport>();
    transport->failAddress = "11:22:33:44:55:66";
    auto discovery = std::make_shared<FakeDeviceDiscovery>();
    discovery->addDevice({"WH-1000XM5", DeviceAddress(transport->failAddress), true, false});
    DeviceService service(transport, discovery, [&] { return now; });
    service.startAutoConnect(transport->failAddress); service.tick();
    size_t count = 1;
    for (int delay : {1,2,4,8,16,30,30}) {
        now += std::chrono::milliseconds(delay * 1000 - 1); service.tick();
        CHECK(transport->attempts.size() == count);
        now += std::chrono::milliseconds(1); service.tick();
        CHECK(transport->attempts.size() == ++count);
    }
}
TEST_CASE("Settings maintenance spaces feature inquiries across the active session", "[core][recovery]") {
    auto now = DeviceService::Clock::time_point{};
    auto transport = std::make_shared<ReplyTransport>();
    DeviceService service(transport, {}, [&] { return now; }, InitialRefresh::None, true);
    service.connect(DeviceAddress("11:22:33:44:55:66"), "WH-1000XM5");
    const auto connectedFrames = transport->sentFrames().size();

    now += std::chrono::milliseconds(4999);
    service.tick();
    CHECK(transport->sentFrames().size() == connectedFrames);
    now += std::chrono::milliseconds(1);
    service.tick();
    const auto firstStepFrames = transport->sentFrames().size();
    CHECK(firstStepFrames > connectedFrames);

    service.tick();
    CHECK(transport->sentFrames().size() == firstStepFrames);
    now += std::chrono::milliseconds(4999);
    service.tick();
    CHECK(transport->sentFrames().size() == firstStepFrames);
    now += std::chrono::milliseconds(1);
    service.tick();
    CHECK(transport->sentFrames().size() > firstStepFrames);
}
TEST_CASE("Structured snapshots are escaped versioned and truthful", "[core][json]") {
    auto transport = std::make_shared<ReplyTransport>();
    auto discovery = std::make_shared<FakeDeviceDiscovery>();
    discovery->addDevice({"name\n\"|<img> 日本",DeviceAddress("11:22:33:44:55:66")});
    DeviceService service(transport, discovery);
    auto request = Json{{"version",1},{"id","test"},{"method","devices"}};
    auto response = Json::parse(JsonProtocol::executeLine(request.dump(), service));
    CHECK(response["id"] == "test");
    CHECK(response["data"][0]["name"] == "name\n\"|<img> 日本");
    request["version"] = 9;
    CHECK(JsonProtocol::execute(request,service)["error"]["code"] == "VersionMismatch");
    CHECK(Json::parse(JsonProtocol::executeLine("{broken",service))["ok"] == false);
    const auto deep = std::string(40, '[') + "0" + std::string(40, ']');
    CHECK(Json::parse(JsonProtocol::executeLine(deep,service))["error"]["code"] == "InvalidRequest");
    service.connect(DeviceAddress("11:22:33:44:55:66"),"WH-1000XM5");
    const auto snapshot = JsonProtocol::snapshot(service);
    CHECK(snapshot["connected"] == true);
    CHECK(snapshot["equalizer"]["bands"] == Json::array({1,2,3,4,5}));
    CHECK(snapshot["features"]["noiseControl"]["availability"] == "valid");
    CHECK(snapshot["codec"] == "Unknown");
    CHECK(JsonProtocol::execute({{"version",1},{"id",1},{"method","ambient"},{"params",{{"level",21}}}},service)["error"]["code"] == "InvalidRequest");
    service.disconnect();
    CHECK(JsonProtocol::snapshot(service)["features"]["noiseControl"]["availability"] == "stale");
    auto status = IpcProtocol::execute(IpcProtocol::parseCommand("status"),service);
    CHECK_FALSE(status.success); CHECK(status.message == "Device disconnected");
}
TEST_CASE("Notification callbacks may read state from another thread", "[core][events]") {
    auto transport = std::make_shared<ReplyTransport>();
    SonyDevice device(transport); device.connect(DeviceAddress("11:22:33:44:55:66"),"WH-1000XM5");
    std::promise<bool> completed;
    auto result = completed.get_future();
    auto id = device.events().onStateChanged([&](const auto&) {
        auto read = std::async(std::launch::async, [&] { return device.snapshot()->noiseControl.ambientLevel; });
        completed.set_value(read.get() == 12);
    });
    transport->notify({0x69,0x17,1,1,1,0,12});
    REQUIRE(result.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
    CHECK(result.get()); device.events().removeListener(id);
}
#ifndef _WIN32
namespace {
struct ClientFd {
    int fd{-1};
    explicit ClientFd(const std::string& path) {
        fd = ::socket(AF_UNIX,SOCK_STREAM,0);
        sockaddr_un address{}; address.sun_family=AF_UNIX;
        std::strcpy(address.sun_path,path.c_str());
        REQUIRE(::connect(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address)) == 0);
    }
    ~ClientFd() { if (fd >= 0) ::close(fd); }
};
}
TEST_CASE("IPC isolates idle oversized and disappearing clients", "[core][ipc][security]") {
    PrivateSocket socket;
    auto service = std::make_shared<DeviceService>(std::make_shared<FakeTransport>());
    IpcServer server(service,socket.path); server.start();
    IpcClient client(socket.path);
    SECTION("Idle client does not delay other clients") {
        ClientFd idle(socket.path);
        auto result = client.sendCommand("devices",std::chrono::milliseconds(300));
        CHECK(result.success);
    }
    SECTION("Oversized request is disconnected") {
        ClientFd attacker(socket.path);
        std::string bytes(17*1024,'x');
#ifdef MSG_NOSIGNAL
        ::send(attacker.fd,bytes.data(),bytes.size(),MSG_NOSIGNAL);
#else
        ::write(attacker.fd,bytes.data(),bytes.size());
#endif
        pollfd pfd{attacker.fd,POLLIN,0}; REQUIRE(::poll(&pfd,1,2000) > 0);
        char b; CHECK(::read(attacker.fd,&b,1) <= 0);
        CHECK(client.sendCommand("devices").success);
    }
    SECTION("Client disappears before response") {
        { ClientFd vanished(socket.path); ::write(vanished.fd,"devices\n",8); }
        CHECK(client.sendCommand("devices").success);
    }
    SECTION("JSON and legacy clients coexist") {
        auto response = Json::parse(client.request(R"({"version":1,"id":7,"method":"snapshot"})"));
        CHECK(response["ok"] == true); CHECK(response["id"] == 7);
        CHECK(client.sendCommand("devices").success);
    }
    SECTION("Second daemon cannot evict the first") {
        IpcServer second(service,socket.path); CHECK_THROWS(second.start());
        second.stop(); CHECK(client.sendCommand("devices").success);
    }
    struct stat st{}; REQUIRE(::lstat(socket.path.c_str(),&st) == 0);
    CHECK((st.st_mode & 0777) == 0600);
}
TEST_CASE("IPC refuses insecure paths and recovers an owned stale socket", "[core][ipc][security]") {
    PrivateSocket socket;
    auto service = std::make_shared<DeviceService>(std::make_shared<FakeTransport>());
    SECTION("Private parent required") {
        ::chmod(socket.directory.c_str(),0755);
        IpcServer server(service,socket.path); CHECK_THROWS(server.start());
    }
    SECTION("Never replaces a regular file") {
        std::ofstream(socket.path) << "keep";
        IpcServer server(service,socket.path); CHECK_THROWS(server.start());
        CHECK(std::filesystem::file_size(socket.path) == 4);
    }
    SECTION("Never follows a symlink") {
        std::filesystem::create_symlink(socket.directory / "missing",socket.path);
        IpcServer server(service,socket.path); CHECK_THROWS(server.start());
        CHECK(std::filesystem::is_symlink(socket.path));
    }
    SECTION("Stale socket can be reclaimed") {
        int fd = ::socket(AF_UNIX,SOCK_STREAM,0);
        sockaddr_un address{}; address.sun_family=AF_UNIX; std::strcpy(address.sun_path,socket.path.c_str());
        REQUIRE(::bind(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address)) == 0); ::close(fd);
        IpcServer server(service,socket.path); REQUIRE_NOTHROW(server.start());
        CHECK(IpcClient(socket.path).sendCommand("devices").success);
    }
}
#endif
