#pragma once
#include "Host.h"
namespace helper {
class Qmp {
public:
    Qmp(Host& host, int port);
    Json execute(const std::string& command, const Json& arguments = Json::object());
    void verifyName(const std::string& name);
private:
    std::unique_ptr<Channel> channel_;
    uint64_t sequence_ = 0;
};
}
