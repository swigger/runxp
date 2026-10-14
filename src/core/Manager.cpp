#include "Manager.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <thread>

namespace helper {
namespace {
void requireFile(const fs::path& p, const std::string& label) {
    if (!fs::is_regular_file(p)) throw std::runtime_error(label + " 不存在: " + utf8(p));
}
Json document(const fs::path& p) {
    auto j = readJson(p);
    if (j.value("schema", 0) != 1) throw std::runtime_error("不支持的配置版本: " + utf8(p));
    return j;
}
}
Manager::Manager(fs::path root, Host& host) : root_(fs::weakly_canonical(fs::absolute(root))), host_(host) {
    fs::create_directories(root_);
    fs::create_directories(root_ / "machines");
}
std::unique_ptr<Lease> Manager::lease() { return host_.lock(root_ / "machines" / ".workspace.lock"); }
Settings Manager::settings() const {
    auto file = root_ / "workspace.json";
    auto s = fs::exists(file) ? document(file).at("settings").get<Settings>() : Settings{};
    // Migrate only known old package paths; leave custom locations alone.
    const auto oldQemu = root_ / "bin/qemu-system-i386w.exe";
    if (resolve(s.qemu) == oldQemu && fs::is_regular_file(root_ / Settings{}.qemu)) s.qemu = Settings{}.qemu;
    if (!s.toolsIso.empty()) {
        auto tools = resolve(s.toolsIso);
        if ((tools == root_ / "qemu-3dfx-tools.iso" || tools == root_ / "bin/qemu-3dfx-tools.iso")
            && fs::is_regular_file(root_ / "guest-tools.iso")) tools = root_ / "guest-tools.iso";
        s.toolsIso = portable(tools);
    }
    return s;
}
fs::path Manager::resolve(const std::string& s) const {
    if (s.empty()) return {};
    auto p = path(s);
    auto full = fs::weakly_canonical(p.is_absolute() ? p : root_ / p);
    // Also migrate an old tools-disc path stored in an individual VM profile.
    if (!fs::exists(full) && (full == root_ / "qemu-3dfx-tools.iso" || full == root_ / "bin/qemu-3dfx-tools.iso")
        && fs::is_regular_file(root_ / "guest-tools.iso")) return root_ / "guest-tools.iso";
    return full;
}
std::string Manager::portable(const fs::path& p) const {
    if (p.empty()) return {};
    auto full = fs::weakly_canonical(fs::absolute(p));
    auto relative = full.lexically_relative(root_);
    if (!relative.empty() && *relative.begin() != "..") return utf8(relative);
    return utf8(full);
}
void Manager::saveSettings(Settings s) {
    auto guard = lease();
    for (const auto& vm : machines()) if (state(vm.id) != State::Stopped)
        throw std::runtime_error("请先关闭所有虚拟机，再修改运行环境");
    if (s.qemu.empty() || s.imageTool.empty() || s.firmware.empty()) throw std::runtime_error("请填写 QEMU、qemu-img 和固件路径");
    s.qemu = portable(resolve(s.qemu)); s.imageTool = portable(resolve(s.imageTool));
    s.firmware = portable(resolve(s.firmware)); s.toolsIso = portable(resolve(s.toolsIso));
    host_.atomicWrite(root_ / "workspace.json", Json{{"schema", 1}, {"settings", s}}.dump(2) + "\n");
}
std::vector<Check> Manager::diagnose() {
    auto guard = lease();
    auto s = settings(); std::vector<Check> out;
    auto executable = [&](const std::string& name, const std::string& value) {
        try {
            auto p = resolve(value); requireFile(p, name);
            auto result = host_.run(p, {"--version"}, root_, 10);
            out.push_back({result.exitCode == 0, name, utf8(p) + "\n" + result.output +
                (result.exitCode ? "\n退出码: " + std::to_string(result.exitCode) + "；请检查相邻 DLL。" : "")});
        } catch (const std::exception& e) { out.push_back({false, name, e.what()}); }
    };
    executable("QEMU 3dfx", s.qemu); executable("qemu-img", s.imageTool);
    for (auto name : {"bios-256k.bin", "vgabios-cirrus.bin", "kvmvapic.bin"}) {
        auto p = resolve(s.firmware) / name;
        out.push_back({fs::is_regular_file(p), name, utf8(p)});
    }
    auto qemuDir = resolve(s.qemu).parent_path();
    for (auto name : {"glide2x.dll", "glide3x.dll", "OpenGLid.ini"}) {
        auto p = qemuDir / name; out.push_back({fs::is_regular_file(p), name, utf8(p), false});
    }
    out.push_back({!s.toolsIso.empty() && fs::is_regular_file(resolve(s.toolsIso)), "客体工具 ISO", s.toolsIso, false});
    try {
        auto result = host_.run(resolve(s.qemu), {"-machine", "help"}, root_, 10);
        out.push_back({result.exitCode == 0 && result.output.find("pc-i440fx-9.2") != std::string::npos,
            "XP 硬件预设", "需要 pc-i440fx-9.2 / Cirrus / IDE / AC97 / PCnet。默认 TCG，无需启用 Hyper-V。"});
    } catch (const std::exception& e) { out.push_back({false, "XP 硬件预设", e.what()}); }
    return out;
}
fs::path Manager::directory(const std::string& id) const { validateId(id); return root_ / "machines" / path(id); }
fs::path Manager::disk(const std::string& id) const { return directory(id) / "disk.qcow2"; }
Machine Manager::machine(const std::string& id) const {
    auto vm = document(directory(id) / "machine.json").at("machine").get<Machine>();
    validate(vm);
    if (vm.id != id) throw std::runtime_error("虚拟机目录与配置 ID 不一致");
    return vm;
}
std::vector<Machine> Manager::machines(std::vector<std::string>* warnings) const {
    std::vector<Machine> out;
    for (const auto& entry : fs::directory_iterator(root_ / "machines")) {
        if (!entry.is_directory() || !fs::exists(entry.path() / "machine.json")) continue;
        try { out.push_back(machine(utf8(entry.path().filename()))); }
        catch (const std::exception& e) { if (warnings) warnings->push_back(e.what()); else throw; }
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    return out;
}
Runtime Manager::runtime(const std::string& id) const { return readJson(directory(id) / "runtime.json").get<Runtime>(); }
State Manager::state(const std::string& id) {
    auto file = directory(id) / "runtime.json";
    if (fs::exists(file)) {
        try {
            auto current = host_.processState(runtime(id));
            if (current != State::Stopped) return current;
        } catch (...) { return State::Unknown; }
    }
    if (!fs::is_regular_file(disk(id))) return State::Unknown;
    return host_.diskAvailable(disk(id)) ? State::Stopped : State::Unknown;
}
void Manager::requireStopped(const std::string& id) {
    machine(id);
    if (state(id) != State::Stopped) throw std::runtime_error("虚拟机正在运行、磁盘被占用或状态无法确认；请先关闭虚拟机");
}
void Manager::writeMachine(const Machine& vm) {
    host_.atomicWrite(directory(vm.id) / "machine.json", Json{{"schema", 1}, {"machine", vm}}.dump(2) + "\n");
}
ProcessResult Manager::image(const std::vector<std::string>& args) {
    auto exe = resolve(settings().imageTool); requireFile(exe, "qemu-img");
    auto nativeArgs = args;
    for (auto& arg : nativeArgs) {
        const auto p = path(arg);
        if (!p.is_absolute()) continue;
        const auto relative = p.lexically_relative(root_);
        const auto value = utf8(relative);
        if (!value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char c) { return c < 128; }))
            arg = value;
    }
    auto result = host_.run(exe, nativeArgs, root_); // Never kill an image mutation on a timer.
    if (result.exitCode) throw std::runtime_error("qemu-img 失败 (" + std::to_string(result.exitCode) + "): " + result.output);
    return result;
}
Json Manager::imageInfo(const fs::path& p) {
    return Json::parse(image({"info", "--output=json", utf8(p)}).output);
}
Machine Manager::create(Machine vm, int sizeGiB, const fs::path& source) {
    auto guard = lease();
    vm.id = newId(); vm.mac = "52:54:00:" + vm.id.substr(0, 2) + ":" + vm.id.substr(2, 2) + ":" + vm.id.substr(4, 2);
    vm.iso = portable(resolve(vm.iso)); validate(vm);
    if (sizeGiB < 1 || sizeGiB > 2048) throw std::runtime_error("磁盘容量应为 1–2048 GiB");
    fs::path import;
    if (!source.empty()) {
        import = fs::weakly_canonical(fs::absolute(source)); requireFile(import, "导入磁盘");
        if (!host_.diskAvailable(import)) throw std::runtime_error("导入源磁盘正在使用或不可写，请先关闭使用它的程序");
    }
    auto dir = directory(vm.id);
    if (!fs::create_directory(dir)) throw std::runtime_error("无法创建虚拟机目录");
    // Publish the profile only after qemu-img finishes. Failed imports remain
    // identifiable .partial files and never overwrite the source.
    auto partial = dir / "disk.partial.qcow2";
    try {
        if (import.empty()) image({"create", "-f", "qcow2", "-o", "compat=1.1", utf8(partial), std::to_string(sizeGiB) + "G"});
        else {
            auto info = imageInfo(import);
            const auto fmt = info.at("format").get<std::string>();
            if (fmt != "qcow2" && fmt != "raw" && fmt != "vmdk" && fmt != "vdi" && fmt != "vpc" && fmt != "vhdx")
                throw std::runtime_error("不支持的导入格式: " + fmt);
            image({"convert", "-f", fmt, "-O", "qcow2", "-o", "compat=1.1", utf8(import), utf8(partial)});
        }
        fs::rename(partial, disk(vm.id)); writeMachine(vm);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string(e.what()) + "\n未完成的数据保留在: " + utf8(dir));
    }
    return vm;
}
void Manager::save(const Machine& vm) {
    auto guard = lease(); requireStopped(vm.id); validate(vm);
    auto copy = vm; copy.iso = portable(resolve(vm.iso)); writeMachine(copy);
}
void Manager::removeFromList(const std::string& id) {
    auto guard = lease(); requireStopped(id);
    fs::rename(directory(id) / "machine.json", directory(id) / path("machine.removed-" + timestamp() + ".json"));
}
Runtime Manager::start(const std::string& id, BootOptions boot) {
    auto guard = lease(); requireStopped(id); const auto vm = machine(id); const auto s = settings();
    requireFile(resolve(s.qemu), "QEMU");
    for (auto file : {"bios-256k.bin", "vgabios-cirrus.bin", "kvmvapic.bin"}) requireFile(resolve(s.firmware) / file, "固件");
    if (!boot.iso.empty()) { boot.iso = utf8(resolve(boot.iso)); requireFile(path(boot.iso), "ISO"); }
    // This application only owns stand-alone qcow2 images, never backing chains.
    auto info = imageInfo(disk(id));
    if (info.value("format", "") != "qcow2" || info.contains("backing-filename"))
        throw std::runtime_error("需要独立的 qcow2 磁盘；请通过导入功能转换磁盘");
    const auto port = host_.freePort();
    // Keep compatibility with MinGW builds using the host code page for argv.
    // Keep QMP identity ASCII; the GUI carries the localized display name.
    const auto name = "WinHelper-" + id;
    // Keep all QEMU command-line paths ASCII and relative. This particular -w.exe
    // build does not consistently convert Windows Unicode argv to UTF-8.
    // Firmware is tiny; copy the preset's 3 files into the VM working directory.
    auto firmwareCache = directory(id) / "firmware";
    fs::create_directories(firmwareCache);
    for (auto file : {"bios-256k.bin", "vgabios-cirrus.bin", "kvmvapic.bin"})
        fs::copy_file(resolve(s.firmware) / file, firmwareCache / file, fs::copy_options::overwrite_existing);
    // OpenGlide looks for its INI in the process working directory. Seed it
    // from the published defaults, preserving any VM-specific adjustments.
    const auto glideConfig = resolve(s.qemu).parent_path() / "OpenGLid.ini";
    if (fs::is_regular_file(glideConfig))
        fs::copy_file(glideConfig, directory(id) / "OpenGLid.ini", fs::copy_options::skip_existing);
    const auto media = boot.iso.empty() ? std::string{} : stageIso(id, path(boot.iso));
    const auto args = launchArguments(vm, boot, "disk.qcow2", "firmware", name, port);
    const auto logDirectory = directory(id) / "logs";
    fs::create_directories(logDirectory);
    const auto log = logDirectory / path(timestamp() + "-" + newId().substr(0, 6) + ".log");
    auto process = host_.spawn(resolve(s.qemu), args, directory(id), log);
    process.port = port; process.monitorName = name;
    try { host_.atomicWrite(directory(id) / "runtime.json", Json(process).dump(2) + "\n"); }
    catch (...) { host_.terminate(process); throw; }
    std::string last; std::unique_ptr<Qmp> ready;
    for (int attempt = 0; attempt < 40; ++attempt) {
        if (host_.processState(process) == State::Stopped)
            throw std::runtime_error("QEMU 已退出。日志: " + utf8(log) + "\n" + readText(log));
        try { ready = std::make_unique<Qmp>(host_, port); ready->verifyName(name); break; }
        catch (const std::exception& e) { last = e.what(); }
        ready.reset();
        std::this_thread::sleep_for(std::chrono::milliseconds(125));
    }
    if (!ready) throw std::runtime_error("QEMU 进程已启动，但监控连接未就绪: " + last + "\n请查看日志: " + utf8(log));
    try {
        if (!media.empty()) ready->execute("blockdev-change-medium", {{"id", "cdrom"}, {"filename", media}, {"format", "raw"}, {"read-only-mode", "read-only"}});
        ready->execute("cont");
    } catch (...) { try { ready->execute("quit"); } catch (...) {} throw; }
    auto updated = vm; updated.iso = portable(resolve(boot.iso)); writeMachine(updated);
    pruneMedia(id, media);
    return process;
}
std::string Manager::stageIso(const std::string& id, const fs::path& source) {
    auto folder = directory(id) / "media"; fs::create_directories(folder);
    auto name = "iso-" + newId() + ".iso"; auto target = folder / name;
    std::error_code ec;
    fs::create_hard_link(source, target, ec);
    if (ec) {
        // Cross-volume or non-NTFS media: bounded by the image size, no reparse
        // points or elevation needed. Publish only a complete copy.
        auto partial = folder / (name + ".partial");
        try { fs::copy_file(source, partial); fs::rename(partial, target); }
        catch (...) { fs::remove(partial, ec); throw; }
    }
    return "media/" + name;
}
void Manager::pruneMedia(const std::string& id, const std::string& keep) {
    auto folder = directory(id) / "media";
    if (!fs::is_directory(folder)) return;
    for (const auto& entry : fs::directory_iterator(folder)) {
        auto name = utf8(entry.path().filename());
        if (name.size() != 40 || !name.starts_with("iso-") || !name.ends_with(".iso") || "media/" + name == keep) continue;
        try { validateId(name.substr(4, 32)); } catch (...) { continue; }
        // Only our cache entries; a still-open image is retained until next time.
        std::error_code ec; fs::remove(entry.path(), ec);
    }
}
Qmp Manager::monitor(const std::string& id) {
    auto r = runtime(id);
    if (host_.processState(r) != State::Running) throw std::runtime_error("没有可确认身份的运行实例");
    Qmp qmp(host_, r.port); qmp.verifyName(r.monitorName); return qmp;
}
void Manager::shutdown(const std::string& id, bool force) {
    auto guard = lease();
    auto qmp = monitor(id);
    qmp.execute(force ? "quit" : "system_powerdown");
}
void Manager::changeMouse(const std::string& id, bool usb) {
    auto guard = lease(); auto vm = machine(id);
    const auto current = state(id);
    if (current == State::Unknown) throw std::runtime_error("虚拟机状态无法确认，无法切换鼠标");
    if (current == State::Running) {
        auto qmp = monitor(id);
        auto present = [&](const std::string& name) {
            const auto devices = qmp.execute("qom-list", {{"path", "/machine/peripheral"}});
            return std::any_of(devices.begin(), devices.end(), [&](const auto& d) { return d.value("name", "") == name; });
        };
        if (present("tablet") != usb) {
            if (usb) {
                if (!present("usb")) throw std::runtime_error("当前虚拟机缺少 USB 控制器，请关机并重新启动后再切换。");
                qmp.execute("device_add", {{"driver", "usb-tablet"}, {"id", "tablet"}, {"bus", "usb.0"}});
            } else qmp.execute("device_del", {{"id", "tablet"}});
            // device_del acknowledges a request, not necessarily completion.
            // Confirm the QOM device actually disappears before persisting or
            // allowing its ID to be reused by the next switch.
            bool complete = false;
            for (int attempt = 0; attempt < 50; ++attempt) {
                if (present("tablet") == usb) { complete = true; break; }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            if (!complete) throw std::runtime_error("鼠标切换尚未完成，请稍后重试。");
        }
    }
    vm.usb = usb; writeMachine(vm);
}
void Manager::changeIso(const std::string& id, const std::string& iso) {
    auto guard = lease(); auto vm = machine(id); const auto p = resolve(iso);
    if (!iso.empty()) requireFile(p, "ISO");
    const auto current = state(id);
    if (current == State::Unknown) throw std::runtime_error("虚拟机状态无法确认");
    if (current == State::Running) {
        auto qmp = monitor(id);
        if (iso.empty()) qmp.execute("eject", {{"id", "cdrom"}, {"force", false}});
        else {
            const auto staged = stageIso(id, p);
            qmp.execute("blockdev-change-medium", {{"id", "cdrom"}, {"filename", staged}, {"format", "raw"}, {"read-only-mode", "read-only"}});
            pruneMedia(id, staged);
        }
    }
    if (iso.empty() || current == State::Stopped) pruneMedia(id, "");
    vm.iso = portable(p); writeMachine(vm);
}
std::vector<Snapshot> Manager::snapshotList(const std::string& id) {
    const auto info = imageInfo(disk(id));
    if (info.value("format", "") != "qcow2" || info.contains("backing-filename"))
        throw std::runtime_error("此功能只支持独立 qcow2 磁盘");
    auto labelsFile = directory(id) / "snapshot-labels.json";
    auto labels = fs::exists(labelsFile) ? readJson(labelsFile) : Json::object();
    std::vector<Snapshot> out;
    for (const auto& s : info.value("snapshots", Json::array())) {
        auto token = s.value("name", "");
        out.push_back({s.at("id").get<std::string>(), labels.value(token, token), s.value("date-sec", int64_t{}), s.value("vm-state-size", uint64_t{}), token});
    }
    return out;
}
std::vector<Snapshot> Manager::snapshots(const std::string& id) {
    auto guard = lease(); requireStopped(id); return snapshotList(id);
}
void Manager::createSnapshot(const std::string& id, const std::string& name) {
    auto guard = lease(); requireStopped(id);
    if (name.empty() || name.size() > 160 || name.find_first_of("\r\n\0", 0, 3) != std::string::npos ||
        name.find_first_not_of("0123456789") == std::string::npos)
        throw std::runtime_error("请输入快照名称，不能全为数字或包含换行，最多 160 UTF-8 字节");
    for (const auto& s : snapshotList(id)) if (s.name == name || s.id == name) throw std::runtime_error("快照名称已存在");
    auto token = "snapshot-" + newId();
    auto file = directory(id) / "snapshot-labels.json";
    auto labels = fs::exists(file) ? readJson(file) : Json::object();
    labels[token] = name;
    // Commit the label first; a failed image operation only leaves harmless metadata.
    host_.atomicWrite(file, labels.dump(2) + "\n");
    image({"snapshot", "-c", token, utf8(disk(id))});
}
std::string Manager::requireSnapshot(const std::string& id, const std::string& snapshotId) {
    const auto list = snapshotList(id);
    auto found = std::find_if(list.begin(), list.end(), [&](const auto& s) { return s.id == snapshotId; });
    if (found == list.end()) throw std::runtime_error("快照已不存在，请刷新列表");
    if (found->vmStateBytes) throw std::runtime_error("不能通过此工具操作带运行内存状态的快照");
    return found->token;
}
std::string Manager::restoreSnapshot(const std::string& id, const std::string& snapshotId) {
    auto guard = lease(); requireStopped(id); const auto token = requireSnapshot(id, snapshotId);
    auto backup = "before-restore-" + timestamp() + "-" + newId().substr(0, 6);
    image({"snapshot", "-c", backup, utf8(disk(id))});
    image({"snapshot", "-a", token, utf8(disk(id))});
    return backup;
}
void Manager::deleteSnapshot(const std::string& id, const std::string& snapshotId) {
    auto guard = lease(); requireStopped(id); const auto token = requireSnapshot(id, snapshotId);
    image({"snapshot", "-d", token, utf8(disk(id))});
}
void Manager::exportDisk(const std::string& id, const fs::path& destination) {
    auto guard = lease(); requireStopped(id);
    auto target = fs::absolute(destination);
    if (fs::exists(target)) throw std::runtime_error("备份目标已存在，不会覆盖");
    auto partial = target.parent_path() / path(utf8(target.filename()) + ".partial-" + newId());
    image({"convert", "-f", "qcow2", "-O", "qcow2", utf8(disk(id)), utf8(partial)});
    // rename fails if destination appeared concurrently; never overwrite it.
    fs::rename(partial, target);
}
}
