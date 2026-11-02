#include <catch2/catch_test_macros.hpp>
#include "sony/core/SonyDevice.h"
#include "sony/protocol/FrameCodec.h"
#include "sony/transport/FakeTransport.h"
#include "sony/transport/SonyError.h"

#include <algorithm>
#include <chrono>
#include <thread>
#include <atomic>
#include <vector>

using namespace sony;
using namespace sony::core;
using namespace sony::protocol;
using namespace sony::transport;

namespace {

class AutoAckFakeTransport : public FakeTransport {
public:
    size_t send(std::span<const std::byte> data) override {
        size_t res = FakeTransport::send(data);
        if (!data.empty()) {
            std::vector<uint8_t> bytes(data.size());
            std::transform(data.begin(), data.end(), bytes.begin(), [](std::byte b) {
                return static_cast<uint8_t>(b);
            });
            try {
                auto frame = FrameCodec::decode(bytes);
                if (frame.type == DataType::DataMdr) {
                    SonyFrame ackFrame{
                        .type = DataType::Ack,
                        .sequence = frame.sequence,
                        .payload = {}
                    };
                    queueIncoming(FrameCodec::encode(ackFrame));

                    // Auto-respond to known inquiry requests
                    if (!frame.payload.empty()) {
                        uint8_t op = frame.payload[0];
                        if (op == 0x68) {
                            _ncReply = frame.payload;
                            _ncReply[0] = 0x67;
                        }
                        if (op == 0x58) {
                            if (frame.payload[3] > 0) _eqReply = frame.payload;
                            _eqReply[0] = 0x57;
                            _eqReply[2] = frame.payload[2];
                        }
                        if (op == 0xe8) _dsee = frame.payload[2];
                        if (op == 0xf8) _toggles[frame.payload[1]] = frame.payload[2];
                        if (op == 0xf6) {
                            queueIncoming(FrameCodec::encode(SonyFrame{
                                .type = DataType::DataMdr, .sequence = _nextRespSeq(),
                                .payload = {0xf7, frame.payload[1],
                                    _toggles.contains(frame.payload[1]) ? _toggles[frame.payload[1]] : uint8_t{1}}}));
                        }
                        if (op == 0x00) { // Init query
                            queueIncoming(FrameCodec::encode(SonyFrame{
                                .type = DataType::DataMdr,
                                .sequence = _nextRespSeq(),
                                .payload = {0x01, 0x00}
                            }));
                        } else if (op == 0x22) { // Battery query
                            queueIncoming(FrameCodec::encode(SonyFrame{
                                .type = DataType::DataMdr,
                                .sequence = _nextRespSeq(),
                                .payload = {0x23, 0x00, 85, 0x00}
                            }));
                        } else if (op == 0x66) { // NC query
                            queueIncoming(FrameCodec::encode(SonyFrame{
                                .type = DataType::DataMdr,
                                .sequence = _nextRespSeq(),
                                .payload = _ncReply
                            }));
                        } else if (op == 0x56) { // EQ query
                            queueIncoming(FrameCodec::encode(SonyFrame{
                                .type = DataType::DataMdr,
                                .sequence = _nextRespSeq(),
                                .payload = _eqReply
                            }));
                        } else if (op == 0x26 || op == 0x20) {
                            const auto type = frame.payload[1];
                            std::vector<uint8_t> reply = op == 0x26
                                ? std::vector<uint8_t>{0x27, type, _autoMode, _autoTimer}
                                : std::vector<uint8_t>{0x21, type, 7, 0, 1, 2, 3, 4, 0x10, 0x11};
                            queueIncoming(FrameCodec::encode(SonyFrame{
                                .type = DataType::DataMdr, .sequence = _nextRespSeq(), .payload = reply}));
                        } else if (op == 0x28) {
                            _autoMode = frame.payload[2]; _autoTimer = frame.payload[3];
                        } else if (op == 0xe6) { // DSEE query
                            queueIncoming(FrameCodec::encode(SonyFrame{
                                .type = DataType::DataMdr,
                                .sequence = _nextRespSeq(),
                                .payload = {0xe7, 0x01, _dsee}
                            }));
                        }
                    }
                }
            } catch (...) {}
        }
        return res;
    }

private:
    uint8_t _nextRespSeq() {
        return _respSeq++;
    }
    uint8_t _respSeq{0};
    uint8_t _autoMode{0x11}, _autoTimer{0};
    uint8_t _dsee{0};
    std::map<uint8_t, uint8_t> _toggles;
    std::vector<uint8_t> _ncReply{0x67,0x17,1,1,0,0,0};
    std::vector<uint8_t> _eqReply{0x57,0,0,6,10,10,10,10,10,10};
};

} // namespace

TEST_CASE("SonyDevice lifecycle and profile identification", "[core][device]") {
    auto transport = std::make_shared<AutoAckFakeTransport>();
    SonyDevice dev(transport, SonyProtocolVersion::V2);

    CHECK_FALSE(dev.isConnected());
    CHECK(dev.protocolVersion() == SonyProtocolVersion::V2);
    CHECK(dev.profile().model == SonyModel::Unknown);

    DeviceAddress addr("11:22:33:44:55:66");
    dev.connect(addr, "WH-1000XM5");

    CHECK(dev.isConnected());
    CHECK(dev.profile().model == SonyModel::WH1000XM5);
    CHECK(dev.capabilities().noiseCancelling);
    CHECK(dev.capabilities().ambientSound);
    CHECK(dev.capabilities().equalizer);
    CHECK(dev.capabilities().dsee);
    CHECK(dev.capabilities().speakToChat);

    dev.disconnect();
    CHECK_FALSE(dev.isConnected());
}

TEST_CASE("SonyDevice control methods and state updates", "[core][device]") {
    auto transport = std::make_shared<AutoAckFakeTransport>();
    SonyDevice dev(transport, SonyProtocolVersion::V2);
    dev.connect(DeviceAddress("11:22:33:44:55:66"), "WH-1000XM5");

    std::atomic<int> stateChangeCount{0};
    std::atomic<int> ncChangeCount{0};
    std::atomic<int> eqChangeCount{0};

    dev.events().onStateChanged([&](const DeviceStateChanged&) {
        stateChangeCount.fetch_add(1);
    });

    dev.events().onNoiseControlChanged([&](const NoiseControlChanged&) {
        ncChangeCount.fetch_add(1);
    });

    dev.events().onEqualizerChanged([&](const EqualizerChanged&) {
        eqChangeCount.fetch_add(1);
    });

    SECTION("ANC control") {
        dev.setAnc(true);
        auto snap1 = dev.snapshot();
        CHECK(snap1->noiseControl.mode == NoiseControlMode::NoiseCancelling);
        CHECK(ncChangeCount.load() >= 1);

        dev.setAnc(false);
        auto snap2 = dev.snapshot();
        CHECK(snap2->noiseControl.mode == NoiseControlMode::Off);
    }

    SECTION("Ambient control") {
        dev.setAmbient(15, false);
        auto snap = dev.snapshot();
        CHECK(snap->noiseControl.mode == NoiseControlMode::Ambient);
        CHECK(snap->noiseControl.ambientLevel == 15);
        CHECK_FALSE(snap->noiseControl.focusOnVoice);
    }

    SECTION("Equalizer preset control") {
        dev.setEqualizerPreset(0x16); // Bass Boost
        auto snap = dev.snapshot();
        CHECK(snap->equalizer.preset == 0x16);
        CHECK(eqChangeCount.load() >= 1);

        for (int preset : {-1, 256, 272}) {
            dev.setEqualizerPreset(preset);
            auto unchanged = dev.snapshot();
            CHECK(unchanged->equalizer.preset == snap->equalizer.preset);
            CHECK(unchanged->equalizer.clearBass == snap->equalizer.clearBass);
            CHECK(unchanged->equalizer.bands == snap->equalizer.bands);
        }
    }

    SECTION("Equalizer custom control") {
        dev.setEqualizerCustom(4, {1, 2, 3, 4, 5});
        auto snap = dev.snapshot();
        CHECK(snap->equalizer.preset == 0xa0);
        CHECK(snap->equalizer.clearBass == 4);
        CHECK(snap->equalizer.bands[0] == 1);
        CHECK(snap->equalizer.bands[4] == 5);
    }

    SECTION("Feature toggles") {
        dev.setDsee(true);
        CHECK(dev.snapshot()->dsee == true);

        dev.setAutoPowerOff(3);
        CHECK(dev.snapshot()->autoPowerOff == 3);

        dev.setSpeakToChat(true);
        CHECK(dev.snapshot()->speakToChat == true);

        dev.setAdaptiveVolume(true);
        CHECK(dev.snapshot()->adaptiveVolume == true);
    }

    dev.disconnect();
}

TEST_CASE("SonyDevice unsolicited notification dispatch", "[core][device]") {
    auto transport = std::make_shared<AutoAckFakeTransport>();
    SonyDevice dev(transport, SonyProtocolVersion::V2);
    dev.connect(DeviceAddress("11:22:33:44:55:66"), "WH-1000XM5");

    std::atomic<int> batteryLevel{-1};
    dev.events().onBatteryChanged([&](const BatteryChanged& evt) {
        if (evt.battery.main.has_value()) {
            batteryLevel.store(*evt.battery.main);
        }
    });

    // Device notification for battery: 0x25, 0x00, level 88, not charging
    SonyFrame batNotif{
        .type = DataType::DataMdr,
        .sequence = 100,
        .payload = {0x25, 0x00, 88, 0x00}
    };
    transport->queueIncoming(FrameCodec::encode(batNotif));

    // Allow background session thread to process notification
    for (int i = 0; i < 50 && batteryLevel.load() != 88; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    CHECK(batteryLevel.load() == 88);
    CHECK(dev.snapshot()->battery.main.value_or(-1) == 88);

    dev.disconnect();
}
