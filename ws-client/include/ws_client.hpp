#pragma once

/// @file ws_client.hpp
/// @brief TLS WebSocket client using Asio coroutines with retry support.
///
/// Demonstrates:
/// - Rule of Six: Move-only resource class
/// - Perfect forwarding factory method
/// - Retry mechanism integration
/// - Asio awaitable coroutines (no Cobalt)

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

#include "protocol.hpp"
#include "retry.hpp"
#include "svc_addr_config.hpp"

namespace ws {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace ssl = asio::ssl;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;


// ═══════════════════════════════════════════════════════════════════════════
// WSClient — Move-Only Resource Class with Retry Support
// ═══════════════════════════════════════════════════════════════════════════
//
// RULE OF SIX RATIONALE:
//
// This class manages unique resources:
// • SSL context (OpenSSL state — unique ownership)
// • io_context reference (external lifetime — not owned)
// • Retry executor (stateful, but movable)
//
// DECISION: Move-only semantics
// • Default ctor: Deleted (requires valid io_context)
// • Destructor: Releases SSL context, stops operations
// • Copy ops: DELETED — SSL context cannot be duplicated
// • Move ops: Transfer ownership using std::exchange
//
// ═══════════════════════════════════════════════════════════════════════════

/// TLS WebSocket client with automatic retry support.
///
/// @par Ownership Model
/// Move-only — cannot be copied, can be moved to transfer ownership.
///
/// @par Retry Behavior
/// Connection attempts are retried using exponential backoff.
/// Configure via RetryConfig at construction time.
///
/// @par Example
/// @code
/// auto client = WSClient::create(ioc, config);
/// client->start("Hello, server!");
/// ioc.run();
/// @endcode
class WSClient : public protocol::IPacketHandler {
public:
    // ───────────────────────────────────────────────────────────────────────
    // RULE OF SIX: Move-Only Pattern
    // ───────────────────────────────────────────────────────────────────────
    
    /// Default constructor — DELETED (requires io_context).
    WSClient() = delete;
    
    /// Destructor — releases resources.
    ~WSClient() override;
    
    /// Copy constructor — DELETED (unique ownership of SSL context).
    WSClient(const WSClient&) = delete;
    
    /// Copy assignment — DELETED.
    WSClient& operator=(const WSClient&) = delete;
    
    /// Move constructor — transfers ownership.
    ///
    /// Uses std::exchange for safe single-expression transfer.
    WSClient(WSClient&& other) noexcept;
    
    /// Move assignment — transfers ownership with cleanup.
    WSClient& operator=(WSClient&& other) noexcept;
    
    // ───────────────────────────────────────────────────────────────────────
    // Factory Methods
    // ───────────────────────────────────────────────────────────────────────
    
    /// Create client with perfect forwarding.
    ///
    /// @tparam Args Constructor argument types
    /// @param args Arguments forwarded to constructor
    /// @return unique_ptr to constructed client
    template<typename... Args>
    [[nodiscard]] static auto create(Args&&... args) -> std::unique_ptr<WSClient> {
        return std::unique_ptr<WSClient>(new WSClient(std::forward<Args>(args)...));
    }
    
    /// Create client with custom retry configuration.
    [[nodiscard]] static auto create_with_retry(
        asio::io_context& ioc,
        const svckit::AddrConfig& cfg,
        const protocol::retry::RetryConfig& retry_cfg
    ) -> std::unique_ptr<WSClient>;
    
    // ───────────────────────────────────────────────────────────────────────
    // Client Operations
    // ───────────────────────────────────────────────────────────────────────
    
    /// Start client session with initial message.
    ///
    /// Spawns connection coroutine. Non-blocking — returns immediately.
    /// @param initial_message First message to send after connecting
    void start(const std::string& initial_message);
    
    /// Stop client operations.
    void stop();
    
    /// Check if client is running.
    [[nodiscard]] auto is_running() const noexcept -> bool {
        return running_.load(std::memory_order_acquire);
    }
    
    // ───────────────────────────────────────────────────────────────────────
    // IPacketHandler Implementation (Strategy Pattern)
    // ───────────────────────────────────────────────────────────────────────
    
    void on_normal(const protocol::Packet& pkt) override;
    void on_urgent(const protocol::Packet& pkt) override;

private:
    // ───────────────────────────────────────────────────────────────────────
    // Private Constructors (use factory)
    // ───────────────────────────────────────────────────────────────────────
    
    /// Construct client with configuration.
    explicit WSClient(asio::io_context& ioc, const svckit::AddrConfig& cfg);
    
    /// Construct client with custom retry configuration.
    WSClient(asio::io_context& ioc, 
             const svckit::AddrConfig& cfg,
             const protocol::retry::RetryConfig& retry_cfg);
    
    // ───────────────────────────────────────────────────────────────────────
    // Coroutine Handlers
    // ───────────────────────────────────────────────────────────────────────
    
    /// Main session coroutine.
    auto run_session(std::string initial) -> asio::awaitable<void>;
    
    /// Connection with retry wrapper.
    auto connect_with_retry() -> asio::awaitable<void>;
    
    // ───────────────────────────────────────────────────────────────────────
    // Member Data
    // ───────────────────────────────────────────────────────────────────────
    
    /// Reference to io_context (not owned).
    asio::io_context& ioc_;
    
    /// SSL context (owned via unique_ptr).
    std::unique_ptr<ssl::context> ssl_ctx_;
    
    /// Client configuration (value type, copyable).
    svckit::AddrConfig cfg_;
    
    /// Retry executor for connection attempts.
    protocol::retry::DefaultRetryExecutor retry_executor_;
    
    /// Protocol API for packet handling.
    protocol::ProtocolAPI api_;
    
    /// Running state flag.
    std::atomic<bool> running_{false};
};

}  // namespace ws
