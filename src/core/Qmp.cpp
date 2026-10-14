#include "Qmp.h"
#include <stdexcept>
namespace helper {
Qmp::Qmp(Host& host, int port) : channel_(host.connect(port)) {
    const auto greeting = Json::parse(channel_->line());
    if (!greeting.contains("QMP")) throw std::runtime_error("QMP 握手无效");
    execute("qmp_capabilities");
}
Json Qmp::execute(const std::string& cmd, const Json& args) {
    const auto id = ++sequence_;
    channel_->send(Json{{"execute", cmd}, {"arguments", args}, {"id", id}}.dump() + "\r\n");
    for (int n = 0; n < 4096; ++n) {
        const auto reply = Json::parse(channel_->line());
        if (reply.contains("event")) continue;
        if (!reply.contains("id") || reply["id"] != id) continue;
        if (reply.contains("error")) throw std::runtime_error("QMP " + cmd + ": " + reply["error"].value("desc", reply["error"].dump()));
        if (reply.contains("return")) return reply["return"];
    }
    throw std::runtime_error("QMP 响应数量超出限制");
}
void Qmp::verifyName(const std::string& name) {
    if (execute("query-name").value("name", "") != name)
        throw std::runtime_error("QMP 身份不匹配，已停止操作");
}
}
