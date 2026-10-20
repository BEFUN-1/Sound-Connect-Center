#pragma once
#include "SemanticTypes.h"
#include "SonyError.h"
#include <span>

namespace sony::protocol {
inline NoiseControlState decodeV2NoiseControl(std::span<const uint8_t> p) {
    const bool adaptive = p.size() >= 2 && p[1] == 0x19;
    if (p.size() != (adaptive ? 9u : 7u) || (p[1] != 0x17 && p[1] != 0x19) ||
        p[2] > 1 || p[3] > 1 || p[4] > 1 || p[5] > 1 ||
        (adaptive && (p[7] > 1 || p[8] > 2)))
        throw SonyException(SonyErrorCode::InvalidResponse, "Malformed noise-control response");
    NoiseControlState result;
    result.mode = p[3] == 0 ? NoiseControlMode::Off : p[4] == 1 ? NoiseControlMode::Ambient : NoiseControlMode::NoiseCancelling;
    result.ambientLevel = p[6];
    result.focusOnVoice = p[5] == 1;
    if (adaptive) { result.adaptiveNoiseCancelling = p[7] == 1; result.adaptiveSensitivity = p[8]; }
    return result;
}
}
