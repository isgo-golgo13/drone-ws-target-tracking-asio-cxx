#pragma once

/// @file retry.hpp
/// @brief Retry mechanism with policy-based backoff strategies.
///
/// Demonstrates:
/// - Policy-based design for backoff strategies
/// - Asio coroutine integration
/// - Rule of Six for stateful retry contexts
/// - Perfect forwarding for operation execution

#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <limits>
#include <optional>
#include <random>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/steady_timer.hpp>

#include <fmt/core.h>

namespace protocol::retry {

namespace asio = boost::asio;
using namespace std::chrono_literals;


// ═══════════════════════════════════════════════════════════════════════════
// Duration Utilities
// ═══════════════════════════════════════════════════════════════════════════

using Duration = std::chrono::milliseconds;

constexpr Duration kDefaultInitialDelay = 100ms;
constexpr Duration kDefaultMaxDelay = 30s;
constexpr std::size_t kDefaultMaxAttempts = 5;
constexpr double kDefaultMultiplier = 2.0;
constexpr double kDefaultJitterFactor = 0.1;


// ═══════════════════════════════════════════════════════════════════════════
// RetryConfig — Configuration Value Class
// ═══════════════════════════════════════════════════════════════════════════
//
// RULE OF SIX RATIONALE:
// • All members are trivially copyable or have correct special members
// • No resource management required
// • Compiler-generated operations are correct
//
// ═══════════════════════════════════════════════════════════════════════════

/// Retry configuration parameters.
struct RetryConfig {
    // Rule of Six: All Default (trivial aggregate)
    RetryConfig() = default;
    ~RetryConfig() = default;
    RetryConfig(const RetryConfig&) = default;
    RetryConfig& operator=(const RetryConfig&) = default;
    RetryConfig(RetryConfig&&) noexcept = default;
    RetryConfig& operator=(RetryConfig&&) noexcept = default;
    
    /// Maximum number of retry attempts.
    std::size_t max_attempts{kDefaultMaxAttempts};
    
    /// Initial delay before first retry.
    Duration initial_delay{kDefaultInitialDelay};
    
    /// Maximum delay cap.
    Duration max_delay{kDefaultMaxDelay};
    
    /// Multiplier for exponential backoff.
    double multiplier{kDefaultMultiplier};
    
    /// Jitter factor (0.0 - 1.0) to randomize delays.
    double jitter_factor{kDefaultJitterFactor};
    
    // Builder methods
    [[nodiscard]] auto with_max_attempts(std::size_t n) && -> RetryConfig {
        max_attempts = n;
        return std::move(*this);
    }
    
    [[nodiscard]] auto with_initial_delay(Duration d) && -> RetryConfig {
        initial_delay = d;
        return std::move(*this);
    }
    
    [[nodiscard]] auto with_max_delay(Duration d) && -> RetryConfig {
        max_delay = d;
        return std::move(*this);
    }
    
    [[nodiscard]] auto with_multiplier(double m) && -> RetryConfig {
        multiplier = m;
        return std::move(*this);
    }
    
    [[nodiscard]] auto with_jitter(double j) && -> RetryConfig {
        jitter_factor = j;
        return std::move(*this);
    }
};


// ═══════════════════════════════════════════════════════════════════════════
// BACKOFF POLICY CONCEPT
// ═══════════════════════════════════════════════════════════════════════════

/// Concept for backoff delay calculation policies.
template<typename P>
concept BackoffPolicy = requires(P policy, std::size_t attempt) {
    { policy.delay_for(attempt) } -> std::convertible_to<Duration>;
    { policy.max_attempts() } -> std::convertible_to<std::size_t>;
};


// ═══════════════════════════════════════════════════════════════════════════
// FixedBackoffPolicy — Constant Delay
// ═══════════════════════════════════════════════════════════════════════════

/// Fixed delay backoff — same delay for every retry.
class FixedBackoffPolicy {
public:
    // Rule of Six: All Default
    FixedBackoffPolicy() = default;
    ~FixedBackoffPolicy() = default;
    FixedBackoffPolicy(const FixedBackoffPolicy&) = default;
    FixedBackoffPolicy& operator=(const FixedBackoffPolicy&) = default;
    FixedBackoffPolicy(FixedBackoffPolicy&&) noexcept = default;
    FixedBackoffPolicy& operator=(FixedBackoffPolicy&&) noexcept = default;
    
    explicit FixedBackoffPolicy(Duration delay, std::size_t max_attempts = kDefaultMaxAttempts)
        : delay_{delay}
        , max_attempts_{max_attempts}
    {}
    
    [[nodiscard]] auto delay_for(std::size_t /*attempt*/) const noexcept -> Duration {
        return delay_;
    }
    
    [[nodiscard]] auto max_attempts() const noexcept -> std::size_t {
        return max_attempts_;
    }

private:
    Duration delay_{kDefaultInitialDelay};
    std::size_t max_attempts_{kDefaultMaxAttempts};
};


// ═══════════════════════════════════════════════════════════════════════════
// LinearBackoffPolicy — Linear Increase
// ═══════════════════════════════════════════════════════════════════════════

/// Linear backoff — delay increases linearly with each attempt.
class LinearBackoffPolicy {
public:
    // Rule of Six: All Default
    LinearBackoffPolicy() = default;
    ~LinearBackoffPolicy() = default;
    LinearBackoffPolicy(const LinearBackoffPolicy&) = default;
    LinearBackoffPolicy& operator=(const LinearBackoffPolicy&) = default;
    LinearBackoffPolicy(LinearBackoffPolicy&&) noexcept = default;
    LinearBackoffPolicy& operator=(LinearBackoffPolicy&&) noexcept = default;
    
    explicit LinearBackoffPolicy(Duration initial, 
                                  Duration increment,
                                  Duration max_delay = kDefaultMaxDelay,
                                  std::size_t max_attempts = kDefaultMaxAttempts)
        : initial_{initial}
        , increment_{increment}
        , max_delay_{max_delay}
        , max_attempts_{max_attempts}
    {}
    
    [[nodiscard]] auto delay_for(std::size_t attempt) const noexcept -> Duration {
        auto delay = initial_ + (increment_ * attempt);
        return std::min(delay, max_delay_);
    }
    
    [[nodiscard]] auto max_attempts() const noexcept -> std::size_t {
        return max_attempts_;
    }

private:
    Duration initial_{kDefaultInitialDelay};
    Duration increment_{100ms};
    Duration max_delay_{kDefaultMaxDelay};
    std::size_t max_attempts_{kDefaultMaxAttempts};
};


// ═══════════════════════════════════════════════════════════════════════════
// ExponentialBackoffPolicy — Exponential Increase with Jitter
// ═══════════════════════════════════════════════════════════════════════════
//
// RULE OF SIX RATIONALE:
// • Contains mutable std::mt19937 (PRNG state)
// • PRNG is non-trivially copyable but has correct special members
// • Compiler-generated operations are correct
// • Mutable because delay_for may be called on const object
//
// ═══════════════════════════════════════════════════════════════════════════

/// Exponential backoff with optional jitter.
///
/// delay = min(initial * (multiplier ^ attempt) * (1 ± jitter), max_delay)
///
/// Jitter helps prevent thundering herd problems when multiple clients
/// retry simultaneously.
class ExponentialBackoffPolicy {
public:
    // ───────────────────────────────────────────────────────────────────────
    // RULE OF SIX: All Defaulted
    // std::mt19937 has correct copy/move semantics
    // ───────────────────────────────────────────────────────────────────────
    
    ExponentialBackoffPolicy() = default;
    ~ExponentialBackoffPolicy() = default;
    ExponentialBackoffPolicy(const ExponentialBackoffPolicy&) = default;
    ExponentialBackoffPolicy& operator=(const ExponentialBackoffPolicy&) = default;
    ExponentialBackoffPolicy(ExponentialBackoffPolicy&&) noexcept = default;
    ExponentialBackoffPolicy& operator=(ExponentialBackoffPolicy&&) noexcept = default;
    
    explicit ExponentialBackoffPolicy(const RetryConfig& config)
        : initial_{config.initial_delay}
        , max_delay_{config.max_delay}
        , multiplier_{config.multiplier}
        , jitter_factor_{config.jitter_factor}
        , max_attempts_{config.max_attempts}
        , rng_{std::random_device{}()}
    {}
    
    ExponentialBackoffPolicy(Duration initial,
                              Duration max_delay,
                              double multiplier,
                              double jitter_factor,
                              std::size_t max_attempts)
        : initial_{initial}
        , max_delay_{max_delay}
        , multiplier_{multiplier}
        , jitter_factor_{jitter_factor}
        , max_attempts_{max_attempts}
        , rng_{std::random_device{}()}
    {}
    
    /// Calculate delay for given attempt (0-indexed).
    [[nodiscard]] auto delay_for(std::size_t attempt) const -> Duration {
        // Base delay: initial * multiplier^attempt
        double base_ms = static_cast<double>(initial_.count());
        for (std::size_t i = 0; i < attempt; ++i) {
            base_ms *= multiplier_;
            // Prevent overflow
            if (base_ms > static_cast<double>(max_delay_.count())) {
                base_ms = static_cast<double>(max_delay_.count());
                break;
            }
        }
        
        // Apply jitter
        if (jitter_factor_ > 0.0) {
            std::uniform_real_distribution<double> dist(
                1.0 - jitter_factor_, 
                1.0 + jitter_factor_
            );
            base_ms *= dist(rng_);
        }
        
        // Clamp to max
        auto delay_ms = static_cast<std::int64_t>(base_ms);
        delay_ms = std::min(delay_ms, static_cast<std::int64_t>(max_delay_.count()));
        
        return Duration{delay_ms};
    }
    
    [[nodiscard]] auto max_attempts() const noexcept -> std::size_t {
        return max_attempts_;
    }

private:
    Duration initial_{kDefaultInitialDelay};
    Duration max_delay_{kDefaultMaxDelay};
    double multiplier_{kDefaultMultiplier};
    double jitter_factor_{kDefaultJitterFactor};
    std::size_t max_attempts_{kDefaultMaxAttempts};
    mutable std::mt19937 rng_{std::random_device{}()};
};


// ═══════════════════════════════════════════════════════════════════════════
// RetryResult — Operation Result with Attempt Metadata
// ═══════════════════════════════════════════════════════════════════════════

/// Result of a retry operation.
template<typename T>
struct RetryResult {
    std::optional<T> value;       ///< Result value (if successful)
    std::size_t attempts{0};      ///< Number of attempts made
    Duration total_delay{0};      ///< Total delay incurred
    std::exception_ptr last_error; ///< Last exception (if failed)
    
    [[nodiscard]] auto success() const noexcept -> bool {
        return value.has_value();
    }
    
    [[nodiscard]] auto failed() const noexcept -> bool {
        return !value.has_value();
    }
};

/// Specialization for void operations.
template<>
struct RetryResult<void> {
    bool succeeded{false};
    std::size_t attempts{0};
    Duration total_delay{0};
    std::exception_ptr last_error;
    
    [[nodiscard]] auto success() const noexcept -> bool {
        return succeeded;
    }
    
    [[nodiscard]] auto failed() const noexcept -> bool {
        return !succeeded;
    }
};


// ═══════════════════════════════════════════════════════════════════════════
// RetryExecutor — Coroutine-Based Retry Engine
// ═══════════════════════════════════════════════════════════════════════════
//
// RULE OF SIX RATIONALE:
// • Contains BackoffPolicyT (policy type, should be copyable/movable)
// • Contains asio::any_io_executor (handle type with correct semantics)
// • Compiler-generated operations delegate to members correctly
//
// ═══════════════════════════════════════════════════════════════════════════

/// Retry executor using Asio coroutines.
///
/// @tparam BackoffPolicyT Backoff delay calculation policy
///
/// @par Example
/// @code
/// RetryExecutor executor{ioc.get_executor(), ExponentialBackoffPolicy{config}};
///
/// auto result = co_await executor.execute([&]() -> asio::awaitable<int> {
///     co_return co_await async_operation();
/// });
///
/// if (result.success()) {
///     process(*result.value);
/// }
/// @endcode
template<BackoffPolicy BackoffPolicyT = ExponentialBackoffPolicy>
class RetryExecutor {
public:
    // ───────────────────────────────────────────────────────────────────────
    // RULE OF SIX: All Defaulted
    // ───────────────────────────────────────────────────────────────────────
    
    RetryExecutor() = default;
    ~RetryExecutor() = default;
    RetryExecutor(const RetryExecutor&) = default;
    RetryExecutor& operator=(const RetryExecutor&) = default;
    RetryExecutor(RetryExecutor&&) noexcept = default;
    RetryExecutor& operator=(RetryExecutor&&) noexcept = default;
    
    /// Construct with executor and backoff policy.
    explicit RetryExecutor(asio::any_io_executor executor, 
                           BackoffPolicyT policy = BackoffPolicyT{})
        : executor_{std::move(executor)}
        , policy_{std::move(policy)}
    {}
    
    // ───────────────────────────────────────────────────────────────────────
    // Execution Interface
    // ───────────────────────────────────────────────────────────────────────
    
    /// Execute operation with retries (returns value).
    template<typename F>
        requires std::invocable<F> && 
                 (!std::is_void_v<typename std::invoke_result_t<F>::value_type>)
    [[nodiscard]] auto execute(F&& operation) 
        -> asio::awaitable<RetryResult<typename std::invoke_result_t<F>::value_type>>
    {
        using ResultT = typename std::invoke_result_t<F>::value_type;
        RetryResult<ResultT> result;
        
        for (std::size_t attempt = 0; attempt < policy_.max_attempts(); ++attempt) {
            result.attempts = attempt + 1;
            
            try {
                result.value = co_await std::invoke(std::forward<F>(operation));
                co_return result;
            } catch (...) {
                result.last_error = std::current_exception();
                
                // Don't delay after last attempt
                if (attempt + 1 < policy_.max_attempts()) {
                    auto delay = policy_.delay_for(attempt);
                    result.total_delay += delay;
                    
                    asio::steady_timer timer{executor_, delay};
                    co_await timer.async_wait(asio::use_awaitable);
                }
            }
        }
        
        co_return result;
    }
    
    /// Execute void operation with retries.
    template<typename F>
        requires std::invocable<F> && 
                 std::is_void_v<typename std::invoke_result_t<F>::value_type>
    [[nodiscard]] auto execute(F&& operation) -> asio::awaitable<RetryResult<void>>
    {
        RetryResult<void> result;
        
        for (std::size_t attempt = 0; attempt < policy_.max_attempts(); ++attempt) {
            result.attempts = attempt + 1;
            
            try {
                co_await std::invoke(std::forward<F>(operation));
                result.succeeded = true;
                co_return result;
            } catch (...) {
                result.last_error = std::current_exception();
                
                if (attempt + 1 < policy_.max_attempts()) {
                    auto delay = policy_.delay_for(attempt);
                    result.total_delay += delay;
                    
                    asio::steady_timer timer{executor_, delay};
                    co_await timer.async_wait(asio::use_awaitable);
                }
            }
        }
        
        co_return result;
    }
    
    /// Execute with custom predicate for retryable errors.
    template<typename F, typename Predicate>
        requires std::invocable<F> && 
                 std::invocable<Predicate, std::exception_ptr>
    [[nodiscard]] auto execute_if(F&& operation, Predicate&& should_retry)
        -> asio::awaitable<RetryResult<typename std::invoke_result_t<F>::value_type>>
    {
        using ResultT = typename std::invoke_result_t<F>::value_type;
        RetryResult<ResultT> result;
        
        for (std::size_t attempt = 0; attempt < policy_.max_attempts(); ++attempt) {
            result.attempts = attempt + 1;
            
            try {
                if constexpr (std::is_void_v<ResultT>) {
                    co_await std::invoke(std::forward<F>(operation));
                    result.succeeded = true;
                } else {
                    result.value = co_await std::invoke(std::forward<F>(operation));
                }
                co_return result;
            } catch (...) {
                result.last_error = std::current_exception();
                
                // Check if error is retryable
                if (!std::invoke(std::forward<Predicate>(should_retry), result.last_error)) {
                    co_return result;  // Non-retryable, bail out
                }
                
                if (attempt + 1 < policy_.max_attempts()) {
                    auto delay = policy_.delay_for(attempt);
                    result.total_delay += delay;
                    
                    asio::steady_timer timer{executor_, delay};
                    co_await timer.async_wait(asio::use_awaitable);
                }
            }
        }
        
        co_return result;
    }
    
    // ───────────────────────────────────────────────────────────────────────
    // Accessors
    // ───────────────────────────────────────────────────────────────────────
    
    [[nodiscard]] auto policy() const noexcept -> const BackoffPolicyT& {
        return policy_;
    }

private:
    asio::any_io_executor executor_;
    BackoffPolicyT policy_;
};


// ───────────────────────────────────────────────────────────────────────────
// Type Aliases
// ───────────────────────────────────────────────────────────────────────────

/// Default retry executor with exponential backoff.
using DefaultRetryExecutor = RetryExecutor<ExponentialBackoffPolicy>;

/// Fixed delay retry executor.
using FixedRetryExecutor = RetryExecutor<FixedBackoffPolicy>;

/// Linear backoff retry executor.
using LinearRetryExecutor = RetryExecutor<LinearBackoffPolicy>;


// ───────────────────────────────────────────────────────────────────────────
// Factory Functions
// ───────────────────────────────────────────────────────────────────────────

/// Create exponential backoff executor.
[[nodiscard]] inline auto make_retry_executor(asio::any_io_executor executor,
                                               const RetryConfig& config = {})
    -> DefaultRetryExecutor
{
    return DefaultRetryExecutor{std::move(executor), ExponentialBackoffPolicy{config}};
}

/// Create fixed delay executor.
[[nodiscard]] inline auto make_fixed_retry_executor(asio::any_io_executor executor,
                                                     Duration delay,
                                                     std::size_t max_attempts = kDefaultMaxAttempts)
    -> FixedRetryExecutor
{
    return FixedRetryExecutor{std::move(executor), FixedBackoffPolicy{delay, max_attempts}};
}

}  // namespace protocol::retry
