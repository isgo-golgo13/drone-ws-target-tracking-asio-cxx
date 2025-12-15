# Drone WebSockets Target Tracking Client/Server w/ Retry C++23
TLS WebSocket Server, WebSocket Client and Torpedo Firing Application Protocol with Retry Backoff Algorithm using C++23, Boost.Beast C++ WebSockets API, Boost Asio Coroutines


Production-Grade TLS WebSocket Client/Server using progressive C++23 idioms:
- **Rule of Six** class design with comprehensive documentation
- **Policy-Oriented Strategy Patterns** for compile-time polymorphism
- **Perfect forwarding Factory Method Patterns** using variadic templates
- **Retry Protocol Integration** with exponential backoff
- **Asio Coroutines** (`asio::awaitable`) without Cobalt dependency

## Architecture

```
drone-ws-target-tracking-cxx-asio/
├── CMakeLists.txt              # Boost Beast + Asio (NO Cobalt)
├── README.md                   # Rule of Six masterclass documentation
├── scripts/gen-certs.sh        # TLS certificate generator
├── svckit/
│   └── include/svc_addr_config.hpp   # AddrConfig with Rule of Six (All Default)
├── protocol/
│   ├── include/protocol.hpp    # Policy-based Strategy pattern, Packet class
│   ├── include/retry.hpp       # Exponential backoff with policy design
│   └── src/                    # Template instantiations
├── ws-server/
│   ├── include/ws_server.hpp   # Rule of Six: Move-only pattern
│   └── src/ws_server.cpp       # std::exchange in move ops
├── ws-client/
│   ├── include/ws_client.hpp   # Rule of Six: Move-only + retry integration
│   └── src/ws_client.cpp       # std::exchange in move ops
└── src/main.cpp                # Orchestrator (Non-copyable, Non-movable)
```

## Key Patterns Provided
ClassRule of Six PatternRationaleTlsConfig, AddrConfig, PacketAll DefaultNo raw resourcesWSServer, WSClientMove-only (copy deleted)Unique ownership of sockets/SSLApplicationNon-copyable, Non-movableProcess-lifetime singletonIPacketHandlerNon-copyable, Non-movableAbstract interface

| Class                           | Rule of Six Pattern            | Rationale                       |
|---------------------------------|--------------------------------|---------------------------------|
| TlsConfig, AddrConfig, Packet   | All `default`                  | No raw resources                |
| WSServer, WSClient              | Move-only (copy deleted)       | Unique ownership of sockets/SSL |
| Application                     | Non-copyable, Non-movable      | Process-lifetime singleton      |
| IPacketHandler                  | Non-copyable, Non-movable      | Abstract interface              |


## Building

```bash
# Generate TLS certificates
mkdir -p certificates
mkcert -cert-file certificates/server.pem \
       -key-file certificates/server-key.pem \
       localhost 127.0.0.1 ::1

# Build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Run
./build/ws-server    # Terminal 1
./build/ws-client    # Terminal 2
```

---

# C++23 Masterclass: Rule of Six

## Overview

The **Rule of Six** extends the classic Rule of Five by explicitly considering the default constructor. For any class managing resources, you must consider all six special member functions:

| Function | Signature | Purpose |
|----------|-----------|---------|
| Default Constructor | `T()` | Initialize to valid empty/default state |
| Destructor | `~T()` | Release resources |
| Copy Constructor | `T(const T&)` | Deep copy resources |
| Copy Assignment | `T& operator=(const T&)` | Deep copy with self-assignment safety |
| Move Constructor | `T(T&&) noexcept` | Transfer ownership |
| Move Assignment | `T& operator=(T&&) noexcept` | Transfer with self-assignment safety |

## Decision Matrix: When to `= default`, `= delete`, or Define

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    RULE OF SIX DECISION MATRIX                              │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  Does your class manage resources (memory, handles, connections)?           │
│                                                                             │
│  NO ──► Use `= default` for all six                                         │
│         (Compiler generates correct trivial operations)                     │
│                                                                             │
│  YES ─► Continue to resource type analysis...                               │
│                                                                             │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  Is the resource UNIQUELY OWNED (file handle, raw pointer, socket)?         │
│                                                                             │
│  YES ──► Move-only semantics:                                               │
│          • Default ctor: Define (initialize to null/invalid state)          │
│          • Destructor: Define (release resource)                            │
│          • Copy ctor: `= delete`                                            │
│          • Copy assign: `= delete`                                          │
│          • Move ctor: Define with `std::exchange`                           │
│          • Move assign: Define with `std::exchange`                         │
│                                                                             │
│  NO ───► Is the resource SHARED (shared_ptr, reference counted)?            │
│                                                                             │
│          YES ──► Use `= default` (shared_ptr handles everything)            │
│                                                                             │
│          NO ──► Full value semantics (deep copy):                           │
│                 • Define all six explicitly                                 │
│                 • Copy operations perform deep copy                         │
│                 • Move operations transfer + nullify source                 │
│                                                                             │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  Should instances be NON-COPYABLE AND NON-MOVABLE (singletons, mutexes)?    │
│                                                                             │
│  YES ──► Delete all four copy/move operations:                              │
│          • Copy ctor: `= delete`                                            │
│          • Copy assign: `= delete`                                          │
│          • Move ctor: `= delete`                                            │
│          • Move assign: `= delete`                                          │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

## Pattern 1: Trivial Class (All Default)

Use when: Class contains only trivially copyable members or smart pointers.

```cpp
class Config {
public:
    // ═══════════════════════════════════════════════════════════════════
    // RULE OF SIX: All defaulted — no resource management needed
    // Compiler generates correct trivial operations for all members
    // ═══════════════════════════════════════════════════════════════════
    
    Config() = default;
    ~Config() = default;
    
    Config(const Config&) = default;
    Config& operator=(const Config&) = default;
    
    Config(Config&&) noexcept = default;
    Config& operator=(Config&&) noexcept = default;

private:
    std::string host_;          // Handles own memory
    uint16_t port_{0};          // Trivially copyable
    std::shared_ptr<SSL_CTX> ssl_ctx_;  // Reference counted
};
```

## Pattern 2: Move-Only Resource (Unique Ownership)

Use when: Class owns a unique resource that cannot be shared.

```cpp
class Connection {
public:
    // ═══════════════════════════════════════════════════════════════════
    // RULE OF SIX: Move-only — unique ownership of socket
    // 
    // • Default ctor: Establish null/invalid state
    // • Destructor: Close socket if valid
    // • Copy ops: DELETED — socket cannot be duplicated
    // • Move ops: Transfer ownership using std::exchange
    // ═══════════════════════════════════════════════════════════════════
    
    // Default constructor — initialize to invalid state
    Connection() noexcept : socket_fd_{-1} {}
    
    // Destructor — release resource
    ~Connection() {
        if (socket_fd_ >= 0) {
            ::close(socket_fd_);
        }
    }
    
    // Copy operations — DELETED (unique ownership)
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    
    // Move constructor — transfer ownership
    Connection(Connection&& other) noexcept
        : socket_fd_{std::exchange(other.socket_fd_, -1)}  // Safe transfer
    {}
    
    // Move assignment — transfer with cleanup
    Connection& operator=(Connection&& other) noexcept {
        if (this != &other) {  // Self-assignment check
            // Release current resource
            if (socket_fd_ >= 0) {
                ::close(socket_fd_);
            }
            // Transfer ownership
            socket_fd_ = std::exchange(other.socket_fd_, -1);
        }
        return *this;
    }

private:
    int socket_fd_;
};
```

## Pattern 3: Non-Copyable, Non-Movable (Fixed Resource)

Use when: Resource is tied to identity (mutex, singleton, hardware handle).

```cpp
class DatabasePool {
public:
    // ═══════════════════════════════════════════════════════════════════
    // RULE OF SIX: Non-copyable, non-movable
    // 
    // Connection pool cannot be copied or moved — it manages thread-local
    // state and connection affinity that would be invalidated by transfer.
    // ═══════════════════════════════════════════════════════════════════
    
    explicit DatabasePool(size_t pool_size);
    ~DatabasePool();
    
    // ALL copy/move operations DELETED
    DatabasePool(const DatabasePool&) = delete;
    DatabasePool& operator=(const DatabasePool&) = delete;
    DatabasePool(DatabasePool&&) = delete;
    DatabasePool& operator=(DatabasePool&&) = delete;

private:
    std::vector<ConnectionHandle> connections_;
    std::mutex mutex_;
};
```

## Why `std::exchange` in Move Operations?

`std::exchange` provides exception-safe, single-expression ownership transfer:

```cpp
// WITHOUT std::exchange — two statements, source left in unknown state
Connection(Connection&& other) noexcept
    : socket_fd_{other.socket_fd_}
{
    other.socket_fd_ = -1;  // Must remember to nullify!
}

// WITH std::exchange — single expression, self-documenting
Connection(Connection&& other) noexcept
    : socket_fd_{std::exchange(other.socket_fd_, -1)}  // Atomic swap
{}
```

Advantages:
1. **Single expression** — cannot forget to nullify source
2. **Exception safe** — no partial state if interrupted
3. **Self-documenting** — clearly shows ownership transfer
4. **Works in initializer lists** — clean member initialization

---

# Policy-Based Strategy Pattern

Traditional Strategy uses virtual dispatch (runtime polymorphism). Policy-based design uses templates (compile-time polymorphism) for zero-overhead abstraction.

## Traditional Strategy (Virtual Dispatch)

```cpp
// Runtime overhead: vtable lookup + indirect call
struct PacketHandler {
    virtual ~PacketHandler() = default;
    virtual void on_packet(const Packet& p) = 0;
};

class Connection {
    std::unique_ptr<PacketHandler> handler_;
public:
    void receive(const Packet& p) {
        handler_->on_packet(p);  // Indirect call
    }
};
```

## Policy-Based Strategy (Compile-Time)

```cpp
// Zero overhead: inline expansion at compile time
template<typename DispatchPolicy>
class Connection {
    DispatchPolicy policy_;  // No pointer, no vtable
public:
    void receive(const Packet& p) {
        policy_.on_packet(p);  // Direct call, inlined
    }
};

// Policies
struct LoggingDispatch {
    void on_packet(const Packet& p) {
        log("Received: {}", p.payload);
    }
};

struct AlertDispatch {
    void on_packet(const Packet& p) {
        if (p.urgency == Urgency::Red) alert();
    }
};

// Usage — type encodes behavior
Connection<LoggingDispatch> debug_conn;
Connection<AlertDispatch> prod_conn;
```

---

# Perfect Forwarding Factory Methods

Factory methods using variadic templates and perfect forwarding:

```cpp
class WSServer {
public:
    // ═══════════════════════════════════════════════════════════════════
    // Perfect forwarding factory — constructs in-place, zero copies
    // 
    // Args&&... preserves value category (lvalue/rvalue) of each argument
    // std::forward<Args>(args)... forwards with original category
    // ═══════════════════════════════════════════════════════════════════
    
    template<typename... Args>
    [[nodiscard]] static auto create(Args&&... args) 
        -> std::unique_ptr<WSServer>
    {
        // Private ctor called via unique_ptr
        return std::unique_ptr<WSServer>(
            new WSServer(std::forward<Args>(args)...)
        );
    }
    
    // Alternative: return shared_ptr
    template<typename... Args>
    [[nodiscard]] static auto create_shared(Args&&... args)
        -> std::shared_ptr<WSServer>
    {
        // Uses allocate_shared optimization
        return std::make_shared<WSServer>(std::forward<Args>(args)...);
    }

private:
    // Constructor is private — must use factory
    WSServer(boost::asio::io_context& ioc, const AddrConfig& cfg);
};

// Usage
auto server = WSServer::create(ioc, config);
```

---

# Retry Mechanism with Exponential Backoff

```cpp
template<typename RetryPolicy = ExponentialBackoff>
class RetryExecutor {
public:
    template<typename Awaitable>
    auto execute(Awaitable&& operation) -> asio::awaitable</*result*/> {
        for (size_t attempt = 0; attempt < policy_.max_attempts(); ++attempt) {
            try {
                co_return co_await std::forward<Awaitable>(operation);
            } catch (const std::exception& e) {
                if (attempt + 1 >= policy_.max_attempts()) throw;
                
                auto delay = policy_.delay_for(attempt);
                co_await asio::steady_timer(executor_, delay).async_wait(use_awaitable);
            }
        }
    }

private:
    RetryPolicy policy_;
};
```

---

## License

MIT
