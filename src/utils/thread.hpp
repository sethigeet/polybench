#pragma once

#include <string_view>

namespace utils::thread {
bool pin_current_thread_to_cpu(int cpu, std::string_view role) noexcept;
}
