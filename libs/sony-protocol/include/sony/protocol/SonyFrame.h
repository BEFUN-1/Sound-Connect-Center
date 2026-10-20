#pragma once

#include "DataType.h"
#include <cstdint>
#include <vector>

namespace sony::protocol {

struct SonyFrame {
    DataType type{DataType::DataMdr};
    uint8_t sequence{0};
    std::vector<uint8_t> payload{};

    bool operator==(const SonyFrame& other) const {
        return type == other.type && sequence == other.sequence && payload == other.payload;
    }
};

} // namespace sony::protocol
