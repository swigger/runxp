#pragma once
#include "Types.h"
#include <memory>

namespace helper {
struct ProcessResult { int64_t exitCode; std::string output; };
struct Lease { virtual ~Lease() = default; };
struct Channel {
    virtual ~Channel() = default;
    virtual void send(const std::string& bytes) = 0;
    virtual std::string line() = 0;
};
// Only this boundary knows about native process/handle/socket APIs.
struct Host {
    virtual ~Host() = default;
    virtual ProcessResult run(const fs::path& exe, const std::vector<std::string>& args,
        const fs::path& cwd, int timeoutSeconds = 0) = 0;
    virtual Runtime spawn(const fs::path& exe, const std::vector<std::string>& args,
        const fs::path& cwd, const fs::path& log) = 0;
    virtual State processState(const Runtime& process) = 0;
    virtual void terminate(const Runtime& process) = 0;
    virtual std::unique_ptr<Lease> lock(const fs::path& file) = 0;
    virtual bool diskAvailable(const fs::path& disk) = 0;
    virtual void atomicWrite(const fs::path& file, const std::string& text) = 0;
    virtual int freePort() = 0;
    virtual std::unique_ptr<Channel> connect(int port) = 0;
};
std::unique_ptr<Host> makeHost();
}
