#include <windows.h>
#include <shellapi.h>
#include <filesystem>
#include <string>
#include "platform/WindowsArguments.h"

// This statically linked Win32 entry point has only Windows system imports.
// The WinUI process keeps its supported app-local runtime layout in bin/winui.
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    try {
        std::wstring path(32768, L'\0');
        const auto size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (!size || size >= path.size()) throw std::runtime_error("module path");
        path.resize(size);
        const auto root = std::filesystem::path(path).parent_path();
        const auto app = root / L"bin" / L"winui" / L"qemu-3dfx-winhelper-ui.exe";
        std::wstring command = helper::quoteWindowsArgument(app.wstring()) + L" --workspace "
            + helper::quoteWindowsArgument(root.wstring());
        int count{};
        auto args = CommandLineToArgvW(GetCommandLineW(), &count);
        if (!args) throw std::runtime_error("arguments");
        struct Args { wchar_t** p; ~Args() { LocalFree(p); } } owned{args};
        for (int i = 1; i < count; ++i) {
            const std::wstring argument = args[i];
            command += L" " + helper::quoteWindowsArgument(argument);
            if (argument == L"--workspace") {
                if (++i >= count) throw std::runtime_error("missing workspace");
                // Resolve caller-relative arguments before changing the child CWD.
                command += L" " + helper::quoteWindowsArgument(std::filesystem::absolute(args[i]).wstring());
            }
        }
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(app.c_str(), command.data(), nullptr, nullptr, FALSE, 0,
            nullptr, root.c_str(), &startup, &process)) {
            const auto message = L"无法打开 WinHelper。请先完整解压压缩包，保留 bin 文件夹，再双击主程序。\n\nWindows 错误码："
                + std::to_wstring(GetLastError());
            MessageBoxW(nullptr, message.c_str(), L"QEMU 3dfx · WinHelper", MB_OK | MB_ICONERROR);
            return 1;
        }
        CloseHandle(process.hThread); CloseHandle(process.hProcess);
        return 0;
    } catch (...) {
        MessageBoxW(nullptr, L"无法读取启动路径。请完整解压后再打开程序，并检查工作区路径。",
            L"QEMU 3dfx · WinHelper", MB_OK | MB_ICONERROR);
        return 1;
    }
}
