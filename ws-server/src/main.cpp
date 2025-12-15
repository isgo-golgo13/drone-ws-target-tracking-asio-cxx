#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>

#include <boost/asio.hpp>
#include <fmt/core.h>

#include "ws_server.hpp"
#include "svc_addr_config.hpp"

namespace {

boost::asio::io_context* g_ioc = nullptr;

void signal_handler(int sig) {
    fmt::print("\n[MAIN] Received signal {}, shutting down...\n", sig);
    if (g_ioc) {
        g_ioc->stop();
    }
}

}  // namespace

int main() {
    try {
        // Configuration
        auto cfg = svckit::AddrConfig::from_env_defaults("0.0.0.0", 8443);
        
        fmt::print("[MAIN] Starting WebSocket server\n");
        fmt::print("[MAIN] URL: {}\n", cfg.ws_url());
        fmt::print("[MAIN] Cert: {}\n", cfg.tls().cert_file.string());
        
        // IO context
        boost::asio::io_context ioc{1};
        g_ioc = &ioc;
        
        // Signal handling
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);
        
        // Create and run server using factory method
        auto server = ws::WSServer::create(ioc, cfg);
        server->run();
        
        // Run event loop
        ioc.run();
        
        // Cleanup
        server->stop();
        
        fmt::print("[MAIN] Server shutdown complete\n");
        return EXIT_SUCCESS;
        
    } catch (const std::exception& e) {
        fmt::print(stderr, "[MAIN] Fatal error: {}\n", e.what());
        return EXIT_FAILURE;
    }
}
