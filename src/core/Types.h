#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace helper {
namespace fs = std::filesystem;
using Json = nlohmann::json;
// All strings crossing the core API boundary are UTF-8, including paths.
fs::path path(const std::string& utf8);
std::string utf8(const fs::path& value);
std::string readText(const fs::path& value);
Json readJson(const fs::path& value);
std::string newId();
std::string timestamp();
void validateId(const std::string& value);

struct Settings {
    std::string qemu = "bin/qemu-system-x86_64w.exe";
    std::string imageTool = "bin/qemu-img.exe";
    std::string firmware = "bin/share";
    std::string toolsIso = "guest-tools.iso";
};
struct Machine {
    std::string id, name = "Windows XP", iso;
    int memoryMiB = 1024, cpus = 2;
    std::string accelerator = "tcg";
    bool network = true, sound = true, usb = true;
    std::string mac;
};
struct BootOptions {
    std::string iso;
    bool bootCdOnce = false;
    bool headless = false; // CLI/integration diagnostics only.
};
struct Snapshot {
    std::string id, name;
    int64_t date = 0;
    uint64_t vmStateBytes = 0;
    std::string token;
};
enum class State { Stopped, Running, Unknown };
struct Runtime {
    uint32_t pid = 0;
    uint64_t started = 0;
    int port = 0;
    std::string qemu, monitorName;
};
struct Check { bool ok; std::string title, detail; bool required = true; };
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Settings, qemu, imageTool, firmware, toolsIso)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Machine, id, name, iso, memoryMiB, cpus, accelerator, network, sound, usb, mac)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Runtime, pid, started, port, qemu, monitorName)
void validate(const Machine& vm);
std::string qemuEscape(const std::string& value);
std::vector<std::string> launchArguments(const Machine& vm, const BootOptions& boot,
    const fs::path& disk, const fs::path& firmware, const std::string& monitorName, int port);
}
