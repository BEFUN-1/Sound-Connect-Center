#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace sony::protocol {

struct BatteryState {
    std::optional<int> main;
    std::optional<int> left;
    std::optional<int> right;
    std::optional<int> caseBattery;
    bool charging{false};
};

enum class NoiseControlMode {
    Off,
    NoiseCancelling,
    Ambient
};

struct NoiseControlState {
    NoiseControlMode mode{NoiseControlMode::Off};
    int ambientLevel{0};
    bool focusOnVoice{false};
    bool adaptiveNoiseCancelling{false};
    int adaptiveSensitivity{0};
};

struct EqualizerState {
    int preset{0};
    int clearBass{0};
    // 5 elements (+ clearBass) on legacy devices; 10 elements, no separate
    // clearBass, on newer devices such as the WH-1000XM6 -- see
    // DeviceCapabilities::tenBandEqualizer.
    std::vector<int> bands{0, 0, 0, 0, 0};
};

struct MultipointDevice {
    std::string address;
    std::string name;
    int connectionIndex{0};
    int deviceClass{0};
    bool connected{false};
    bool playing{false};
};

struct MultipointState {
    bool supported{false};
    bool sourceSwitchSupported{false};
    bool sourceKeepSupported{false};
    int inquiredType{-1};
    int maxConnected{0};
    bool sourceKeeping{false};
    int playbackIndex{0};
    std::vector<MultipointDevice> devices;
    std::string error;
};

} // namespace sony::protocol
