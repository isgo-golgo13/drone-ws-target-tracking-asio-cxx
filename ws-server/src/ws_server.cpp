#include "ws_server.hpp"

#include <exception>
#include <thread>

#include <fmt/core.h>

namespace ws {

// ═══════════════════════════════════════════════════════════════════════════
// RULE OF SIX IMPLEMENTATION
// ═══════════════════════════════════════════════════════════════════════════

// ───────────────────────────────────────────────────────────────────────────
// Private Constructor
// ───────────────────────────────────────────────────────────────────────────

WSServer::WSServer(asio::io_context& ioc, const svckit::AddrConfig& cfg)
    : ioc_{ioc}
    , acceptor_{ioc}
    , ssl_ctx_{std::make_unique<ssl::context>(ssl::context::tlsv12_server)}
    , cfg_{cfg}
{
    // Configure SSL context
    ssl_ctx_->set_options(
        ssl::context::default_workarounds |
        ssl::context::no_sslv2 |
        ssl::context::single_dh_use
    );
    
    ssl_ctx_->use_certificate_file(cfg_.tls().cert_file.string(), ssl::context::pem);
    ssl_ctx_->use_private_key_file(cfg_.tls().key_file.string(), ssl::context::pem);
    
    // Configure acceptor
    tcp::endpoint endpoint{tcp::v4(), cfg_.port()};
    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(asio::socket_base::reuse_address(true));
    acceptor_.bind(endpoint);
    acceptor_.listen();
}

// ───────────────────────────────────────────────────────────────────────────
// Destructor
// ───────────────────────────────────────────────────────────────────────────

WSServer::~WSServer() {
    // Stop accepting if still running
    if (running_.load(std::memory_order_acquire)) {
        stop();
    }
    
    // Acceptor closed automatically by its destructor
    // SSL context released automatically by unique_ptr destructor
    
    fmt::print("[SERVER] Destroyed\n");
}

// ───────────────────────────────────────────────────────────────────────────
// Move Constructor
// ───────────────────────────────────────────────────────────────────────────
//
// Uses std::exchange for safe, single-expression ownership transfer.
// After move, source is in valid but "empty" state:
// - acceptor_: default-constructed (not open)
// - ssl_ctx_: nullptr
// - running_: false
//
// ───────────────────────────────────────────────────────────────────────────

WSServer::WSServer(WSServer&& other) noexcept
    : ioc_{other.ioc_}  // Reference — just copies reference
    , acceptor_{std::move(other.acceptor_)}  // Move acceptor ownership
    , ssl_ctx_{std::exchange(other.ssl_ctx_, nullptr)}  // Transfer + nullify
    , cfg_{std::move(other.cfg_)}  // Move config (value type)
    , api_{std::move(other.api_)}  // Move API (value type)
    , running_{other.running_.exchange(false)}  // Atomic transfer + reset
{}

// ───────────────────────────────────────────────────────────────────────────
// Move Assignment
// ───────────────────────────────────────────────────────────────────────────
//
// Pattern:
// 1. Self-assignment check (essential for safety)
// 2. Release current resources (stop, close)
// 3. Transfer ownership from source using std::exchange
// 4. Leave source in valid "empty" state
//
// ───────────────────────────────────────────────────────────────────────────

WSServer& WSServer::operator=(WSServer&& other) noexcept {
    if (this != &other) {
        // 1. Release current resources
        if (running_.load(std::memory_order_acquire)) {
            stop();
        }
        
        // Close our acceptor explicitly (will be replaced)
        if (acceptor_.is_open()) {
            beast::error_code ec;
            acceptor_.close(ec);  // Ignore errors on close
        }
        
        // ssl_ctx_ will be replaced, unique_ptr handles cleanup
        
        // 2. Transfer ownership
        // Note: ioc_ is a reference, we just rebind it
        // This is technically UB if io_contexts differ, but 
        // in practice servers are not reassigned across contexts
        
        acceptor_ = std::move(other.acceptor_);
        ssl_ctx_ = std::exchange(other.ssl_ctx_, nullptr);
        cfg_ = std::move(other.cfg_);
        api_ = std::move(other.api_);
        running_.store(other.running_.exchange(false), std::memory_order_release);
    }
    return *this;
}


// ═══════════════════════════════════════════════════════════════════════════
// SERVER OPERATIONS
// ═══════════════════════════════════════════════════════════════════════════

void WSServer::run() {
    running_.store(true, std::memory_order_release);
    fmt::print("[SERVER] Listening on {}:{}\n", cfg_.host(), cfg_.port());
    
    asio::co_spawn(ioc_, accept_loop(), asio::detached);
}

void WSServer::stop() {
    running_.store(false, std::memory_order_release);
    
    beast::error_code ec;
    acceptor_.close(ec);
    
    if (ec) {
        fmt::print("[SERVER] Error closing acceptor: {}\n", ec.message());
    } else {
        fmt::print("[SERVER] Stopped\n");
    }
}


// ═══════════════════════════════════════════════════════════════════════════
// COROUTINE HANDLERS
// ═══════════════════════════════════════════════════════════════════════════

auto WSServer::accept_loop() -> asio::awaitable<void> {
    while (running_.load(std::memory_order_acquire)) {
        auto [ec, socket] = co_await acceptor_.async_accept(
            asio::as_tuple(asio::use_awaitable)
        );
        
        if (ec) {
            if (running_.load(std::memory_order_acquire)) {
                fmt::print("[SERVER] Accept error: {}\n", ec.message());
            }
            continue;
        }
        
        // Spawn session handler (fire-and-forget)
        asio::co_spawn(ioc_, handle_session(std::move(socket)), asio::detached);
    }
}

auto WSServer::handle_session(tcp::socket socket) -> asio::awaitable<void> {
    try {
        // Create SSL stream
        ssl::stream<tcp::socket> ssl_stream{std::move(socket), *ssl_ctx_};
        
        // SSL handshake
        co_await ssl_stream.async_handshake(
            ssl::stream_base::server,
            asio::use_awaitable
        );
        
        // Create WebSocket stream over SSL
        websocket::stream<ssl::stream<tcp::socket>> ws{std::move(ssl_stream)};
        
        // Configure WebSocket
        ws.set_option(websocket::stream_base::timeout::suggested(
            beast::role_type::server
        ));
        
        // Accept WebSocket handshake
        co_await ws.async_accept(asio::use_awaitable);
        
        fmt::print("[SERVER] WebSocket session opened\n");
        
        // Read loop
        while (running_.load(std::memory_order_acquire)) {
            beast::flat_buffer buffer;
            
            auto [ec, bytes] = co_await ws.async_read(
                buffer,
                asio::as_tuple(asio::use_awaitable)
            );
            
            if (ec) {
                if (ec != websocket::error::closed) {
                    fmt::print("[SERVER] Read error: {}\n", ec.message());
                }
                break;
            }
            
            // Process packet
            std::string msg = beast::buffers_to_string(buffer.data());
            auto pkt = api_.make_packet(msg, protocol::Urgency::Green);
            api_.dispatch(pkt, *this);
            
            // Echo response
            co_await ws.async_write(
                asio::buffer(msg),
                asio::use_awaitable
            );
        }
        
        fmt::print("[SERVER] WebSocket session closed\n");
        
    } catch (const std::exception& e) {
        fmt::print("[SERVER] Session exception: {}\n", e.what());
    }
}


// ═══════════════════════════════════════════════════════════════════════════
// STRATEGY PATTERN HANDLERS
// ═══════════════════════════════════════════════════════════════════════════

void WSServer::on_normal(const protocol::Packet& pkt) {
    fmt::print("[SERVER] Normal packet: {}\n", pkt.payload_as_string());
}

void WSServer::on_urgent(const protocol::Packet& pkt) {
    fmt::print("[SERVER] URGENT RED - STREAMING DRONE TARGET DATA\n");
    
    // Simulate SSE-like streaming (fire-and-forget thread)
    std::thread([payload = pkt.payload_as_string()]() {
        for (int i = 0; i < 5; ++i) {
            fmt::print("[DRONE STREAM] lat={:.4f}, lon={:.4f}\n",
                       34.2345 + i * 0.0001,
                       69.1234 + i * 0.0002);
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
        }
    }).detach();
}

}  // namespace ws
