#include <windows.h>

#include <string>
#include <vector>

namespace {

std::wstring moduleDirectory() {
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) return {};
        if (length < buffer.size() - 1) {
            std::wstring path(buffer.data(), length);
            const auto separator = path.find_last_of(L"\\/");
            return separator == std::wstring::npos ? std::wstring() : path.substr(0, separator);
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::wstring quote(const std::wstring& value) {
    std::wstring result = L"\"";
    result += value;
    result += L"\"";
    return result;
}

std::wstring joinPath(const std::wstring& directory, const wchar_t* name) {
    return directory + L"\\" + name;
}

bool exists(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::wstring commandTail() {
    const wchar_t* raw = GetCommandLineW();
    if (raw == nullptr) return {};
    std::wstring command(raw);
    size_t index = 0;
    if (!command.empty() && command.front() == L'\"') {
        index = command.find(L'\"', 1);
        if (index == std::wstring::npos) return {};
        ++index;
    } else {
        index = command.find_first_of(L" \t");
    }
    if (index == std::wstring::npos) return {};
    while (index < command.size() && (command[index] == L' ' || command[index] == L'\t')) ++index;
    return index < command.size() ? L" " + command.substr(index) : std::wstring();
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    const std::wstring directory = moduleDirectory();
    const auto dotnet = joinPath(joinPath(directory, L"runtime"), L"dotnet.exe");
    const auto application = joinPath(directory, L"SonyDeviceCenter.dll");
    if (!exists(dotnet) || !exists(application)) {
        MessageBoxW(nullptr,
                    L"Sony Device Center 运行时文件不完整，请重新运行安装程序。",
                    L"Sony Device Center",
                    MB_ICONERROR | MB_OK);
        return ERROR_FILE_NOT_FOUND;
    }

    std::wstring command = quote(dotnet) + L" " + quote(application) + commandTail();
    std::vector<wchar_t> commandBuffer(command.begin(), command.end());
    commandBuffer.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const BOOL started = CreateProcessW(nullptr,
                                        commandBuffer.data(),
                                        nullptr,
                                        nullptr,
                                        FALSE,
                                        CREATE_NO_WINDOW,
                                        nullptr,
                                        directory.c_str(),
                                        &startup,
                                        &process);
    if (!started) {
        MessageBoxW(nullptr,
                    L"无法启动 .NET Desktop Runtime。请重新运行安装程序。",
                    L"Sony Device Center",
                    MB_ICONERROR | MB_OK);
        return GetLastError();
    }

    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<int>(exitCode);
}
