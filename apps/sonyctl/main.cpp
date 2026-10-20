#include "sony/core/DeviceService.h"
#include "DesktopSession.h"
#include "sony/core/IpcClient.h"
#include "sony/core/IpcProtocol.h"
#include "sony/protocol/FrameCodec.h"
#include "sony/transport/FakeTransport.h"
#include "sony/transport/PlatformTransport.h"
#include "sony/transport/Logger.h"


#include <algorithm>
#include <cctype>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace sony;
using namespace sony::core;
using namespace sony::protocol;
using namespace sony::transport;

namespace {

void printHelp() {
    std::cout << "sonyctl — CLI diagnostic and control tool for Sony audio devices\n\n"
              << "Usage: sonyctl [options] <command> [args...]\n\n"
              << "Commands:\n"
              << "  devices                    List discovered paired Sony devices\n"
              << "  info                       Display connected device information & capabilities\n"
              << "  battery                    Display battery percentage and charging state\n"
              << "  battery json               Refresh only battery and return a JSON snapshot\n"
              << "  anc on|off                 Enable or disable Active Noise Cancelling\n"
              << "  ambient <1-20>|off         Set Ambient Sound level or turn ambient off\n"
              << "  eq get                     Display active Equalizer preset and band levels\n"
              << "  eq preset <name>           Set Equalizer preset (bright, bass-boost, vocal, etc.)\n"
              << "  eq custom <cb> <b1..b5>    Apply custom Clear Bass (-10..10) and 5 EQ bands\n"
              << "  eq <preset>                Shorthand for eq preset <preset>\n"
              << "  dsee on|off|auto           Toggle DSEE sound enhancement\n"
              << "  apo <0-5>                  Set Auto-Power-Off duration preset index\n"
              << "  poweroff                  Power off the connected headphones immediately\n"
              << "  status                     Display connection status\n\n"
              << "  dashboard                  JSON headset status and internal multipoint devices\n"
              << "  multipoint list            List headset connection/pairing history as JSON\n"
              << "  multipoint source <addr>   Switch playback to a connected source\n"
              << "  multipoint default <addr>  Fix playback to a connected source\n"
              << "  multipoint keep on|off     Enable/disable fixed playback source\n"
              << "  multipoint connect|disconnect|unpair <addr>  Manage headset peers\n\n"
              << "Options:\n"
              << "  -s, --socket <path>        Custom Unix domain socket path for sonyd\n"
              << "  -d, --device <address>     Use a specific discovered Bluetooth address\n"
              << "  --direct                   Direct execution bypassing daemon\n"
              << "  --session                  Persistent desktop backend over stdin/stdout\n"
              << "  -v, --verbose              Enable verbose diagnostic logging\n"
              << "  -h, --help                 Display this help menu\n\n"
              << "Examples:\n"
              << "  sonyctl anc on\n"
              << "  sonyctl ambient 10\n"
              << "  sonyctl eq bass-boost\n"
              << "  sonyctl dsee on\n";
}

} // namespace

int main(int argc, char* argv[]) {
    std::string socketPath = defaultSocketPath();
    std::string selectedAddress;
    bool direct = false;
    bool verbose = false;
    bool session = false;
    std::vector<std::string> commandTokens;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printHelp();
            return 0;
        } else if ((arg == "-s" || arg == "--socket") && i + 1 < argc) {
            socketPath = argv[++i];
        } else if ((arg == "-d" || arg == "--device") && i + 1 < argc) {
            selectedAddress = argv[++i];
        } else if (arg == "--direct") {
            direct = true;
        } else if (arg == "--session") {
            session = true;
            direct = true;
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else {
            commandTokens.push_back(std::move(arg));
        }
    }

    if (commandTokens.empty() && !session) {
        printHelp();
        return 0;
    }

    if (verbose) {
        Logger::setLogLevel(LogLevel::Debug);
        Logger::setDeveloperMode(true);
    }

    // Build command line string
    std::ostringstream oss;
    for (size_t i = 0; i < commandTokens.size(); ++i) {
        oss << commandTokens[i];
        if (i + 1 < commandTokens.size()) oss << " ";
    }
    std::string commandLine = oss.str();

    IpcClient daemon(socketPath);
    if (direct && daemon.isDaemonRunning()) {
        std::cerr << "Error: sonyd is running and may own the Bluetooth session. Stop sonyd before using --direct.\n";
        return 1;
    }
    // If direct execution requested or daemon not running, connect via local DeviceService
    if (!direct) {
        IpcClient client(socketPath);
        if (client.isDaemonRunning()) {
            auto resp = client.sendCommand(commandLine);
            if (resp.success) {
                if (!resp.data.empty()) {
                    std::cout << resp.data << "\n";
                } else if (!resp.message.empty()) {
                    std::cout << resp.message << "\n";
                }
                return 0;
            } else {
                std::cerr << "Error: " << resp.message << "\n";
                return 1;
            }
        }
    }

    // Fallback or Direct mode
    if (direct) std::cerr << "Using a direct Bluetooth session (--direct).\n";
    else std::cerr << "Notice: sonyd daemon is not running at " << socketPath << "\n"
              << "Starting direct session...\n";

    std::shared_ptr<ITransport> transport = transport::createPlatformTransport();
    std::shared_ptr<IDeviceDiscovery> discovery = transport::createPlatformDiscovery();
    // One-shot commands need only their own state. In particular, a tray
    // battery poll must not also read noise control, EQ, DSEE and multipoint.
    DeviceService service(transport, discovery, [] { return DeviceService::Clock::now(); },
                          InitialRefresh::None, !session);
    if (session) return runDesktopSession(service);

    auto cmd = IpcProtocol::parseCommand(commandLine);
    if (cmd.type != IpcCommandType::Devices) {
        auto devs = service.discoverDevices();
        if (!devs.empty()) {
            try {
                const auto it = selectedAddress.empty()
                    ? devs.begin()
                    : std::find_if(devs.begin(), devs.end(), [&](const auto& device) {
                        return device.address.size() == selectedAddress.size() &&
                            std::equal(device.address.begin(), device.address.end(), selectedAddress.begin(),
                                [](unsigned char left, unsigned char right) {
                                    return std::tolower(left) == std::tolower(right);
                                });
                    });
                if (it == devs.end()) {
                    std::cerr << "Error: selected device was not found: " << selectedAddress << "\n";
                    return 1;
                }
                service.connect(DeviceAddress(it->address), it->name);
                auto* device = service.activeDevice();
                switch (cmd.type) {
                case IpcCommandType::Info:
                case IpcCommandType::Dashboard: device->refreshAll(); break;
                case IpcCommandType::EqGet: device->refreshEqualizer(); break;
                // Preserve the device's ancillary noise settings when changing
                // just the mode, without waiting for unrelated feature reads.
                case IpcCommandType::Anc:
                case IpcCommandType::Ambient: device->refreshNoiseControl(); break;
                default: break;
                }
            } catch (const std::exception& ex) {
                std::cerr << "Notice: initial connection to " << devs.front().name << " deferred: " << ex.what() << "\n";
            }
        }
    }

    auto resp = IpcProtocol::execute(cmd, service);


    if (resp.success) {
        if (!resp.data.empty()) {
            std::cout << resp.data << "\n";
        } else if (!resp.message.empty()) {
            std::cout << resp.message << "\n";
        }
        return 0;
    } else {
        std::cerr << "Error: " << resp.message << "\n";
        return 1;
    }
}
