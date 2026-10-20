#pragma once

#include "SemanticTypes.h"
#include "SonyError.h"
#include <string>
#include <string_view>
#include <vector>

namespace sony::protocol {

enum class ProtocolGeneration {
    V1,
    V2
};

class IProtocol {
public:
    virtual ~IProtocol() = default;

    [[nodiscard]] virtual ProtocolGeneration generation() const noexcept = 0;

    virtual void initDevice() = 0;

    virtual BatteryState getBattery() = 0;

    virtual NoiseControlState getNoiseControl() = 0;
    virtual void setNoiseControl(const NoiseControlState& state) = 0;

    virtual EqualizerState getEqualizer() = 0;
    virtual void setEqualizerPreset(int preset) = 0;
    // bands.size() is 5 for legacy devices, 10 for tenBandEqualizer devices
    // (clearBass is ignored by implementations in the latter case).
    virtual void setEqualizerCustom(int clearBass, const std::vector<int>& bands) = 0;

    virtual bool getDsee() = 0;
    virtual void setDsee(bool enabled) = 0;

    virtual std::string getFirmwareVersion() = 0;
    virtual std::string getCodec() = 0;

    virtual int getAutoPowerOff() = 0;
    virtual void setAutoPowerOff(int index) = 0;

    virtual bool getSpeakToChat() = 0;
    virtual void setSpeakToChat(bool enabled) = 0;

    virtual bool getAdaptiveVolume() = 0;
    virtual void setAdaptiveVolume(bool enabled) = 0;

    // Request an immediate user power-off on the headset.
    virtual void powerOff() = 0;

    // Sony's second protocol table (DataMdrNo2) exposes multipoint state on
    // newer headsets. Older protocol implementations keep the default
    // unsupported behavior so callers can disable those controls cleanly.
    virtual MultipointState getMultipoint() {
        throw SonyException(SonyErrorCode::Unsupported, "Multipoint is not supported by this protocol");
    }
    virtual void switchMultipointSource(std::string_view) {
        throw SonyException(SonyErrorCode::Unsupported, "Multipoint source switching is not supported");
    }
    virtual void setMultipointSourceKeeping(bool) {
        throw SonyException(SonyErrorCode::Unsupported, "Multipoint source keeping is not supported");
    }
    virtual void multipointConnect(std::string_view) {
        throw SonyException(SonyErrorCode::Unsupported, "Multipoint connect is not supported");
    }
    virtual void multipointDisconnect(std::string_view) {
        throw SonyException(SonyErrorCode::Unsupported, "Multipoint disconnect is not supported");
    }
    virtual void multipointUnpair(std::string_view) {
        throw SonyException(SonyErrorCode::Unsupported, "Multipoint unpair is not supported");
    }
};

} // namespace sony::protocol
