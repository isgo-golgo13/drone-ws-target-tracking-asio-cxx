#include "retry.hpp"

namespace protocol::retry {

// Explicit template instantiations for common retry executor configurations
template class RetryExecutor<ExponentialBackoffPolicy>;
template class RetryExecutor<FixedBackoffPolicy>;
template class RetryExecutor<LinearBackoffPolicy>;

}  // namespace protocol::retry
