#include "core/Manager.h"
#include "platform/WindowsArguments.h"
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <deque>
#include <iostream>

using namespace helper;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F f, const char* message) {
    bool thrown = false; try { f(); } catch (const std::exception&) { thrown = true; }
    check(thrown, message);
}
struct ScriptedChannel : Channel {
    std::deque<std::string> lines;
    std::string* sent;
    ScriptedChannel(std::deque<std::string> l, std::string* s) : lines(std::move(l)), sent(s) {}
    void send(const std::string& text) override { *sent += text; }
    std::string line() override { if (lines.empty()) throw std::runtime_error("closed"); auto s = lines.front(); lines.pop_front(); return s; }
};
struct FakeHost : Host {
    std::deque<std::string> replies;
    std::string sent;
    ProcessResult run(const fs::path&, const std::vector<std::string>&, const fs::path&, int) override { throw std::runtime_error("unused"); }
    Runtime spawn(const fs::path&, const std::vector<std::string>&, const fs::path&, const fs::path&) override { throw std::runtime_error("unused"); }
    State processState(const Runtime&) override { return State::Unknown; }
    void terminate(const Runtime&) override {}
    std::unique_ptr<Lease> lock(const fs::path&) override { return std::make_unique<Lease>(); }
    bool diskAvailable(const fs::path&) override { return false; }
    void atomicWrite(const fs::path&, const std::string&) override {}
    int freePort() override { return 4444; }
    std::unique_ptr<Channel> connect(int) override { return std::make_unique<ScriptedChannel>(replies, &sent); }
};
int main() {
    try {
        const std::vector<std::wstring> cases{L"", L"plain", L"a b", L"C:\\中文 测试\\", L"a\"b", L"a\\\\\"b", L"&,=;$(whoami)", L"line\nbreak"};
        for (const auto& s : cases) {
            auto cmd = L"app.exe " + quoteWindowsArgument(s);
            int count = 0; auto args = CommandLineToArgvW(cmd.c_str(), &count);
            check(args && count == 2 && args[1] == s, "Windows quoting round-trip failed");
            LocalFree(args);
        }
        check(qemuEscape("C:/a,b/file.qcow2") == "C:/a,,b/file.qcow2", "QEMU comma escape failed");
        rejects([] { validateId("../escape"); }, "Path traversal ID accepted");
        Machine vm; vm.id = "0123456789abcdef0123456789abcdef"; vm.mac = "52:54:00:12:34:56";
        BootOptions b; b.iso = "C:/测试,a.iso"; b.bootCdOnce = true;
        auto args = launchArguments(vm, b, path("C:/磁盘,a.qcow2"), path("C:/firmware"), "XP [id]", 23456);
        check(std::find(args.begin(), args.end(), "file=C:/磁盘,,a.qcow2,format=qcow2,if=ide,index=0,id=osdisk") != args.end(), "Disk escaping failed");
        check(std::find(args.begin(), args.end(), "order=c,once=d,menu=on") != args.end(), "Boot-once missing");
        check(std::find(args.begin(), args.end(), "pcnet,id=nic0,netdev=net0,mac=52:54:00:12:34:56,addr=0x4,romfile=") != args.end(), "PXE not disabled");
        check(std::find(args.begin(), args.end(), "AC97,id=sound0,audiodev=snd0,addr=0x5") != args.end(), "Install boot lost AC97");
        vm.sound = false; vm.usb = false; vm.network = false;
        args = launchArguments(vm, b, "disk", "firmware", "name", 4444);
        check(std::find(args.begin(), args.end(), "-audiodev") == args.end(), "Disabled sound still present");
        check(std::find(args.begin(), args.end(), "piix3-usb-uhci,id=usb,addr=0x3") != args.end(), "PS/2 boot lost hot-plug controller");
        check(std::find(args.begin(), args.end(), "usb-tablet,id=tablet,bus=usb.0") == args.end(), "PS/2 boot contains a tablet");
        check(std::find(args.begin(), args.end(), "-nic") != args.end(), "Offline NIC missing");
        b.iso.clear(); rejects([&] { launchArguments(vm, b, "disk", "firmware", "name", 4444); }, "CD boot without ISO accepted");
        vm.memoryMiB = 64; rejects([&] { validate(vm); }, "Invalid memory accepted");
        FakeHost fake;
        fake.replies = {R"({"QMP":{}})", R"({"event":"STOP"})", R"({"return":{},"id":1})",
                        R"({"return":{"name":"wrong"},"id":90})", R"({"return":{"name":"XP"},"id":2})"};
        Qmp qmp(fake, 4444); qmp.verifyName("XP");
        check(fake.sent.find("qmp_capabilities") != std::string::npos, "QMP handshake not sent");
        fake.replies = {R"({"QMP":{}})", R"({"return":{},"id":1})", R"({"return":{"name":"other"},"id":2})"};
        Qmp other(fake, 4444); rejects([&] { other.verifyName("XP"); }, "Foreign QMP identity accepted");
        fake.replies = {R"({"QMP":{}})", R"({"return":{},"id":1})", R"({"error":{"desc":"locked"},"id":2})"};
        Qmp failure(fake, 4444); rejects([&] { failure.execute("eject"); }, "QMP error ignored");
        auto native = makeHost(); auto temp = fs::temp_directory_path() / path("helper-tests-" + newId()); fs::create_directory(temp);
        native->atomicWrite(temp / "test.json", "old"); native->atomicWrite(temp / "test.json", "new");
        check(readText(temp / "test.json") == "new", "Atomic replacement failed");
        { auto first = native->lock(temp / "lock"); rejects([&] { native->lock(temp / "lock"); }, "Concurrent operation lock not enforced"); }
        { auto next = native->lock(temp / "lock"); }
        Manager m(temp, *native);
        check(m.portable(temp / "machines" / "disk.qcow2") == "machines/disk.qcow2", "Portable path not relative");
        check(m.state("0123456789abcdef0123456789abcdef") == State::Unknown, "Missing disk treated as stopped");
        fs::create_directory(temp / "bin");
        native->atomicWrite(temp / "bin/qemu-system-x86_64w.exe", "test placeholder");
        native->atomicWrite(temp / "guest-tools.iso", "test placeholder");
        native->atomicWrite(temp / "bin/qemu-3dfx-tools.iso", "old placeholder");
        Settings legacy; legacy.qemu = "bin/qemu-system-i386w.exe"; legacy.toolsIso = "bin/qemu-3dfx-tools.iso";
        native->atomicWrite(temp / "workspace.json", Json{{"schema", 1}, {"settings", legacy}}.dump());
        check(m.settings().qemu == "bin/qemu-system-x86_64w.exe", "Old QEMU package path not upgraded");
        check(m.settings().toolsIso == "guest-tools.iso", "Old tools path not upgraded");
        check(m.resolve("qemu-3dfx-tools.iso") == temp / "guest-tools.iso", "Missing legacy mounted ISO not remapped");
        legacy.qemu = "custom/qemu.exe"; legacy.toolsIso = "custom/tools.iso";
        native->atomicWrite(temp / "workspace.json", Json{{"schema", 1}, {"settings", legacy}}.dump());
        check(m.settings().qemu == legacy.qemu && m.settings().toolsIso == legacy.toolsIso, "Custom paths changed during migration");
        fs::remove_all(temp);
        std::cout << "PASS: quoting, validation, portable paths, XP command, QMP identity/events/errors, atomic writes, operation locks\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
