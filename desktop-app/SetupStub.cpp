#include <windows.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {
constexpr char kMarker[] = "SONYSC_PAYLOAD1";
constexpr size_t kMarkerSize = sizeof(kMarker) - 1;

std::wstring modulePath() {
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) return {};
        if (length < buffer.size() - 1) return std::wstring(buffer.data(), length);
        buffer.resize(buffer.size() * 2);
    }
}

std::wstring quotePowerShell(const std::wstring& value) {
    std::wstring escaped = value;
    size_t position = 0;
    while ((position = escaped.find(L'\'', position)) != std::wstring::npos) {
        escaped.insert(position, 1, L'\'');
        position += 2;
    }
    return L"'" + escaped + L"'";
}

std::wstring tempDirectory() {
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetTempPathW(MAX_PATH, buffer);
    if (length == 0 || length >= MAX_PATH) return {};
    std::wstring path(buffer, length);
    path += L"SonyDeviceCenterSetup_" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(path.c_str(), nullptr);
    return path;
}

void logLine(const std::wstring& text) {
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetTempPathW(MAX_PATH, buffer);
    if (length == 0 || length >= MAX_PATH) return;
    const std::wstring path = std::wstring(buffer, length) + L"SonyDeviceCenterSetup.log";
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    const std::wstring line = text + L"\r\n";
    DWORD written = 0;
    WriteFile(file, line.data(), static_cast<DWORD>(line.size() * sizeof(wchar_t)), &written, nullptr);
    CloseHandle(file);
}

std::string narrow(const std::wstring& value) {
    const int length = WideCharToMultiByte(CP_ACP, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) return {};
    std::string result(static_cast<size_t>(length - 1), '\0');
    WideCharToMultiByte(CP_ACP, 0, value.c_str(), -1, result.data(), length - 1, nullptr, nullptr);
    return result;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    logLine(L"start");
    const std::wstring self = modulePath();
    const std::wstring temp = tempDirectory();
    if (self.empty() || temp.empty()) {
        logLine(L"path failure");
        MessageBoxW(nullptr, L"无法准备安装目录。", L"Sony Device Center", MB_ICONERROR | MB_OK);
        return 1;
    }

    std::ifstream input(narrow(self), std::ios::binary);
    if (!input) {
        logLine(L"input failure: " + self);
        MessageBoxW(nullptr, L"无法读取安装程序。", L"Sony Device Center", MB_ICONERROR | MB_OK);
        return 1;
    }
    input.seekg(0, std::ios::end);
    const std::streamoff fileSize = input.tellg();
    if (fileSize < static_cast<std::streamoff>(kMarkerSize + sizeof(uint64_t))) {
        logLine(L"file too small");
        MessageBoxW(nullptr, L"安装程序文件不完整。", L"Sony Device Center", MB_ICONERROR | MB_OK);
        return 1;
    }

    input.seekg(fileSize - static_cast<std::streamoff>(kMarkerSize), std::ios::beg);
    char marker[kMarkerSize]{};
    input.read(marker, static_cast<std::streamsize>(kMarkerSize));
    if (memcmp(marker, kMarker, kMarkerSize) != 0) {
        logLine(L"marker failure");
        MessageBoxW(nullptr, L"安装程序 payload 校验失败。", L"Sony Device Center", MB_ICONERROR | MB_OK);
        return 1;
    }

    uint64_t payloadSize = 0;
    input.seekg(fileSize - static_cast<std::streamoff>(kMarkerSize + sizeof(uint64_t)), std::ios::beg);
    input.read(reinterpret_cast<char*>(&payloadSize), sizeof(payloadSize));
    const auto payloadStart = fileSize - static_cast<std::streamoff>(kMarkerSize + sizeof(uint64_t)) - static_cast<std::streamoff>(payloadSize);
    if (payloadSize == 0 || payloadStart < 0) {
        logLine(L"payload size failure");
        MessageBoxW(nullptr, L"安装程序 payload 大小无效。", L"Sony Device Center", MB_ICONERROR | MB_OK);
        return 1;
    }

    const std::wstring archive = temp + L"\\payload.zip";
    std::ofstream output(narrow(archive), std::ios::binary);
    if (!output) return 1;
    logLine(L"writing archive: " + archive);
    input.seekg(payloadStart, std::ios::beg);
    std::vector<char> buffer(1024 * 1024);
    uint64_t remaining = payloadSize;
    while (remaining > 0) {
        const auto chunk = static_cast<std::streamsize>(remaining < buffer.size() ? remaining : buffer.size());
        input.read(buffer.data(), chunk);
        if (input.gcount() != chunk) { logLine(L"archive read failure"); return 1; }
        output.write(buffer.data(), chunk);
        remaining -= static_cast<uint64_t>(chunk);
    }
    output.close();

    const std::wstring payload = temp + L"\\payload";
    const std::wstring command = L"-NoProfile -ExecutionPolicy Bypass -Command \"Expand-Archive -LiteralPath "
        + quotePowerShell(archive) + L" -DestinationPath " + quotePowerShell(payload)
        + L" -Force; & " + quotePowerShell(payload + L"\\InstallPayload.ps1") + L"\"";
    std::vector<wchar_t> commandLine(command.begin(), command.end());
    commandLine.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(L"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe",
                                        commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                                        nullptr, temp.c_str(), &startup, &process);
    if (!started) {
        logLine(L"CreateProcess failure: " + std::to_wstring(GetLastError()));
        MessageBoxW(nullptr, L"无法启动安装过程。", L"Sony Device Center", MB_ICONERROR | MB_OK);
        return static_cast<int>(GetLastError());
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    logLine(L"child exit: " + std::to_wstring(exitCode));
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<int>(exitCode);
}
