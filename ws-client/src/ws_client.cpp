#include "ws_client.hpp"

#include <exception>

#include <fmt/core.h>

namespace ws {

// ═══════════════════════════════════════════════════════════════════════════
// RULE OF SIX IMPLEMENTATION
// ═══════════════════════════════════════════════════════════════════════════

// ───────────────────────────────────────────────────────────────────────────
// Private Constructors
// ───────────────────────────────────────────────────────────────────────────

WSClient::WSClient(asio::io_context& ioc, const svckit::AddrConfig& cfg)
    : ioc_{ioc}
    , ssl_ctx_{std::make_unique<ssl::context>(ssl::context::tlsv12_client)}
    , cfg_{cfg}
    , retry_executor_{ioc.get_executor(), protocol::retry::ExponentialBackoffPolicy{}}
{
    // Configure SSL context for client
    ssl_ctx_->set_verify_mode(ssl::verify_peer);
    ssl_ctx_->load_verify_file(cfg_.tls().ca_file.string());
}

WSClient::WSClient(asio::io_context& ioc, 
                   const svckit::AddrConfig& cfg,
                   const protocol::retry::RetryConfig& retry_cfg)
    : ioc_{ioc}
    , ssl_ctx_{std::make_unique<ssl::context>(ssl::context::tlsv12_client)}
    , cfg_{cfg}
    , retry_executor_{ioc.get_executor(), protocol::retry::ExponentialBackoffPolicy{retry_cfg}}
{
    ssl_ctx_->set_verify_mode(ssl::verify_peer);
    ssl_ctx_->load_verify_file(cfg_.tls().ca_file.string());
}

// ───────────────────────────────────────────────────────────────────────────
// Factory with Retry Config
// ───────────────────────────────────────────────────────────────────────────

auto WSClient::create_with_retry(
    asio::io_context& ioc,
    const svckit::AddrConfig& cfg,
    const protocol::retry::RetryConfig& retry_cfg
) -> std::unique_ptr<WSClient> {
    return std::unique_ptr<WSClient>(new WSClient(ioc, cfg, retry_cfg));
}

// ───────────────────────────────────────────────────────────────────────────
// Destructor
// ───────────────────────────────────────────────────────────────────────────

WSClient::~WSClient() {
    if (running_.load(std::memory_order_acquire)) {
        stop();
    }
    
    fmt::print("[CLIENT] Destroyed\n");
}

// ───────────────────────────────────────────────────────────────────────────
// Move Constructor
// ───────────────────────────────────────────────────────────────────────────
//
// Transfer ownership of:
// • ssl_ctx_: unique_ptr — use std::exchange to transfer + nullify
// • cfg_: value type — std::move
// • retry_executor_: value type — std::move
// • running_: atomic — exchange and copy value
//
// After move, source has:
// • ssl_ctx_ = nullptr
// • running_ = false
// • Other members in moved-from (valid but unspecified) state
//
// ───────────────────────────────────────────────────────────────────────────

WSClient::WSClient(WSClient&& other) noexcept
    : ioc_{other.ioc_}
    , ssl_ctx_{std::exchange(other.ssl_ctx_, nullptr)}
    , cfg_{std::move(other.cfg_)}
    , retry_executor_{std::move(other.retry_executor_)}
    , api_{std::move(other.api_)}
    , running_{other.running_.exchange(false)}
{}

// ───────────────────────────────────────────────────────────────────────────
// Move Assignment
// ───────────────────────────────────────────────────────────────────────────
//
// Pattern:
// 1. Self-assignment check
// 2. Release current resources (stop if running)
// 3. Transfer from source using std::exchange
// 4. Leave source in valid empty state
//
// ───────────────────────────────────────────────────────────────────────────

WSClient& WSClient::operator=(WSClient&& other) noexcept {
    if (this != &other) {
        // Release current resources
        if (running_.load(std::memory_order_acquire)) {
            stop();
        }
        
        // Transfer ownership
        // Note: ioc_ is a reference, cannot be reassigned
        // In practice, clients aren't moved across io_contexts
        
        ssl_ctx_ = std::exchange(other.ssl_ctx_, nullptr);
        cfg_ = std::move(other.cfg_);
        retry_executor_ = std::move(other.retry_executor_);
        api_ = std::move(other.api_);
        running_.store(other.running_.exchange(false), std::memory_order_release);
    }
    return *this;
}


// ═══════════════════════════════════════════════════════════════════════════
// CLIENT OPERATIONS
// ═══════════════════════════════════════════════════════════════════════════

void WSClient::start(const std::string& initial_message) {
    running_.store(true, std::memory_order_release);
    fmt::print("[CLIENT] Starting connection to {}:{}\n", cfg_.host(), cfg_.port());
    
    asio::co_spawn(ioc_, run_session(initial_message), asio::detached);
}

void WSClient::stop() {
    running_.store(false, std::memory_order_release);
    fmt::print("[CLIENT] Stopped\n");
}


// ═══════════════════════════════════════════════════════════════════════════
// COROUTINE HANDLERS
// ═══════════════════════════════════════════════════════════════════════════

auto WSClient::run_session(std::string initial) -> asio::awaitable<void> {
    try {
        // Resolve host
        tcp::resolver resolver{ioc_};
        auto results = co_await resolver.async_resolve(
            cfg_.host(),
            std::to_string(cfg_.port()),
            asio::use_awaitable
        );
        
        // Create SSL stream
        ssl::stream<tcp::socket> ssl_stream{ioc_, *ssl_ctx_};
        
        // Connect TCP
        co_await beast::get_lowest_layer(ssl_stream).async_connect(
            *results.begin(),
            asio::use_awaitable
        );
        
        // SSL handshake
        co_await ssl_stream.async_handshake(
            ssl::stream_base::client,
            asio::use_awaitable
        );
        
        // Create WebSocket stream
        websocket::stream<ssl::stream<tcp::socket>> ws{std::move(ssl_stream)};
        
        // Configure WebSocket
        ws.set_option(websocket::stream_base::timeout::suggested(
            beast::role_type::client
        ));
        
        // WebSocket handshake
        co_await ws.async_handshake(
            cfg_.host(),
            cfg_.endpoint(),
            asio::use_awaitable
        );
        
        fmt::print("[CLIENT] Connected to {}\n", cfg_.ws_url());
        
        // Send initial message
        auto pkt = api_.make_packet(initial, protocol::Urgency::Green);
        co_await ws.async_write(
            asio::buffer(pkt.payload()),
            asio::use_awaitable
        );
        
        fmt::print("[CLIENT] Sent: {}\n", initial);
        
        // Read loop
        while (running_.load(std::memory_order_acquire)) {
            beast::flat_buffer buffer;
            
            auto [ec, bytes] = co_await ws.async_read(
                buffer,
                asio::as_tuple(asio::use_awaitable)
            );
            
            if (ec) {
                if (ec != websocket::error::closed) {
                    fmt::print("[CLIENT] Read error: {}\n", ec.message());
                }
                break;
            }
            
            // Process response
            std::string msg = beast::buffers_to_string(buffer.data());
            auto rx_pkt = api_.make_packet(msg, protocol::Urgency::Green);
            api_.dispatch(rx_pkt, *this);
        }
        
        // Graceful close
        fmt::print("[CLIENT] Closing connection\n");
        co_await ws.async_close(
            websocket::close_code::normal,
            asio::as_tuple(asio::use_awaitable)
        );
        
    } catch (const std::exception& e) {
        fmt::print("[CLIENT] Session exception: {}\n", e.what());
    }
}

auto WSClient::connect_with_retry() -> asio::awaitable<void> {
    // Example of using retry executor for connection
    // This wraps the connection logic with exponential backoff
    
    auto result = co_await retry_executor_.execute([this]() -> asio::awaitable<void> {
        tcp::resolver resolver{ioc_};
        auto results = co_await resolver.async_resolve(
            cfg_.host(),
            std::to_string(cfg_.port()),
            asio::use_awaitable
        );
        
        ssl::stream<tcp::socket> ssl_stream{ioc_, *ssl_ctx_};
        
        co_await beast::get_lowest_layer(ssl_stream).async_connect(
            *results.begin(),
            asio::use_awaitable
        );
        
        co_await ssl_stream.async_handshake(
            ssl::stream_base::client,
            asio::use_awaitable
        );
        
        fmt::print("[CLIENT] Connected (with retry)\n");
    });
    
    if (result.failed()) {
        fmt::print("[CLIENT] Connection failed after {} attempts, total delay: {}ms\n",
                   result.attempts,
                   result.total_delay.count());
                   
        if (result.last_error) {
            try {
                std::rethrow_exception(result.last_error);
            } catch (const std::exception& e) {
                fmt::print("[CLIENT] Last error: {}\n", e.what());
            }
        }
    }
}


// ═══════════════════════════════════════════════════════════════════════════
// STRATEGY PATTERN HANDLERS
// ═══════════════════════════════════════════════════════════════════════════

void WSClient::on_normal(const protocol::Packet& pkt) {
    fmt::print("[CLIENT] Response: {}\n", pkt.payload_as_string());
}

void WSClient::on_urgent(const protocol::Packet& pkt) {
    fmt::print("[CLIENT] RED ALERT! Drone target: {}\n", pkt.payload_as_string());
}

}  // namespace ws
