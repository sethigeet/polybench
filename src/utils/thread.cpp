#include "thread.hpp"

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

#define LOGGER_NAME "Utils::Thread"
#include "logger.hpp"

namespace utils::thread {

bool pin_current_thread_to_cpu(int cpu, std::string_view role) noexcept {
  if (cpu < 0) return false;

#if defined(__linux__)
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(cpu, &cpuset);

  const int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
  if (rc != 0) {
    LOG_WARN("Failed to pin {} thread to CPU {}", role, cpu);
    return false;
  }

  LOG_INFO("Pinned {} thread to CPU {}", role, cpu);
  return true;
#else
  LOG_WARN("CPU affinity requested for {} thread, but platform support is unavailable", role);
  return false;
#endif
}

}  // namespace utils::thread
