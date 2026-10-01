#pragma once

#include <memory>
#include "server_context.hpp"
#include "src/thirdparty/httplib/httplib.h"

namespace vinox::server {

class HttpServer {
public:
    explicit HttpServer(ServerContext& ctx);
    ~HttpServer();

    bool start();
    void stop();

    bool is_running() const;

private:
    void setup_middleware();
    void setup_routes();

    ServerContext& ctx_;
    std::unique_ptr<httplib::Server> server_;
    std::atomic<bool> is_running_{false};
};

} // namespace vinox::server
