// Explicit hardware diagnostic: queries only, never changes headset settings.
#include "sony/transport/PlatformTransport.h"
#include "sony/protocol/ProtocolV2.h"
#include "sony/protocol/MultipointCodec.h"
#include "sony/transport/Logger.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <iostream>

int main() {
    using namespace sony;
    using namespace sony::protocol;
    nlohmann::json result;
    Logger::setLogLevel(LogLevel::Error);
    try {
        auto discovery = transport::createPlatformDiscovery();
        const auto devices = discovery->discover();
        const auto found = std::find_if(devices.begin(), devices.end(),
            [](const auto& device) { return device.name == "WH-1000XM6"; });
        if (found == devices.end()) throw std::runtime_error("WH-1000XM6 unavailable");
        result["model"] = found->name;
        result["bluetoothConnectedAtDiscovery"] = found->connected.value_or(false);
        result["stage"] = "connect";
        auto transport = transport::createPlatformTransport();
        SonyProtocolSession session(transport.get());
        session.connect(found->address);
        ProtocolV2 protocol(session, true, true);
        result["stage"] = "init";
        protocol.initDevice();
        result["stage"] = "noise";
        const auto noise = protocol.getNoiseControl();
        result["noise"] = {
            {"mode", static_cast<int>(noise.mode)}, {"ambientLevel", noise.ambientLevel},
            {"adaptiveOn", noise.adaptiveNoiseCancelling}, {"sensitivity", noise.adaptiveSensitivity}};
        result["stage"] = "autoPowerOff";
        result["autoPowerOff"] = protocol.getAutoPowerOff();
        result["autoPowerOffOptions"] = protocol.getAutoPowerOffOptions();
        result["stage"] = "table2Functions";
        const auto functions = session.sendAndAwaitResponse(
            SonyFrame{.type = DataType::DataMdrNo2, .payload = {0x06, 0}},
            0x07, 0, std::chrono::milliseconds(1500), [](const auto&) { return true; });
        const auto supported = multipoint::functions(functions.payload);
        result["powerSoundSettingAdvertised"] = supported.count(0x47) != 0;
        if (supported.count(0x47)) {
            result["stage"] = "powerSound";
            const auto status = session.sendAndAwaitResponse(
                SonyFrame{.type = DataType::DataMdrNo2, .payload = {0x42, 0x31}},
                0x43, 0x31, std::chrono::milliseconds(1500), [](const auto&) { return true; });
            const auto param = session.sendAndAwaitResponse(
                SonyFrame{.type = DataType::DataMdrNo2, .payload = {0x46, 0x31}},
                0x47, 0x31, std::chrono::milliseconds(1500), [](const auto&) { return true; });
            result["powerSoundStatusBytes"] = status.payload;
            result["powerSoundParamBytes"] = param.payload;
        }
        result["stage"] = "firmware";
        result["firmware"] = protocol.getFirmwareVersion();
        result["stage"] = "complete";
        session.disconnect();
        std::cout << result.dump(2) << "\n";
        return 0;
    } catch (const std::exception& error) {
        result["error"] = error.what();
        std::cout << result.dump(2) << "\n";
        return 1;
    }
}
