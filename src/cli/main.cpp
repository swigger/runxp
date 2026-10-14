#include "core/Manager.h"
#include <iostream>
#include <stdexcept>

using namespace helper;
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc < 3) {
            std::cout << "qemu-helper-cli <workspace> <command> [args]\n"
                "diagnose | list | create <name> <GiB> [iso] | import <name> <disk>\n"
                "status <id> | start <id> [--headless] [--install] [--iso <iso>]\n"
                "shutdown <id> | stop <id> | iso <id> [iso] | save <config.json>\n"
                "mouse <id> <usb|ps2>\n"
                "snapshots <id> | snapshot-create <id> <name> | snapshot-restore <id> <snapshot-id>\n"
                "snapshot-delete <id> <snapshot-id> | export <id> <destination> | remove <id>\n";
            return 2;
        }
        auto arg = [&](int n) { if (n >= argc) throw std::runtime_error("缺少参数"); return utf8(fs::path(argv[n])); };
        auto host = makeHost(); Manager manager(fs::path(argv[1]), *host);
        auto cmd = arg(2); Json result;
        if (cmd == "diagnose") {
            result = Json::array(); bool ok = true;
            for (const auto& c : manager.diagnose()) {
                result.push_back({{"ok", c.ok}, {"title", c.title}, {"detail", c.detail}, {"required", c.required}});
                if (!c.ok && c.required) ok = false;
            }
            std::cout << result.dump(2) << std::endl; return ok ? 0 : 1;
        } else if (cmd == "list") {
            std::vector<std::string> warnings; result = {{"machines", manager.machines(&warnings)}, {"warnings", warnings}};
        } else if (cmd == "create" || cmd == "import") {
            Machine vm; vm.name = arg(3);
            if (cmd == "create") { if (argc > 5) vm.iso = arg(5); result = manager.create(vm, std::stoi(arg(4))); }
            else result = manager.create(vm, 32, path(arg(4)));
        } else if (cmd == "start") {
            auto id = arg(3); auto vm = manager.machine(id); BootOptions boot; boot.iso = vm.iso;
            for (int i = 4; i < argc; ++i) {
                auto option = arg(i);
                if (option == "--headless") boot.headless = true;
                else if (option == "--install") boot.bootCdOnce = true;
                else if (option == "--iso") boot.iso = arg(++i);
                else throw std::runtime_error("未知启动选项: " + option);
            }
            result = manager.start(id, boot);
        } else if (cmd == "status") {
            const auto state = manager.state(arg(3));
            result = {{"state", state == State::Running ? "running" : state == State::Stopped ? "stopped" : "unknown"}};
        } else if (cmd == "shutdown" || cmd == "stop") manager.shutdown(arg(3), cmd == "stop");
        else if (cmd == "iso") manager.changeIso(arg(3), argc > 4 ? arg(4) : "");
        else if (cmd == "mouse") {
            const auto mode = arg(4);
            if (mode != "usb" && mode != "ps2") throw std::runtime_error("鼠标类型应为 usb 或 ps2");
            manager.changeMouse(arg(3), mode == "usb");
        }
        else if (cmd == "save") manager.save(readJson(path(arg(3))).get<Machine>());
        else if (cmd == "snapshots") {
            result = Json::array(); for (const auto& s : manager.snapshots(arg(3)))
                result.push_back({{"id", s.id}, {"name", s.name}, {"date", s.date}, {"vmStateBytes", s.vmStateBytes}});
        } else if (cmd == "snapshot-create") manager.createSnapshot(arg(3), arg(4));
        else if (cmd == "snapshot-restore") result = {{"safetySnapshot", manager.restoreSnapshot(arg(3), arg(4))}};
        else if (cmd == "snapshot-delete") manager.deleteSnapshot(arg(3), arg(4));
        else if (cmd == "export") manager.exportDisk(arg(3), path(arg(4)));
        else if (cmd == "remove") manager.removeFromList(arg(3));
        else throw std::runtime_error("未知命令: " + cmd);
        std::cout << (result.is_null() ? Json{{"ok", true}} : result).dump(2) << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << Json{{"error", e.what()}}.dump(2, ' ', false, Json::error_handler_t::replace) << std::endl;
        return 1;
    }
}
