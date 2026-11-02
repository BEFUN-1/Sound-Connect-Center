#include <catch2/catch_test_macros.hpp>
#include "sony/protocol/CapabilityDiscovery.h"
#include "sony/protocol/ProtocolV2.h"
#include "sony/protocol/FrameCodec.h"
#include "sony/transport/FakeTransport.h"

#include <filesystem>

using namespace sony::protocol;
using namespace sony::transport;

TEST_CASE("CapabilityDiscovery: known devices resolve immediately via registry without probing", "[protocol][capabilities]")
{
    FakeTransport fake;
    SonyProtocolSession session(&fake);
    session.connect("11:22:33:44:55:66");

    ProtocolV2 proto(session);
    CapabilityDiscovery discovery;

    // Discover capabilities for WH-1000XM5
    auto caps = discovery.discover(proto, "Sony WH-1000XM5");

    // Must resolve immediately with known capabilities
    REQUIRE(caps.battery == true);
    REQUIRE(caps.noiseCancelling == true);
    REQUIRE(caps.ambientSound == true);
    REQUIRE(caps.equalizer == true);
    REQUIRE(caps.dsee == true);
    REQUIRE(caps.speakToChat == true);
    REQUIRE(caps.adaptiveVolume == true);
    REQUIRE(caps.multipoint == true);

    // ZERO frames sent because no probing was needed!
    REQUIRE(fake.sentCount() == 0);
}

TEST_CASE("CapabilityDiscovery: unknown device probes and populates cache", "[protocol][capabilities]")
{
    FakeTransport fake;
    SonyProtocolSession session(&fake);
    session.connect("11:22:33:44:55:66");

    ProtocolV2 proto(session);
    auto cache = std::make_shared<CapabilityCache>();
    CapabilityDiscovery discovery(cache);

    // Fake an unknown device: probeDevice checks every optional feature in
    // order, so provide a complete response script instead of letting the
    // unmentioned requests consume the later DSEE fixture.
    // 1. Firmware
    fake.queueIncoming(FrameCodec::encode(SonyFrame{ .type = DataType::Ack, .sequence = 0 }));
    fake.queueIncoming(FrameCodec::encode(SonyFrame{ .type = DataType::DataMdr, .sequence = 1, .payload = {0x05, 0x02, 0x00, '1', '.', '0'} }));
    // 2. Codec
    fake.queueIncoming(FrameCodec::encode(SonyFrame{ .type = DataType::Ack, .sequence = 1 }));
    fake.queueIncoming(FrameCodec::encode(SonyFrame{ .type = DataType::DataMdr, .sequence = 0, .payload = {0x13, 0x02, 0x10} }));
    // 3. Battery
    fake.queueIncoming(FrameCodec::encode(SonyFrame{ .type = DataType::Ack, .sequence = 2 }));
    fake.queueIncoming(FrameCodec::encode(SonyFrame{ .type = DataType::DataMdr, .sequence = 1, .payload = {0x23, 0x00, 90, 0} }));
    // 4. Noise Control
    fake.queueIncoming(FrameCodec::encode(SonyFrame{ .type = DataType::Ack, .sequence = 3 }));
    fake.queueIncoming(FrameCodec::encode(SonyFrame{ .type = DataType::DataMdr, .sequence = 0,
        .payload = {0x67, 0x17, 0x01, 1, 1, 0, 12} }));
    // 5. Equalizer
    fake.queueIncoming(FrameCodec::encode(SonyFrame{ .type = DataType::Ack, .sequence = 4 }));
    fake.queueIncoming(FrameCodec::encode(SonyFrame{ .type = DataType::DataMdr, .sequence = 1,
        .payload = {0x57, 0x00, 0x16, 0x06, 13, 10, 11, 12, 13, 14} }));
    // 6. DSEE
    fake.queueIncoming(FrameCodec::encode(SonyFrame{ .type = DataType::Ack, .sequence = 5 }));
    fake.queueIncoming(FrameCodec::encode(SonyFrame{ .type = DataType::DataMdr, .sequence = 0, .payload = {0xe7, 0x01, 0x01} }));
    // 7. Speak-to-Chat
    fake.queueIncoming(FrameCodec::encode(SonyFrame{ .type = DataType::Ack, .sequence = 6 }));
    fake.queueIncoming(FrameCodec::encode(SonyFrame{ .type = DataType::DataMdr, .sequence = 1,
        .payload = {0xf7, 0x0c, 0x00} }));
    // 8. Adaptive volume
    fake.queueIncoming(FrameCodec::encode(SonyFrame{ .type = DataType::Ack, .sequence = 7 }));
    fake.queueIncoming(FrameCodec::encode(SonyFrame{ .type = DataType::DataMdr, .sequence = 0,
        .payload = {0xf7, 0x0a, 0x00} }));
    // 9. Auto power-off
    fake.queueIncoming(FrameCodec::encode(SonyFrame{ .type = DataType::Ack, .sequence = 8 }));
    fake.queueIncoming(FrameCodec::encode(SonyFrame{ .type = DataType::DataMdr, .sequence = 1,
        .payload = {0x27, 0x05, 0x00, 0x00} }));

    auto caps = discovery.probeDevice(proto, SonyModel::Unknown, "AA:BB:CC:DD:EE:FF");
    REQUIRE(caps.firmwareInfo == true);
    REQUIRE(caps.battery == true);
    REQUIRE(caps.dsee == true);

    // Verify cache was populated
    std::string key = CapabilityCache::makeKey(SonyModel::Unknown, "1.0", "AA:BB:CC:DD:EE:FF");
    REQUIRE(cache->has(key));
    auto cached = cache->get(key);
    REQUIRE(cached.has_value());
    REQUIRE(cached->battery == true);
    REQUIRE(cached->dsee == true);
}

TEST_CASE("CapabilityDiscovery: discoverAsync runs non-blocking", "[protocol][capabilities]")
{
    FakeTransport fake;
    SonyProtocolSession session(&fake);
    session.connect("11:22:33:44:55:66");

    ProtocolV2 proto(session);
    CapabilityDiscovery discovery;

    auto future = discovery.discoverAsync(proto, "WH-1000XM4");
    REQUIRE(future.valid());

    auto caps = future.get();
    REQUIRE(caps.noiseCancelling == true);
    REQUIRE(caps.wearSensor == true);
    REQUIRE(caps.equalizer == true);
    REQUIRE(caps.speakToChat == true);
    REQUIRE(fake.sentCount() == 0);
}

TEST_CASE("CapabilityCache: persistence to file", "[protocol][capabilities]")
{
    auto tempPath = std::filesystem::temp_directory_path() / "test_sony_caps_cache.txt";

    {
        CapabilityCache cache;
        DeviceCapabilities caps;
        caps.battery = true;
        caps.equalizer = true;
        caps.dsee = true;

        cache.put("WH-1000XM5@2.0.1", caps);
        REQUIRE(cache.saveToFile(tempPath));
    }

    {
        CapabilityCache loadedCache;
        REQUIRE(loadedCache.loadFromFile(tempPath));
        auto retrieved = loadedCache.get("WH-1000XM5@2.0.1");
        REQUIRE(retrieved.has_value());
        REQUIRE(retrieved->battery == true);
        REQUIRE(retrieved->equalizer == true);
        REQUIRE(retrieved->dsee == true);
        REQUIRE(retrieved->speakToChat == false);
    }

    std::filesystem::remove(tempPath);
}
