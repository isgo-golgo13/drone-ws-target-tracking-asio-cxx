#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <thread>

#include <boost/asio.hpp>
#include <fmt/core.h>

#include "ws_server.hpp"
#include "ws_client.hpp"
#include "svc_addr_config.hpp"

namespace {

std::atomic<bool> g_running{true};

void signal_handler(int sig) {
    fmt::print("\n[ORCH] Received signal {}, initiating shutdown...\n", sig);
    g_running.store(false, std::memory_order_release);
}

}  // namespace

/// Orchestrator application demonstrating Rule of Six and factory patterns.
///
/// This application:
/// 1. Creates WSServer and WSClient using factory methods
/// 2. Runs each in separate threads with their own io_context
/// 3. Demonstrates ownership transfer (move semantics)
/// 4. Handles graceful shutdown via signals
class Application {
public:
    // ───────────────────────────────────────────────────────────────────────
    // RULE OF SIX: Non-copyable, Non-movable
    //
    // Application is a singleton-like orchestrator tied to process lifetime.
    // Moving or copying would create multiple signal handlers, multiple
    // shutdown sequences, etc.
    // ───────────────────────────────────────────────────────────────────────
    
    Application() = default;
    ~Application() = default;
    
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;
    
    /// Run the orchestrated server and client.
    auto run() -> int {
        try {
            // Install signal handlers
            std::signal(SIGINT, signal_handler);
            std::signal(SIGTERM, signal_handler);
            
            fmt::print("[ORCH] Starting orchestrator\n");
            
            // Server thread
            std::thread server_thread([this]() {
                run_server();
            });
            
            // Give server time to start
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            
            // Client thread
            std::thread client_thread([this]() {
                run_client();
            });
            
            // Wait for shutdown signal
            while (g_running.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            
            fmt::print("[ORCH] Shutdown initiated, waiting for threads...\n");
            
            // Join threads
            if (server_thread.joinable()) {
                server_thread.join();
            }
            if (client_thread.joinable()) {
                client_thread.join();
            }
            
            fmt::print("[ORCH] Shutdown complete\n");
            return EXIT_SUCCESS;
            
        } catch (const std::exception& e) {
            fmt::print(stderr, "[ORCH] Fatal error: {}\n", e.what());
            return EXIT_FAILURE;
        }
    }

private:
    void run_server() {
        try {
            boost::asio::io_context ioc{1};
            
            auto cfg = svckit::AddrConfig::from_env_defaults("0.0.0.0", 8443);
            
            // Create server using factory (demonstrates perfect forwarding)
            auto server = ws::WSServer::create(ioc, cfg);
            server->run();
            
            // Run until shutdown
            while (g_running.load(std::memory_order_acquire)) {
                ioc.run_for(std::chrono::milliseconds(50));
            }
            
            server->stop();
            
        } catch (const std::exception& e) {
            fmt::print(stderr, "[SERVER] Exception: {}\n", e.what());
        }
    }
    
    void run_client() {
        try {
            boost::asio::io_context ioc{1};
            
            auto cfg = svckit::AddrConfig::from_env_defaults("localhost", 8443);
            
            // Create client using factory
            auto client = ws::WSClient::create(ioc, cfg);
            client->start("HELLO FROM ORCHESTRATOR");
            
            // Run until shutdown
            while (g_running.load(std::memory_order_acquire)) {
                ioc.run_for(std::chrono::milliseconds(50));
            }
            
            client->stop();
            
        } catch (const std::exception& e) {
            fmt::print(stderr, "[CLIENT] Exception: {}\n", e.what());
        }
    }
};


int main() {
    Application app;
    return app.run();
}
