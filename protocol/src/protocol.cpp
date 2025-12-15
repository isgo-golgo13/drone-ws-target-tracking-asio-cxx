#include "protocol.hpp"

namespace protocol {

// Explicit template instantiations for common dispatcher configurations
// This ensures the symbols are available at link time and reduces compile times

template class PacketDispatcher<ConsoleDispatchPolicy, ConsoleLoggingPolicy>;
template class PacketDispatcher<SilentDispatchPolicy, SilentLoggingPolicy>;
template class PacketDispatcher<CallbackDispatchPolicy, SilentLoggingPolicy>;

}  // namespace protocol
