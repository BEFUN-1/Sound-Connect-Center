#include <catch2/catch_test_macros.hpp>
#include "sony/protocol/MultipointCodec.h"
#include "sony/protocol/ProtocolV2.h"
#include "ReplyingFakeTransport.h"

using namespace sony;
using namespace sony::protocol;
namespace {
const std::string peer = "AA:BB:CC:DD:EE:FF";
std::vector<uint8_t> list(uint8_t type = 2) {
    std::vector<uint8_t> p{0x37, type, 2};
    const std::string history = "11:22:33:44:55:66";
    p.insert(p.end(), history.begin(), history.end());
    p.push_back(0);
    if (type == 2) p.insert(p.end(), {0x24, 0x04, 0x04});
    p.insert(p.end(), {3, 'o', 'l', 'd'});
    p.insert(p.end(), peer.begin(), peer.end());
    p.push_back(2); // Connection index 2, although this is vector element 1.
    if (type == 2) p.insert(p.end(), {0x00, 0x01, 0x0C});
    p.insert(p.end(), {3, 'P', 'C', '!'});
    p.push_back(2);
    return p;
}
struct Fixture {
    sony::test::ReplyingFakeTransport fake;
    SonyProtocolSession session{&fake};
    ProtocolV2 protocol{session};
    uint8_t seq{0};
    Fixture() { session.connect("00:11:22:33:44:55"); }
    void reply(const std::vector<uint8_t>& payload) {
        fake.queueReply({SonyFrame{.type = DataType::DataMdrNo2, .sequence = seq, .payload = payload}});
        seq ^= 1;
    }
    void discovery(uint8_t type = 2, bool source = true) {
        reply(source ? std::vector<uint8_t>{7, 0, 2, static_cast<uint8_t>(type == 2 ? 0x32 : 0x30), 0, 0x31, 0}
                     : std::vector<uint8_t>{7, 0, 1, static_cast<uint8_t>(type == 2 ? 0x32 : 0x30), 0});
        reply({0x31, type, 8, 2, 0});
    }
    void state(bool keeping = false, uint8_t type = 2, bool source = true) {
        reply(list(type));
        if (source) reply({0x37, 1, static_cast<uint8_t>(keeping ? 0 : 1)});
    }
    std::vector<std::vector<uint8_t>> requests() {
        std::vector<std::vector<uint8_t>> result;
        for (const auto& bytes : fake.sentFrames()) {
            const auto f = FrameCodec::decode(bytes);
            if (f.type == DataType::DataMdrNo2) result.push_back(f.payload);
        }
        return result;
    }
};
}
TEST_CASE("Multipoint: list uses connection index, class byte order and history", "[multipoint]") {
    for (uint8_t type : {0, 2}) {
        const auto value = multipoint::devices(list(type));
        REQUIRE(value.supported);
        REQUIRE(value.devices.size() == 2);
        REQUIRE(value.devices[0].address == peer);
        REQUIRE(value.devices[0].connected);
        REQUIRE(value.devices[0].playing);
        REQUIRE_FALSE(value.devices[1].connected);
        REQUIRE_FALSE(value.devices[1].playing);
        REQUIRE(value.devices[0].deviceClass == (type == 2 ? 0x00010C : 0xFFFFFF));
    }
    auto emptyPlayback = list(); emptyPlayback.back() = 0;
    const auto value = multipoint::devices(emptyPlayback);
    REQUIRE_FALSE(value.devices[0].playing);
}
TEST_CASE("Multipoint: malformed lists and unsupported function tables are rejected", "[multipoint]") {
    const auto original = list();
    for (size_t size = 0; size < original.size(); ++size) {
        const std::vector<uint8_t> p(original.begin(), original.begin() + size);
        REQUIRE_THROWS_AS(multipoint::devices(p), SonyException);
    }
    auto bad = original; bad[3] = 'Z';
    REQUIRE_THROWS_AS(multipoint::devices(bad), SonyException);
    bad = original; bad.back() = 9;
    REQUIRE_THROWS_AS(multipoint::devices(bad), SonyException);
    REQUIRE_THROWS_AS(multipoint::functions(std::vector<uint8_t>{7, 0, 1, 0x32}), SonyException);
    Fixture f; f.reply({7, 0, 0});
    REQUIRE_FALSE(f.protocol.getMultipoint().supported);
    REQUIRE(f.requests() == std::vector<std::vector<uint8_t>>{{6, 0}});
    REQUIRE_THROWS_AS(f.protocol.multipointUnpair(peer), SonyException);
    REQUIRE(f.requests().size() == 1);
}
TEST_CASE("Multipoint: literal second table requests and inverted ON value", "[multipoint]") {
    Fixture f; f.discovery(); f.state(true);
    const auto state = f.protocol.getMultipoint();
    REQUIRE(state.sourceKeeping);
    REQUIRE(state.sourceSwitchSupported);
    REQUIRE(state.maxConnected == 2);
    REQUIRE(f.requests() == std::vector<std::vector<uint8_t>>{{6,0}, {0x30,2}, {0x36,2}, {0x36,1}});
    f.state(true); f.reply({0x39,1,1,0});
    f.protocol.setMultipointSourceKeeping(false);
    REQUIRE(f.requests().back() == std::vector<uint8_t>{0x38,1,1});
    f.state(false); f.reply({0x39,1,0,0});
    f.protocol.setMultipointSourceKeeping(true);
    REQUIRE(f.requests().back() == std::vector<uint8_t>{0x38,1,0});
}
TEST_CASE("Multipoint: source switch waits for matching address and surfaces failure", "[multipoint]") {
    Fixture f; f.discovery(); f.state();
    std::vector<uint8_t> response{0x3D,1,3}; response.insert(response.end(), peer.begin(), peer.end());
    f.reply(response);
    REQUIRE_THROWS_AS(f.protocol.switchMultipointSource(peer), SonyException);
    std::vector<uint8_t> expected{0x3C,1}; expected.insert(expected.end(), peer.begin(), peer.end());
    REQUIRE(f.requests().back() == expected);
}
TEST_CASE("Multipoint: disconnect connect unpair require terminal result", "[multipoint]") {
    for (uint8_t action : {0, 1, 2}) {
        Fixture f; f.discovery(0, false); f.state(false, 0, false);
        std::vector<uint8_t> progress{0x3D,0,action,static_cast<uint8_t>((action << 4) + 2)};
        progress.insert(progress.end(), peer.begin(), peer.end());
        auto wrong = progress; wrong[3] = action << 4; wrong.back() = '0';
        auto done = progress; done[3] = action << 4;
        f.fake.queueReply({
            SonyFrame{.type=DataType::DataMdrNo2,.sequence=f.seq,.payload=progress},
            SonyFrame{.type=DataType::DataMdrNo2,.sequence=static_cast<uint8_t>(f.seq ^ 1),.payload=wrong},
            SonyFrame{.type=DataType::DataMdrNo2,.sequence=f.seq,.payload=done}});
        if (action == 0) f.protocol.multipointDisconnect(peer);
        if (action == 1) f.protocol.multipointConnect(peer);
        if (action == 2) f.protocol.multipointUnpair(peer);
        std::vector<uint8_t> expected{0x3C,0,action}; expected.insert(expected.end(), peer.begin(), peer.end());
        REQUIRE(f.requests().back() == expected);
    }
}
TEST_CASE("Multipoint: device-management busy result retries the requested operation", "[multipoint]") {
    for (uint8_t action : {0, 1, 2}) {
        Fixture f; f.discovery(0, false); f.state(false, 0, false);
        std::vector<uint8_t> busy{0x3D,0,action,static_cast<uint8_t>((action << 4) + 3)};
        busy.insert(busy.end(), peer.begin(), peer.end());
        auto done = busy; done[3] = action << 4;
        f.fake.queueReply({SonyFrame{.type=DataType::DataMdrNo2,.sequence=f.seq,.payload=busy}});
        f.fake.queueReply({SonyFrame{.type=DataType::DataMdrNo2,.sequence=static_cast<uint8_t>(f.seq ^ 1),.payload=done}});

        if (action == 0) f.protocol.multipointDisconnect(peer);
        if (action == 1) f.protocol.multipointConnect(peer);
        if (action == 2) f.protocol.multipointUnpair(peer);

        const auto requests = f.requests();
        REQUIRE(requests.size() == 5);
        REQUIRE(requests[3] == requests[4]);
        std::vector<uint8_t> expected{0x3C,0,action}; expected.insert(expected.end(), peer.begin(), peer.end());
        REQUIRE(requests[3] == expected);
    }
}
TEST_CASE("Multipoint: connect and disconnect accept asynchronous in-progress result", "[multipoint]") {
    for (uint8_t action : {0, 1}) {
        Fixture f; f.discovery(2, false); f.state(false, 2, false);
        std::vector<uint8_t> accepted{0x3D,2,action,static_cast<uint8_t>((action << 4) + 2)};
        accepted.insert(accepted.end(), peer.begin(), peer.end());
        f.reply(accepted);

        if (action == 0) f.protocol.multipointDisconnect(peer);
        else f.protocol.multipointConnect(peer);

        const auto requests = f.requests();
        REQUIRE(requests.size() == 4);
        std::vector<uint8_t> expected{0x3C,2,action}; expected.insert(expected.end(), peer.begin(), peer.end());
        REQUIRE(requests.back() == expected);
    }
}
TEST_CASE("Multipoint: unpair still requires a terminal result", "[multipoint]") {
    Fixture f; f.discovery(2, false); f.state(false, 2, false);
    std::vector<uint8_t> accepted{0x3D,2,2,0x22};
    accepted.insert(accepted.end(), peer.begin(), peer.end());
    f.reply(accepted);
    REQUIRE_THROWS_AS(f.protocol.multipointUnpair(peer), SonyException);
}
TEST_CASE("Multipoint: first table responses cannot satisfy second table request", "[multipoint][session]") {
    Fixture f;
    f.fake.queueReply({SonyFrame{.type=DataType::DataMdr,.sequence=0,.payload={7,0,0}},
        SonyFrame{.type=DataType::DataMdrNo2,.sequence=1,.payload={7,0,1,0x32,0}}});
    f.seq = 0;
    f.reply({0x31,2,8,2,0}); f.state(false, 2, false);
    REQUIRE(f.protocol.getMultipoint().supported);
    REQUIRE(f.requests().size() == 3);
}
TEST_CASE("Multipoint: XM6 LE capable management uses the class list", "[multipoint]") {
    Fixture f;
    f.reply({7,0,2,0x33,5,0x31,0xFF}); f.reply({0x31,2,8,2,0}); f.state();
    const auto state = f.protocol.getMultipoint();
    REQUIRE(state.supported);
    REQUIRE(state.inquiredType == 2);
    REQUIRE(state.devices[0].playing);
}
TEST_CASE("Multipoint: disabled table 2 is never queried", "[multipoint]") {
    Fixture f;
    f.fake.queueReply({SonyFrame{.type=DataType::DataMdr,.sequence=0,.payload={1,0,3,0,0x30,0x17,1,1}}});
    f.protocol.initDevice();
    REQUIRE_FALSE(f.protocol.getMultipoint().supported);
    REQUIRE(f.requests().empty());
}
TEST_CASE("ProtocolV2: accepts switching noise status and keeps ambient level", "[protocol][v2][noise]") {
    Fixture f;
    f.fake.queueReply({SonyFrame{.type=DataType::DataMdr,.sequence=0,.payload={0x67,0x17,0,1,0,0,12}}});
    const auto state = f.protocol.getNoiseControl();
    REQUIRE(state.mode == NoiseControlMode::NoiseCancelling);
    REQUIRE(state.ambientLevel == 12);
}
TEST_CASE("ProtocolV2: XM6 discovers type 19 and preserves adaptive noise fields", "[protocol][v2][noise]") {
    Fixture f;
    f.fake.queueReply({SonyFrame{.type=DataType::DataMdr,.sequence=0,.payload={1,0,3,0,0x30,0x17,0,0}}});
    f.fake.queueReply({SonyFrame{.type=DataType::DataMdr,.sequence=1,.payload={7,0,1,0x6D,0}}});
    f.protocol.initDevice();
    f.fake.queueReply({SonyFrame{.type=DataType::DataMdr,.sequence=0,.payload={0x67,0x19,1,1,0,0,10,1,2}}});
    auto state = f.protocol.getNoiseControl();
    REQUIRE(state.mode == NoiseControlMode::NoiseCancelling);
    REQUIRE(state.adaptiveNoiseCancelling);
    REQUIRE(state.adaptiveSensitivity == 2);
    state.mode = NoiseControlMode::Off;
    f.fake.queueReply({SonyFrame{.type=DataType::Ack,.sequence=0}});
    f.protocol.setNoiseControl(state);
    REQUIRE(FrameCodec::decode(f.fake.lastSentFrame()).payload == std::vector<uint8_t>{0x68,0x19,1,0,0,0,10,1,2});
}
