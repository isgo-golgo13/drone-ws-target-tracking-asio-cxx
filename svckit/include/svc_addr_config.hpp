#pragma once

/// @file svc_addr_config.hpp
/// @brief Service configuration toolkit demonstrating Rule of Six patterns.
///
/// This header provides AddrConfig and TlsConfig classes following modern C++23
/// idioms with comprehensive Rule of Six implementation.

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace svckit {

// ═══════════════════════════════════════════════════════════════════════════
// TlsConfig — Trivial Class Pattern (All Default)
// ═══════════════════════════════════════════════════════════════════════════
//
// RULE OF SIX RATIONALE:
// • Contains only std::filesystem::path members (value types)
// • No raw pointers, handles, or unique resources
// • Compiler-generated operations are correct and optimal
// • Explicitly defaulted for documentation and clarity
//
// ═══════════════════════════════════════════════════════════════════════════

/// TLS certificate paths configuration.
class TlsConfig {
public:
    // ───────────────────────────────────────────────────────────────────────
    // RULE OF SIX: All Defaulted
    // ───────────────────────────────────────────────────────────────────────
    
    /// Default constructor — members use their own default constructors.
    TlsConfig() = default;
    
    /// Destructor — trivial, no resources to release.
    ~TlsConfig() = default;
    
    /// Copy constructor — memberwise copy (paths handle own memory).
    TlsConfig(const TlsConfig&) = default;
    
    /// Copy assignment — memberwise copy assignment.
    TlsConfig& operator=(const TlsConfig&) = default;
    
    /// Move constructor — memberwise move.
    TlsConfig(TlsConfig&&) noexcept = default;
    
    /// Move assignment — memberwise move assignment.
    TlsConfig& operator=(TlsConfig&&) noexcept = default;
    
    // ───────────────────────────────────────────────────────────────────────
    // Parameterized Constructor
    // ───────────────────────────────────────────────────────────────────────
    
    /// Construct with explicit paths.
    TlsConfig(std::filesystem::path cert,
              std::filesystem::path key,
              std::filesystem::path ca)
        : cert_file{std::move(cert)}
        , key_file{std::move(key)}
        , ca_file{std::move(ca)}
    {}
    
    // ───────────────────────────────────────────────────────────────────────
    // Factory Methods
    // ───────────────────────────────────────────────────────────────────────
    
    /// Create TLS config from environment-derived paths.
    /// Uses CERT_PATH environment variable, falls back to ./certificates.
    [[nodiscard]] static auto from_env() -> TlsConfig {
        const char* env = std::getenv("CERT_PATH");
        std::filesystem::path base = (env && *env)
            ? std::filesystem::path{env}
            : std::filesystem::path{"certificates"};
            
        return TlsConfig{
            base / "server.pem",
            base / "server-key.pem",
            base / "server.pem"
        };
    }
    
    // ───────────────────────────────────────────────────────────────────────
    // Public Data Members (aggregate-style for simple config)
    // ───────────────────────────────────────────────────────────────────────
    
    std::filesystem::path cert_file;
    std::filesystem::path key_file;
    std::filesystem::path ca_file;
};


// ═══════════════════════════════════════════════════════════════════════════
// ProtocolHint — Enum Class (No Special Members Needed)
// ═══════════════════════════════════════════════════════════════════════════

/// Protocol hint for connection type.
enum class ProtocolHint : std::uint8_t {
    Wss,  ///< Secure WebSocket (TLS)
    Ws    ///< Plain WebSocket
};

/// Convert ProtocolHint to string representation.
[[nodiscard]] constexpr auto to_string(ProtocolHint hint) noexcept 
    -> std::string_view 
{
    switch (hint) {
        case ProtocolHint::Wss: return "wss";
        case ProtocolHint::Ws:  return "ws";
    }
    return "wss";  // Default fallback
}


// ═══════════════════════════════════════════════════════════════════════════
// AddrConfig — Trivial Class Pattern with Builder Methods
// ═══════════════════════════════════════════════════════════════════════════
//
// RULE OF SIX RATIONALE:
// • Contains std::string (value type), uint16_t (trivial), TlsConfig (trivial)
// • No raw pointers or unique resources requiring manual management
// • All members handle their own memory/lifetime
// • Compiler-generated operations are correct
//
// ═══════════════════════════════════════════════════════════════════════════

/// Address and TLS configuration for WebSocket services.
///
/// Follows the Open-Closed Principle — extend via builder methods
/// without modifying existing API.
///
/// @par Example Usage
/// @code
/// auto cfg = AddrConfig::from_env_defaults("localhost", 8443)
///                .with_endpoint("/api/v1/ws")
///                .without_tls();
/// @endcode
class AddrConfig {
public:
    // ───────────────────────────────────────────────────────────────────────
    // RULE OF SIX: All Defaulted
    // 
    // All members are either:
    // • std::string — manages own memory, has correct special members
    // • uint16_t — trivially copyable
    // • TlsConfig — trivial class with defaulted special members
    // • ProtocolHint — enum, trivially copyable
    // • bool — trivially copyable
    //
    // Therefore, compiler-generated operations are correct.
    // ───────────────────────────────────────────────────────────────────────
    
    /// Default constructor.
    AddrConfig() = default;
    
    /// Destructor.
    ~AddrConfig() = default;
    
    /// Copy constructor.
    AddrConfig(const AddrConfig&) = default;
    
    /// Copy assignment.
    AddrConfig& operator=(const AddrConfig&) = default;
    
    /// Move constructor.
    AddrConfig(AddrConfig&&) noexcept = default;
    
    /// Move assignment.
    AddrConfig& operator=(AddrConfig&&) noexcept = default;
    
    // ───────────────────────────────────────────────────────────────────────
    // Parameterized Constructor
    // ───────────────────────────────────────────────────────────────────────
    
    /// Construct with host, port, and TLS configuration.
    AddrConfig(std::string host, std::uint16_t port, TlsConfig tls)
        : host_{std::move(host)}
        , port_{port}
        , tls_{std::move(tls)}
    {}
    
    // ───────────────────────────────────────────────────────────────────────
    // Factory Methods (Named Constructors)
    // ───────────────────────────────────────────────────────────────────────
    
    /// Create configuration from environment defaults.
    /// @param host Hostname or IP address
    /// @param port Port number
    /// @return Configured AddrConfig instance
    [[nodiscard]] static auto from_env_defaults(std::string host, std::uint16_t port) 
        -> AddrConfig 
    {
        return AddrConfig{std::move(host), port, TlsConfig::from_env()};
    }
    
    /// Perfect forwarding factory for derived configurations.
    /// @tparam Args Constructor argument types
    /// @param args Arguments forwarded to constructor
    /// @return Configured AddrConfig instance
    template<typename... Args>
    [[nodiscard]] static auto create(Args&&... args) -> AddrConfig {
        return AddrConfig{std::forward<Args>(args)...};
    }
    
    // ───────────────────────────────────────────────────────────────────────
    // Builder Methods (Fluent Interface)
    // ───────────────────────────────────────────────────────────────────────
    
    /// Set endpoint path.
    [[nodiscard]] auto with_endpoint(std::string endpoint) && -> AddrConfig {
        endpoint_ = std::move(endpoint);
        return std::move(*this);
    }
    
    /// Disable TLS (use plain WebSocket).
    [[nodiscard]] auto without_tls() && -> AddrConfig {
        use_tls_ = false;
        protocol_hint_ = ProtocolHint::Ws;
        return std::move(*this);
    }
    
    /// Set custom TLS configuration.
    [[nodiscard]] auto with_tls(TlsConfig tls) && -> AddrConfig {
        tls_ = std::move(tls);
        use_tls_ = true;
        protocol_hint_ = ProtocolHint::Wss;
        return std::move(*this);
    }
    
    // ───────────────────────────────────────────────────────────────────────
    // Accessors
    // ───────────────────────────────────────────────────────────────────────
    
    [[nodiscard]] auto host() const noexcept -> const std::string& { return host_; }
    [[nodiscard]] auto port() const noexcept -> std::uint16_t { return port_; }
    [[nodiscard]] auto endpoint() const noexcept -> const std::string& { return endpoint_; }
    [[nodiscard]] auto tls() const noexcept -> const TlsConfig& { return tls_; }
    [[nodiscard]] auto use_tls() const noexcept -> bool { return use_tls_; }
    [[nodiscard]] auto protocol_hint() const noexcept -> ProtocolHint { return protocol_hint_; }
    
    /// Get full WebSocket URL.
    [[nodiscard]] auto ws_url() const -> std::string {
        return std::string{to_string(protocol_hint_)} + "://" + 
               host_ + ":" + std::to_string(port_) + endpoint_;
    }
    
    /// Get host:port address string.
    [[nodiscard]] auto addr() const -> std::string {
        return host_ + ":" + std::to_string(port_);
    }

private:
    std::string host_;
    std::uint16_t port_{0};
    TlsConfig tls_;
    std::string endpoint_{"/"};
    ProtocolHint protocol_hint_{ProtocolHint::Wss};
    bool use_tls_{true};
};

}  // namespace svckit
