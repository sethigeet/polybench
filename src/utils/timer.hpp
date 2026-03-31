#pragma once
#include <chrono>
#include <iostream>

namespace utils::timer {

class ScopeTimer {
 public:
  explicit ScopeTimer(const std::string& name)
      : name_(name), start_(std::chrono::high_resolution_clock::now()) {}

  ~ScopeTimer() {
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start_);
    std::cout << name_ << " took " << duration.count() << " µs\n";
  }

 private:
  std::string name_;
  std::chrono::high_resolution_clock::time_point start_;
};

}  // namespace utils::timer
