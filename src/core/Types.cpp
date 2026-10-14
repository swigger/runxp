#include "Types.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>

namespace helper {
fs::path path(const std::string& s) { return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size())); }
std::string utf8(const fs::path& p) {
    auto s = p.generic_u8string();
    return {reinterpret_cast<const char*>(s.data()), s.size()};
}
std::string readText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("无法读取文件: " + utf8(p));
    return {std::istreambuf_iterator<char>(f), {}};
}
Json readJson(const fs::path& p) {
    try { return Json::parse(readText(p)); }
    catch (const std::exception& e) { throw std::runtime_error("配置读取失败 " + utf8(p) + ": " + e.what()); }
}
std::string newId() {
    std::random_device rd;
    std::ostringstream s;
    for (int i = 0; i < 4; ++i) s << std::hex << std::setw(8) << std::setfill('0') << static_cast<uint32_t>(rd());
    return s.str();
}
std::string timestamp() {
    return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}
void validateId(const std::string& s) {
    if (s.size() != 32 || s.find_first_not_of("0123456789abcdef") != std::string::npos)
        throw std::runtime_error("无效的虚拟机 ID");
}
void validate(const Machine& vm) {
    validateId(vm.id);
    if (vm.name.empty() || vm.name.size() > 180 || vm.name.find_first_of("\r\n\0", 0, 3) != std::string::npos)
        throw std::runtime_error("请输入有效的虚拟机名称（最多 180 UTF-8 字节）");
    if (vm.memoryMiB < 128 || vm.memoryMiB > 3584) throw std::runtime_error("XP 内存范围为 128–3584 MiB");
    if (vm.cpus < 1 || vm.cpus > 4) throw std::runtime_error("处理器数量应为 1–4");
    if (vm.accelerator != "tcg" && vm.accelerator != "whpx") throw std::runtime_error("未知的加速器");
    if (vm.mac.size() != 17) throw std::runtime_error("MAC 地址无效");
    for (size_t i = 0; i < vm.mac.size(); ++i) {
        if (i % 3 == 2 ? vm.mac[i] != ':' : !std::isxdigit(static_cast<unsigned char>(vm.mac[i])))
            throw std::runtime_error("MAC 地址无效");
    }
}
std::string qemuEscape(const std::string& value) {
    if (value.find('\0') != std::string::npos) throw std::runtime_error("参数包含 NUL");
    std::string out;
    for (char c : value) { out += c; if (c == ',') out += ','; }
    return out;
}
std::vector<std::string> launchArguments(const Machine& vm, const BootOptions& b,
    const fs::path& disk, const fs::path& firmware, const std::string& monitorName, int port) {
    validate(vm);
    if (port < 1 || port > 65535) throw std::runtime_error("QMP 端口无效");
    if (b.bootCdOnce && b.iso.empty()) throw std::runtime_error("从光盘启动需要选择 ISO");
    std::vector<std::string> a{
        "-name", qemuEscape(monitorName), "-L", utf8(firmware),
        "-machine", "pc-i440fx-9.2,hpet=off", "-accel", vm.accelerator == "whpx" ? "whpx,kernel-irqchip=off" : "tcg,thread=multi",
        "-cpu", "Nehalem,-lm", "-smp", std::to_string(vm.cpus) + ",sockets=1,cores=" + std::to_string(vm.cpus) + ",threads=1",
        "-m", std::to_string(vm.memoryMiB), "-vga", "cirrus", "-display", b.headless ? "none" : "sdl",
        "-rtc", "base=localtime", "-monitor", "none", "-serial", "none",
        "-drive", "file=" + qemuEscape(utf8(disk)) + ",format=qcow2,if=ide,index=0,id=osdisk",
        "-drive", "if=none,media=cdrom,id=cdmedia",
        "-device", "ide-cd,drive=cdmedia,id=cdrom,bus=ide.1,unit=0",
        "-boot", b.bootCdOnce ? "order=c,once=d,menu=on" : "order=c,menu=on",
        "-qmp", "tcp:127.0.0.1:" + std::to_string(port) + ",server=on,wait=off", "-S"};
    auto add = [&](std::string key, std::string value) { a.push_back(std::move(key)); a.push_back(std::move(value)); };
    // Keep the controller present even when starting with PS/2 input, so the
    // tablet can be hot-plugged without changing the guest's PCI topology.
    add("-device", "piix3-usb-uhci,id=usb,addr=0x3");
    if (vm.usb) {
        add("-device", "usb-tablet,id=tablet,bus=usb.0");
    }
    if (vm.sound) {
        add("-audiodev", b.headless ? "none,id=snd0" : "sdl,id=snd0");
        add("-device", "AC97,id=sound0,audiodev=snd0,addr=0x5");
    }
    if (vm.network) {
        add("-netdev", "user,id=net0");
        add("-device", "pcnet,id=nic0,netdev=net0,mac=" + vm.mac + ",addr=0x4,romfile=");
    } else add("-nic", "none");
    return a;
}
}
