#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>

#include <boost/asio.hpp>
#include <fmt/core.h>

#include "ws_client.hpp"
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
        auto cfg = svckit::AddrConfig::from_env_defaults("localhost", 8443);
        
        fmt::print("[MAIN] Starting WebSocket client\n");
        fmt::print("[MAIN] Target: {}\n", cfg.ws_url());
        
        // IO context
        boost::asio::io_context ioc{1};
        g_ioc = &ioc;
        
        // Signal handling
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);
        
        // Create client using factory method
        auto client = ws::WSClient::create(ioc, cfg);
        
        // Start with initial message
        client->start("HELLO FROM CLIENT");
        
        // Run event loop
        ioc.run();
        
        // Cleanup
        client->stop();
        
        fmt::print("[MAIN] Client shutdown complete\n");
        return EXIT_SUCCESS;
        
    } catch (const std::exception& e) {
        fmt::print(stderr, "[MAIN] Fatal error: {}\n", e.what());
        return EXIT_FAILURE;
    }
}
