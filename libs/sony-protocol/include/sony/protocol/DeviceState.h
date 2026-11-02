#pragma once

#include "SemanticTypes.h"
#include <memory>
#include <map>
#include <chrono>

namespace sony::protocol {

struct FeatureStatus {
    std::string availability{"unknown"};
    int64_t lastSuccessMs{0};
    std::string error;
};
inline int64_t stateTimestamp() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
struct DeviceState {
    uint64_t sessionId{0};
    uint64_t revision{0};
    std::map<std::string, FeatureStatus> features;
    BatteryState battery;
    NoiseControlState noiseControl;
    EqualizerState equalizer;
    MultipointState multipoint;

    bool dsee{false};

    std::string firmware;
    std::string codec;

    int autoPowerOff{0};
    std::vector<int> autoPowerOffOptions;
    bool speakToChat{false};
    bool adaptiveVolume{false};
};

using DeviceStateSnapshot = std::shared_ptr<const DeviceState>;

} // namespace sony::protocol
