#pragma once
#include "Host.h"
#include "Qmp.h"
#include <functional>

namespace helper {
// Synchronous, UI-independent API. UI calls long operations on a worker thread.
// Mutation methods take an OS-backed workspace lease, including across processes.
class Manager {
public:
    Manager(fs::path root, Host& host);
    const fs::path& root() const { return root_; }
    Settings settings() const;
    void saveSettings(Settings settings);
    fs::path resolve(const std::string& value) const;
    std::string portable(const fs::path& value) const;
    std::vector<Check> diagnose();
    std::vector<Machine> machines(std::vector<std::string>* warnings = nullptr) const;
    Machine machine(const std::string& id) const;
    Machine create(Machine values, int sizeGiB, const fs::path& importDisk = {});
    void save(const Machine& vm);
    void removeFromList(const std::string& id);
    State state(const std::string& id);
    Runtime start(const std::string& id, BootOptions boot);
    void shutdown(const std::string& id, bool force = false);
    void changeIso(const std::string& id, const std::string& iso);
    void changeMouse(const std::string& id, bool usb);
    std::vector<Snapshot> snapshots(const std::string& id);
    void createSnapshot(const std::string& id, const std::string& name);
    std::string restoreSnapshot(const std::string& id, const std::string& snapshotId);
    void deleteSnapshot(const std::string& id, const std::string& snapshotId);
    void exportDisk(const std::string& id, const fs::path& destination);
    fs::path directory(const std::string& id) const;
    fs::path disk(const std::string& id) const;
private:
    fs::path root_;
    Host& host_;
    std::unique_ptr<Lease> lease();
    void requireStopped(const std::string& id);
    Runtime runtime(const std::string& id) const;
    void writeMachine(const Machine& vm);
    Json imageInfo(const fs::path& disk);
    ProcessResult image(const std::vector<std::string>& args);
    std::vector<Snapshot> snapshotList(const std::string& id);
    std::string requireSnapshot(const std::string& id, const std::string& snapshotId);
    void pruneMedia(const std::string& id, const std::string& keep);
    Qmp monitor(const std::string& id);
    std::string stageIso(const std::string& id, const fs::path& source);
};
}
