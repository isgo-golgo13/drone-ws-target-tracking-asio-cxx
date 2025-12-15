#pragma once

/// @file ws_server.hpp
/// @brief TLS WebSocket server using Asio coroutines.
///
/// Demonstrates:
/// - Rule of Six: Move-only resource class
/// - Perfect forwarding factory method
/// - Policy-based packet dispatch
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
// WSServer — Move-Only Resource Class
// ═══════════════════════════════════════════════════════════════════════════
//
// RULE OF SIX RATIONALE:
//
// This class manages unique resources:
// • TCP acceptor (socket handle — cannot be duplicated)
// • SSL context (OpenSSL state — unique ownership)
// • io_context reference (external lifetime — not owned)
//
// DECISION: Move-only semantics
// • Default ctor: Deleted (requires valid io_context)
// • Destructor: Closes acceptor, releases SSL context
// • Copy ops: DELETED — acceptor/SSL context cannot be duplicated
// • Move ops: Transfer ownership using std::exchange
//
// WHY NOT shared_ptr?
// The server has a single owner (main or orchestrator). Sharing ownership
// would complicate lifetime and introduce potential use-after-move bugs.
// Move-only enforces clear ownership transfer.
//
// ═══════════════════════════════════════════════════════════════════════════

/// TLS WebSocket server with coroutine-based session handling.
///
/// @par Ownership Model
/// Move-only — cannot be copied, can be moved to transfer ownership.
/// The server owns its acceptor and SSL context exclusively.
///
/// @par Thread Safety
/// Not thread-safe. Run from single thread or strand.
///
/// @par Example
/// @code
/// auto server = WSServer::create(ioc, config);
/// server->run();
/// ioc.run();
/// @endcode
class WSServer : public protocol::IPacketHandler {
public:
    // ───────────────────────────────────────────────────────────────────────
    // RULE OF SIX: Move-Only Pattern
    // ───────────────────────────────────────────────────────────────────────
    
    /// Default constructor — DELETED (requires io_context).
    WSServer() = delete;
    
    /// Destructor — releases resources.
    ///
    /// Closes acceptor if open, SSL context released via unique_ptr.
    ~WSServer() override;
    
    /// Copy constructor — DELETED (unique ownership of acceptor/SSL).
    WSServer(const WSServer&) = delete;
    
    /// Copy assignment — DELETED.
    WSServer& operator=(const WSServer&) = delete;
    
    /// Move constructor — transfers ownership.
    ///
    /// Source server is left in valid but inactive state.
    /// Uses std::exchange for safe single-expression transfer.
    WSServer(WSServer&& other) noexcept;
    
    /// Move assignment — transfers ownership with cleanup.
    ///
    /// Current resources released before transfer.
    /// Self-assignment check included for safety.
    WSServer& operator=(WSServer&& other) noexcept;
    
    // ───────────────────────────────────────────────────────────────────────
    // Factory Methods
    // ───────────────────────────────────────────────────────────────────────
    
    /// Create server with perfect forwarding.
    ///
    /// @tparam Args Constructor argument types
    /// @param args Arguments forwarded to constructor
    /// @return unique_ptr to constructed server
    ///
    /// Using unique_ptr factory because:
    /// 1. Server is move-only, returning by value works but ptr is clearer
    /// 2. Allows polymorphic usage if we add derived server types
    /// 3. Consistent with other resource-managing types
    template<typename... Args>
    [[nodiscard]] static auto create(Args&&... args) -> std::unique_ptr<WSServer> {
        return std::unique_ptr<WSServer>(new WSServer(std::forward<Args>(args)...));
    }
    
    // ───────────────────────────────────────────────────────────────────────
    // Server Operations
    // ───────────────────────────────────────────────────────────────────────
    
    /// Start accepting connections.
    ///
    /// Spawns accept loop coroutine. Non-blocking — returns immediately.
    /// Call io_context::run() to process connections.
    void run();
    
    /// Stop accepting new connections.
    ///
    /// Closes acceptor. Existing sessions continue until complete.
    void stop();
    
    /// Check if server is running.
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
    // Private Constructor (use factory)
    // ───────────────────────────────────────────────────────────────────────
    
    /// Construct server with configuration.
    ///
    /// @param ioc IO context for async operations
    /// @param cfg Address and TLS configuration
    ///
    /// @throws boost::system::system_error if bind/listen fails
    explicit WSServer(asio::io_context& ioc, const svckit::AddrConfig& cfg);
    
    // ───────────────────────────────────────────────────────────────────────
    // Coroutine Handlers
    // ───────────────────────────────────────────────────────────────────────
    
    /// Accept loop coroutine.
    auto accept_loop() -> asio::awaitable<void>;
    
    /// Handle single WebSocket session.
    auto handle_session(tcp::socket socket) -> asio::awaitable<void>;
    
    // ───────────────────────────────────────────────────────────────────────
    // Member Data
    // ───────────────────────────────────────────────────────────────────────
    
    /// Reference to io_context (not owned).
    asio::io_context& ioc_;
    
    /// TCP acceptor (owned, move-only resource).
    tcp::acceptor acceptor_;
    
    /// SSL context (owned via unique_ptr).
    std::unique_ptr<ssl::context> ssl_ctx_;
    
    /// Server configuration (value type, copyable).
    svckit::AddrConfig cfg_;
    
    /// Protocol API for packet handling.
    protocol::ProtocolAPI api_;
    
    /// Running state flag (atomic for thread-safe reads).
    std::atomic<bool> running_{false};
};

}  // namespace ws
