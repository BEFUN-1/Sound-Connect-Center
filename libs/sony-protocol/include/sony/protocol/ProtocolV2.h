#pragma once

#include "IProtocol.h"
#include "SonyProtocolSession.h"
#include <mutex>

namespace sony::protocol {

class ProtocolV2 : public IProtocol {
public:
    // tenBandEqualizer selects the newer 10-band equalizer wire format
    // (inquired type 0x04, no separate Clear Bass slot) used by devices such
    // as the WH-1000XM6, instead of the legacy 5-band + Clear Bass format
    // (inquired type 0x00) shared with ProtocolV1. See DeviceProfile.h and
    // issue #10.
    explicit ProtocolV2(SonyProtocolSession& session, bool tenBandEqualizer = false);
    ~ProtocolV2() override = default;

    [[nodiscard]] ProtocolGeneration generation() const noexcept override {
        return ProtocolGeneration::V2;
    }

    void initDevice() override;

    // V2 uses opcode 0x22 for battery inquiries
    BatteryState getBattery() override;

    NoiseControlState getNoiseControl() override;
    void setNoiseControl(const NoiseControlState& state) override;

    EqualizerState getEqualizer() override;
    void setEqualizerPreset(int preset) override;
    void setEqualizerCustom(int clearBass, const std::vector<int>& bands) override;

    bool getDsee() override;
    void setDsee(bool enabled) override;

    std::string getFirmwareVersion() override;
    std::string getCodec() override;

    int getAutoPowerOff() override;
    void setAutoPowerOff(int index) override;

    bool getSpeakToChat() override;
    void setSpeakToChat(bool enabled) override;

    bool getAdaptiveVolume() override;
    void setAdaptiveVolume(bool enabled) override;
    void powerOff() override;

    MultipointState getMultipoint() override;
    void switchMultipointSource(std::string_view address) override;
    void setMultipointSourceKeeping(bool enabled) override;
    void multipointConnect(std::string_view address) override;
    void multipointDisconnect(std::string_view address) override;
    void multipointUnpair(std::string_view address) override;

private:
    SonyProtocolSession& _session;
    std::mutex _mutex;
    bool _tenBandEqualizer;
    uint8_t _noiseType{0x17};
    bool _multipointDiscovered{false};
    std::optional<bool> _table2Supported;
    MultipointState _multipoint;
    void discoverMultipoint();
    void multipointAction(std::string_view address, uint8_t action);

    SonyFrame sendNo2AndAwait(const std::vector<uint8_t>& request,
                              uint8_t retOpcode,
                              int retSubtype,
                              std::chrono::milliseconds timeout = std::chrono::milliseconds(1200));
    void sendNo2(const std::vector<uint8_t>& request);
};

} // namespace sony::protocol
