#pragma once

/// @file protocol.hpp
/// @brief Protocol definitions with Policy-based Strategy pattern.
///
/// Demonstrates:
/// - Policy-based design (compile-time strategy selection)
/// - Rule of Six for Packet class
/// - Perfect forwarding factory methods
/// - Type-safe urgency handling

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/core.h>

namespace protocol {

// ═══════════════════════════════════════════════════════════════════════════
// Urgency — Enum Class with String Conversion
// ═══════════════════════════════════════════════════════════════════════════

/// Packet urgency level.
enum class Urgency : std::uint8_t {
    Green  = 0,   ///< Normal priority
    Yellow = 1,   ///< Elevated priority
    Red    = 2    ///< Critical / emergency
};

/// Convert urgency to string representation.
[[nodiscard]] constexpr auto to_string(Urgency u) noexcept -> std::string_view {
    constexpr std::array<std::string_view, 3> names = {"GREEN", "YELLOW", "RED"};
    const auto idx = static_cast<std::size_t>(u);
    return idx < names.size() ? names[idx] : "UNKNOWN";
}

/// Parse string to Urgency.
[[nodiscard]] constexpr auto urgency_from_string(std::string_view sv) noexcept -> Urgency {
    if (sv == "RED" || sv == "red") return Urgency::Red;
    if (sv == "YELLOW" || sv == "yellow") return Urgency::Yellow;
    return Urgency::Green;
}


// ═══════════════════════════════════════════════════════════════════════════
// Packet — Value Class with Rule of Six (All Default)
// ═══════════════════════════════════════════════════════════════════════════
//
// RULE OF SIX RATIONALE:
// • Contains std::vector<uint8_t> (manages own memory)
// • Contains Urgency enum (trivially copyable)
// • No raw pointers or external handles
// • Compiler-generated operations are correct
// • Defaulted explicitly for documentation
//
// ═══════════════════════════════════════════════════════════════════════════

/// Protocol packet containing payload and urgency metadata.
///
/// Value semantics — can be freely copied, moved, stored in containers.
class Packet {
public:
    // ───────────────────────────────────────────────────────────────────────
    // RULE OF SIX: All Defaulted
    // ───────────────────────────────────────────────────────────────────────
    
    /// Default constructor — empty payload, GREEN urgency.
    Packet() = default;
    
    /// Destructor — vector handles own cleanup.
    ~Packet() = default;
    
    /// Copy constructor — deep copies payload vector.
    Packet(const Packet&) = default;
    
    /// Copy assignment — deep copies payload vector.
    Packet& operator=(const Packet&) = default;
    
    /// Move constructor — transfers payload ownership.
    Packet(Packet&&) noexcept = default;
    
    /// Move assignment — transfers payload ownership.
    Packet& operator=(Packet&&) noexcept = default;
    
    // ───────────────────────────────────────────────────────────────────────
    // Parameterized Constructors
    // ───────────────────────────────────────────────────────────────────────
    
    /// Construct from payload and urgency.
    Packet(std::vector<std::uint8_t> payload, Urgency urgency)
        : payload_{std::move(payload)}
        , urgency_{urgency}
    {}
    
    /// Construct from string payload.
    Packet(std::string_view data, Urgency urgency)
        : payload_{data.begin(), data.end()}
        , urgency_{urgency}
    {}
    
    // ───────────────────────────────────────────────────────────────────────
    // Factory Methods (Perfect Forwarding)
    // ───────────────────────────────────────────────────────────────────────
    
    /// Create packet with perfect forwarding.
    template<typename... Args>
    [[nodiscard]] static auto create(Args&&... args) -> Packet {
        return Packet{std::forward<Args>(args)...};
    }
    
    /// Create packet from raw bytes.
    [[nodiscard]] static auto from_bytes(std::span<const std::uint8_t> data, 
                                          Urgency urgency = Urgency::Green) 
        -> Packet 
    {
        return Packet{std::vector<std::uint8_t>{data.begin(), data.end()}, urgency};
    }
    
    /// Create packet from string.
    [[nodiscard]] static auto from_string(std::string_view sv,
                                          Urgency urgency = Urgency::Green)
        -> Packet
    {
        return Packet{sv, urgency};
    }
    
    // ───────────────────────────────────────────────────────────────────────
    // Accessors
    // ───────────────────────────────────────────────────────────────────────
    
    [[nodiscard]] auto payload() const noexcept -> const std::vector<std::uint8_t>& {
        return payload_;
    }
    
    [[nodiscard]] auto payload() noexcept -> std::vector<std::uint8_t>& {
        return payload_;
    }
    
    [[nodiscard]] auto urgency() const noexcept -> Urgency {
        return urgency_;
    }
    
    [[nodiscard]] auto payload_as_string() const -> std::string {
        return std::string{payload_.begin(), payload_.end()};
    }
    
    [[nodiscard]] auto payload_view() const noexcept -> std::span<const std::uint8_t> {
        return payload_;
    }
    
    [[nodiscard]] auto size() const noexcept -> std::size_t {
        return payload_.size();
    }
    
    [[nodiscard]] auto empty() const noexcept -> bool {
        return payload_.empty();
    }
    
    // ───────────────────────────────────────────────────────────────────────
    // Mutators
    // ───────────────────────────────────────────────────────────────────────
    
    void set_urgency(Urgency u) noexcept {
        urgency_ = u;
    }
    
    void set_payload(std::vector<std::uint8_t> data) {
        payload_ = std::move(data);
    }
    
    void set_payload(std::string_view sv) {
        payload_.assign(sv.begin(), sv.end());
    }

private:
    std::vector<std::uint8_t> payload_;
    Urgency urgency_{Urgency::Green};
};


// ═══════════════════════════════════════════════════════════════════════════
// POLICY-BASED STRATEGY PATTERN
// ═══════════════════════════════════════════════════════════════════════════
//
// Instead of virtual dispatch (runtime overhead), we use templates to select
// behavior at compile time. The "policy" is a type parameter that provides
// specific implementations.
//
// Benefits over traditional Strategy:
// • Zero runtime overhead (no vtable, no indirect calls)
// • Policies are inlined by compiler
// • Type safety — incompatible policies caught at compile time
// • No heap allocation for strategy objects
//
// ═══════════════════════════════════════════════════════════════════════════

// ───────────────────────────────────────────────────────────────────────────
// Policy Concepts (C++20)
// ───────────────────────────────────────────────────────────────────────────

/// Concept for packet dispatch policies.
template<typename P>
concept DispatchPolicy = requires(P policy, const Packet& pkt) {
    { policy.on_normal(pkt) } -> std::same_as<void>;
    { policy.on_urgent(pkt) } -> std::same_as<void>;
};

/// Concept for logging policies.
template<typename P>
concept LoggingPolicy = requires(P policy, std::string_view msg) {
    { policy.log(msg) } -> std::same_as<void>;
};


// ───────────────────────────────────────────────────────────────────────────
// Dispatch Policies
// ───────────────────────────────────────────────────────────────────────────

/// Default dispatch policy — logs to console.
struct ConsoleDispatchPolicy {
    void on_normal(const Packet& pkt) const {
        fmt::print("[NORMAL] Payload: {}\n", pkt.payload_as_string());
    }
    
    void on_urgent(const Packet& pkt) const {
        fmt::print("[URGENT RED] Alert! Payload: {}\n", pkt.payload_as_string());
    }
};

/// Silent dispatch policy — no output.
struct SilentDispatchPolicy {
    void on_normal(const Packet&) const noexcept {}
    void on_urgent(const Packet&) const noexcept {}
};

/// Callback dispatch policy — invokes user-provided callbacks.
class CallbackDispatchPolicy {
public:
    using Callback = std::function<void(const Packet&)>;
    
    // Rule of Six: All Default (std::function handles own resources)
    CallbackDispatchPolicy() = default;
    ~CallbackDispatchPolicy() = default;
    CallbackDispatchPolicy(const CallbackDispatchPolicy&) = default;
    CallbackDispatchPolicy& operator=(const CallbackDispatchPolicy&) = default;
    CallbackDispatchPolicy(CallbackDispatchPolicy&&) noexcept = default;
    CallbackDispatchPolicy& operator=(CallbackDispatchPolicy&&) noexcept = default;
    
    CallbackDispatchPolicy(Callback on_normal, Callback on_urgent)
        : on_normal_{std::move(on_normal)}
        , on_urgent_{std::move(on_urgent)}
    {}
    
    void on_normal(const Packet& pkt) const {
        if (on_normal_) on_normal_(pkt);
    }
    
    void on_urgent(const Packet& pkt) const {
        if (on_urgent_) on_urgent_(pkt);
    }

private:
    Callback on_normal_;
    Callback on_urgent_;
};


// ───────────────────────────────────────────────────────────────────────────
// Logging Policies
// ───────────────────────────────────────────────────────────────────────────

/// Console logging policy.
struct ConsoleLoggingPolicy {
    void log(std::string_view msg) const {
        fmt::print("{}\n", msg);
    }
};

/// Silent logging policy (no-op).
struct SilentLoggingPolicy {
    void log(std::string_view) const noexcept {}
};


// ───────────────────────────────────────────────────────────────────────────
// Protocol Dispatcher (Policy-Based)
// ───────────────────────────────────────────────────────────────────────────

/// Packet dispatcher using compile-time policy selection.
///
/// @tparam DispatchPolicyT Policy for handling normal/urgent packets
/// @tparam LoggingPolicyT Policy for logging
///
/// @par Example
/// @code
/// // Console dispatcher
/// PacketDispatcher<ConsoleDispatchPolicy> dispatcher;
/// dispatcher.dispatch(packet);
///
/// // Custom callback dispatcher
/// PacketDispatcher<CallbackDispatchPolicy> custom{
///     CallbackDispatchPolicy{
///         [](const Packet& p) { handle_normal(p); },
///         [](const Packet& p) { handle_urgent(p); }
///     }
/// };
/// @endcode
template<DispatchPolicy DispatchPolicyT = ConsoleDispatchPolicy,
         LoggingPolicy LoggingPolicyT = ConsoleLoggingPolicy>
class PacketDispatcher {
public:
    // ───────────────────────────────────────────────────────────────────────
    // RULE OF SIX: All Defaulted
    // Policies are value types or have correct special members
    // ───────────────────────────────────────────────────────────────────────
    
    PacketDispatcher() = default;
    ~PacketDispatcher() = default;
    PacketDispatcher(const PacketDispatcher&) = default;
    PacketDispatcher& operator=(const PacketDispatcher&) = default;
    PacketDispatcher(PacketDispatcher&&) noexcept = default;
    PacketDispatcher& operator=(PacketDispatcher&&) noexcept = default;
    
    /// Construct with custom policies.
    explicit PacketDispatcher(DispatchPolicyT dispatch, 
                              LoggingPolicyT logging = LoggingPolicyT{})
        : dispatch_policy_{std::move(dispatch)}
        , logging_policy_{std::move(logging)}
    {}
    
    // ───────────────────────────────────────────────────────────────────────
    // Dispatch Interface
    // ───────────────────────────────────────────────────────────────────────
    
    /// Dispatch packet based on urgency.
    void dispatch(const Packet& pkt) const {
        logging_policy_.log(fmt::format("Dispatching packet, urgency={}", 
                                        to_string(pkt.urgency())));
        
        switch (pkt.urgency()) {
            case Urgency::Red:
            case Urgency::Yellow:
                dispatch_policy_.on_urgent(pkt);
                break;
            case Urgency::Green:
            default:
                dispatch_policy_.on_normal(pkt);
                break;
        }
    }
    
    /// Access dispatch policy (for configuration).
    [[nodiscard]] auto dispatch_policy() const noexcept -> const DispatchPolicyT& {
        return dispatch_policy_;
    }
    
    /// Access logging policy (for configuration).
    [[nodiscard]] auto logging_policy() const noexcept -> const LoggingPolicyT& {
        return logging_policy_;
    }

private:
    DispatchPolicyT dispatch_policy_;
    LoggingPolicyT logging_policy_;
};


// ───────────────────────────────────────────────────────────────────────────
// Type Aliases for Common Configurations
// ───────────────────────────────────────────────────────────────────────────

/// Default dispatcher with console output.
using DefaultDispatcher = PacketDispatcher<ConsoleDispatchPolicy, ConsoleLoggingPolicy>;

/// Silent dispatcher (no output).
using SilentDispatcher = PacketDispatcher<SilentDispatchPolicy, SilentLoggingPolicy>;

/// Custom callback dispatcher.
using CallbackDispatcher = PacketDispatcher<CallbackDispatchPolicy, SilentLoggingPolicy>;


// ═══════════════════════════════════════════════════════════════════════════
// TRADITIONAL STRATEGY INTERFACE (for runtime polymorphism when needed)
// ═══════════════════════════════════════════════════════════════════════════

/// Abstract handler interface for runtime polymorphism.
/// Use when policies must be swapped at runtime or determined dynamically.
class IPacketHandler {
public:
    virtual ~IPacketHandler() = default;
    
    virtual void on_normal(const Packet& pkt) = 0;
    virtual void on_urgent(const Packet& pkt) = 0;
    
    // Non-copyable, non-movable (interface class)
    IPacketHandler(const IPacketHandler&) = delete;
    IPacketHandler& operator=(const IPacketHandler&) = delete;
    IPacketHandler(IPacketHandler&&) = delete;
    IPacketHandler& operator=(IPacketHandler&&) = delete;

protected:
    IPacketHandler() = default;
};


// ═══════════════════════════════════════════════════════════════════════════
// ProtocolAPI — High-Level API
// ═══════════════════════════════════════════════════════════════════════════

/// High-level protocol API for packet creation and dispatch.
class ProtocolAPI {
public:
    // Rule of Six: All Default
    ProtocolAPI() = default;
    ~ProtocolAPI() = default;
    ProtocolAPI(const ProtocolAPI&) = default;
    ProtocolAPI& operator=(const ProtocolAPI&) = default;
    ProtocolAPI(ProtocolAPI&&) noexcept = default;
    ProtocolAPI& operator=(ProtocolAPI&&) noexcept = default;
    
    /// Create packet from string and urgency.
    [[nodiscard]] auto make_packet(std::string_view data, Urgency urgency) const 
        -> Packet 
    {
        return Packet::from_string(data, urgency);
    }
    
    /// Dispatch packet to handler (traditional strategy).
    void dispatch(const Packet& pkt, IPacketHandler& handler) const {
        switch (pkt.urgency()) {
            case Urgency::Red:
            case Urgency::Yellow:
                handler.on_urgent(pkt);
                break;
            default:
                handler.on_normal(pkt);
        }
    }
    
    /// Dispatch using policy-based dispatcher.
    template<DispatchPolicy D, LoggingPolicy L>
    void dispatch(const Packet& pkt, const PacketDispatcher<D, L>& dispatcher) const {
        dispatcher.dispatch(pkt);
    }
};

}  // namespace protocol
