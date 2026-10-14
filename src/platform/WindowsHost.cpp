#include "core/Host.h"
#include "WindowsArguments.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace helper {
namespace {
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h = INVALID_HANDLE_VALUE) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
std::runtime_error error(const std::string& what) {
    const auto code = GetLastError();
    return std::runtime_error(what + " (Windows error " + std::to_string(code) + ")");
}
std::wstring wide(const std::string& s) {
    if (s.find('\0') != std::string::npos) throw std::runtime_error("参数中不能包含 NUL");
    if (s.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (!size) throw error("无效的 UTF-8");
    std::wstring out(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), out.data(), size);
    return out;
}
uint64_t creationTime(HANDLE h) {
    FILETIME a{}, b{}, c{}, d{};
    if (!GetProcessTimes(h, &a, &b, &c, &d)) throw error("读取进程身份失败");
    return (static_cast<uint64_t>(a.dwHighDateTime) << 32) | a.dwLowDateTime;
}
State identity(HANDLE h, const Runtime& p) {
    DWORD code{};
    if (!GetExitCodeProcess(h, &code)) return State::Unknown;
    if (WaitForSingleObject(h, 0) == WAIT_OBJECT_0) return State::Stopped;
    if (creationTime(h) != p.started) return State::Stopped;
    std::wstring image(32768, L'\0'); DWORD length = static_cast<DWORD>(image.size());
    if (!QueryFullProcessImageNameW(h, 0, image.data(), &length)) return State::Unknown;
    image.resize(length);
    auto expected = fs::weakly_canonical(path(p.qemu)).wstring();
    if (CompareStringOrdinal(image.c_str(), -1, expected.c_str(), -1, TRUE) != CSTR_EQUAL) return State::Unknown;
    return State::Running;
}
struct FileLease : Lease {
    Handle handle;
    explicit FileLease(const fs::path& p) : handle(CreateFileW(p.c_str(), GENERIC_READ | GENERIC_WRITE,
        0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)) {
        if (handle.value == INVALID_HANDLE_VALUE) throw error("另一个操作正在使用该虚拟机或工作区");
    }
};
struct Winsock {
    Winsock() { WSADATA d{}; if (WSAStartup(MAKEWORD(2, 2), &d)) throw std::runtime_error("Winsock 初始化失败"); }
    ~Winsock() { WSACleanup(); }
};
void sockets() { static Winsock once; }
struct Socket {
    SOCKET value = INVALID_SOCKET;
    Socket() : value(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) {
        if (value == INVALID_SOCKET) throw std::runtime_error("创建 QMP socket 失败");
    }
    ~Socket() { if (value != INVALID_SOCKET) closesocket(value); }
};
struct TcpChannel : Channel {
    Socket socket_;
    std::string pending_;
    explicit TcpChannel(int port) {
        if (port < 1 || port > 65535) throw std::runtime_error("无效的 QMP 端口");
        sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(static_cast<u_short>(port));
        u_long mode = 1; ioctlsocket(socket_.value, FIONBIO, &mode);
        if (::connect(socket_.value, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
            if (WSAGetLastError() != WSAEWOULDBLOCK) throw std::runtime_error("无法连接 QMP");
            fd_set write{}, fail{}; FD_ZERO(&write); FD_ZERO(&fail); FD_SET(socket_.value, &write); FD_SET(socket_.value, &fail);
            timeval timeout{1, 0};
            if (select(0, nullptr, &write, &fail, &timeout) <= 0 || FD_ISSET(socket_.value, &fail))
                throw std::runtime_error("QMP 尚未就绪或连接超时");
            int err{}, len = sizeof(err);
            if (getsockopt(socket_.value, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len) || err)
                throw std::runtime_error("QMP 连接失败");
        }
        mode = 0; ioctlsocket(socket_.value, FIONBIO, &mode);
        DWORD timeout = 2500;
        setsockopt(socket_.value, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&timeout), sizeof(timeout));
        setsockopt(socket_.value, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char*>(&timeout), sizeof(timeout));
    }
    void send(const std::string& s) override {
        size_t pos = 0;
        while (pos < s.size()) {
            int n = ::send(socket_.value, s.data() + pos, static_cast<int>(s.size() - pos), 0);
            if (n <= 0) throw std::runtime_error("QMP 发送失败");
            pos += n;
        }
    }
    std::string line() override {
        for (;;) {
            auto end = pending_.find('\n');
            if (end != std::string::npos) {
                auto result = pending_.substr(0, end); pending_.erase(0, end + 1); return result;
            }
            if (pending_.size() > 4 * 1024 * 1024) throw std::runtime_error("QMP 消息过大");
            char buf[4096]; int n = recv(socket_.value, buf, sizeof(buf), 0);
            if (n <= 0) throw std::runtime_error("QMP 连接关闭或响应超时");
            pending_.append(buf, n);
        }
    }
};
Runtime startProcess(const fs::path& exe, const std::vector<std::string>& args, const fs::path& cwd,
    const fs::path& log, HANDLE* processHandle) {
    const auto fullExe = fs::weakly_canonical(exe);
    std::wstring command = quoteWindowsArgument(fullExe.wstring());
    for (const auto& arg : args) command += L" " + quoteWindowsArgument(wide(arg));
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    Handle output(CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (output.value == INVALID_HANDLE_VALUE) throw error("创建进程日志失败: " + utf8(log));
    Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr));
    if (input.value == INVALID_HANDLE_VALUE) throw error("打开 NUL 失败");
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    std::vector<char> storage(bytes);
    auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &bytes)) throw error("初始化进程属性失败");
    struct Cleanup { LPPROC_THREAD_ATTRIBUTE_LIST p; ~Cleanup() { DeleteProcThreadAttributeList(p); } } cleanup{attributes};
    HANDLE inherited[]{output.value, input.value};
    if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, sizeof(inherited), nullptr, nullptr))
        throw error("设置进程句柄失败");
    STARTUPINFOEXW si{}; si.StartupInfo.cb = sizeof(si); si.lpAttributeList = attributes;
    // CREATE_NO_WINDOW suppresses helper consoles. SW_HIDE would also hide
    // QEMU's first SDL window, so leave GUI window visibility to the child.
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput = input.value; si.StartupInfo.hStdOutput = output.value; si.StartupInfo.hStdError = output.value;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(fullExe.c_str(), command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, cwd.c_str(), &si.StartupInfo, &pi))
        throw error("无法启动 " + utf8(fullExe));
    Handle thread(pi.hThread);
    Runtime result; result.pid = pi.dwProcessId; result.qemu = utf8(fullExe);
    try { result.started = creationTime(pi.hProcess); }
    catch (...) { TerminateProcess(pi.hProcess, 1); CloseHandle(pi.hProcess); throw; }
    *processHandle = pi.hProcess;
    return result;
}
class WindowsHost : public Host {
public:
    ProcessResult run(const fs::path& exe, const std::vector<std::string>& args, const fs::path& cwd, int timeout) override {
        auto log = fs::temp_directory_path() / path("qemu-helper-" + newId() + ".log");
        struct Cleanup { fs::path p; ~Cleanup() { std::error_code ec; fs::remove(p, ec); } } cleanup{log};
        HANDLE raw{}; startProcess(exe, args, cwd, log, &raw); Handle process(raw);
        auto wait = WaitForSingleObject(raw, timeout ? static_cast<DWORD>(timeout) * 1000 : INFINITE);
        if (wait != WAIT_OBJECT_0) {
            TerminateProcess(raw, 1); WaitForSingleObject(raw, INFINITE);
            throw std::runtime_error("进程超时或等待失败: " + utf8(exe));
        }
        DWORD code{}; if (!GetExitCodeProcess(raw, &code)) throw error("读取退出码失败");
        return {code, readText(log)};
    }
    Runtime spawn(const fs::path& exe, const std::vector<std::string>& args, const fs::path& cwd, const fs::path& log) override {
        HANDLE raw{}; auto result = startProcess(exe, args, cwd, log, &raw); Handle process(raw); return result;
    }
    State processState(const Runtime& p) override {
        Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, p.pid));
        if (!process.value) return GetLastError() == ERROR_INVALID_PARAMETER ? State::Stopped : State::Unknown;
        try { return identity(process.value, p); } catch (...) { return State::Unknown; }
    }
    void terminate(const Runtime& p) override {
        Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE | PROCESS_TERMINATE, FALSE, p.pid));
        if (!process.value) throw error("无法打开进程");
        if (identity(process.value, p) != State::Running) throw std::runtime_error("进程身份已改变，拒绝终止");
        if (!TerminateProcess(process.value, 1)) throw error("终止进程失败");
        WaitForSingleObject(process.value, 5000);
    }
    std::unique_ptr<Lease> lock(const fs::path& p) override { return std::make_unique<FileLease>(p); }
    bool diskAvailable(const fs::path& p) override {
        Handle file(CreateFileW(p.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        return file.value != INVALID_HANDLE_VALUE;
    }
    void atomicWrite(const fs::path& p, const std::string& text) override {
        auto tmp = p; tmp += L"." + wide(newId()) + L".tmp";
        struct Cleanup { fs::path p; ~Cleanup() { std::error_code ec; fs::remove(p, ec); } } cleanup{tmp};
        {
            Handle file(CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
            if (file.value == INVALID_HANDLE_VALUE) throw error("无法写入配置: " + utf8(p));
            DWORD written{};
            if (text.size() > MAXDWORD || !WriteFile(file.value, text.data(), static_cast<DWORD>(text.size()), &written, nullptr)
                || written != text.size() || !FlushFileBuffers(file.value)) throw error("写入配置失败");
        }
        if (!MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw error("保存配置失败");
    }
    int freePort() override {
        sockets(); Socket s; sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(s.value, reinterpret_cast<sockaddr*>(&addr), sizeof(addr))) throw std::runtime_error("分配 QMP 端口失败");
        int length = sizeof(addr);
        if (getsockname(s.value, reinterpret_cast<sockaddr*>(&addr), &length)) throw std::runtime_error("读取 QMP 端口失败");
        return ntohs(addr.sin_port);
    }
    std::unique_ptr<Channel> connect(int port) override { sockets(); return std::make_unique<TcpChannel>(port); }
};
}
std::unique_ptr<Host> makeHost() { return std::make_unique<WindowsHost>(); }
}
