#include "sony/transport/PlatformTransport.h"
#include <windows.h>
#include <psapi.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

int main(int argc, char** argv) {
    const int seconds = argc > 1 ? std::stoi(argv[1]) : 7200;
    if (seconds < 60 || seconds > 14400) return 2;
    auto discovery = sony::transport::createPlatformDiscovery();
    std::ofstream samples("windows-discovery-soak.csv");
    samples << "seconds,private_bytes,working_set,handles,enumerations,errors\n";
    const auto start = std::chrono::steady_clock::now();
    unsigned enumerations = 0, errors = 0;
    while (std::chrono::steady_clock::now() - start < std::chrono::seconds(seconds)) {
        try {
            // Paired-device metadata only: never open an RFCOMM/audio session.
            discovery->discover();
            discovery->localAddresses();
            ++enumerations;
        } catch (const std::exception&) {
            ++errors;
        }
        PROCESS_MEMORY_COUNTERS_EX memory{};
        memory.cb = sizeof(memory);
        DWORD handles = 0;
        if (!GetProcessMemoryInfo(GetCurrentProcess(),
                reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)) ||
            !GetProcessHandleCount(GetCurrentProcess(), &handles)) return 3;
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start).count();
        samples << elapsed << ',' << memory.PrivateUsage << ',' << memory.WorkingSetSize
            << ',' << handles << ',' << enumerations << ',' << errors << '\n';
        samples.flush();
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    std::cout << "Completed read-only Windows discovery: " << enumerations
              << " enumerations, " << errors << " errors\n";
    return errors == 0 && enumerations > 0 ? 0 : 1;
}
