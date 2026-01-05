#pragma once

#include <array>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>

template <size_t N>
struct FixedString {
  static constexpr size_t capacity = N;
  std::array<char, N + 1> data{};  // +1 for null terminator

  constexpr FixedString() noexcept = default;

  FixedString(std::string_view sv) noexcept {
    const size_t len = sv.size() < N ? sv.size() : N;
    std::memcpy(data.data(), sv.data(), len);
    data[len] = '\0';
  }

  FixedString(const char* str) noexcept : FixedString(std::string_view(str)) {}

  FixedString(const std::string& str) noexcept : FixedString(std::string_view(str)) {}

  FixedString& operator=(std::string_view sv) noexcept {
    const size_t len = sv.size() < N ? sv.size() : N;
    std::memcpy(data.data(), sv.data(), len);
    data[len] = '\0';
    return *this;
  }

  [[nodiscard]] constexpr operator std::string_view() const noexcept {
    return std::string_view(data.data());
  }

  [[nodiscard]] bool operator==(const FixedString& other) const noexcept {
    return std::string_view(*this) == std::string_view(other);
  }
  [[nodiscard]] bool operator==(std::string_view sv) const noexcept {
    return std::string_view(*this) == sv;
  }
  [[nodiscard]] bool operator!=(const FixedString& other) const noexcept {
    return !(*this == other);
  }
  [[nodiscard]] bool operator<(const FixedString& other) const noexcept {
    return std::string_view(*this) < std::string_view(other);
  }
  [[nodiscard]] constexpr const char* c_str() const noexcept { return data.data(); }
  [[nodiscard]] std::string str() const { return std::string(data.data()); }
  [[nodiscard]] constexpr bool empty() const noexcept { return data[0] == '\0'; }
};

namespace std {
template <size_t N>
struct hash<FixedString<N>> {
  size_t operator()(const FixedString<N>& fs) const noexcept {
    return std::hash<std::string_view>{}(std::string_view(fs));
  }
};

template <size_t N>
std::ostream& operator<<(std::ostream& os, const FixedString<N>& fs) {
  os << std::string_view(fs);
  return os;
}
}  // namespace std

#include <spdlog/fmt/fmt.h>
template <size_t N>
struct fmt::formatter<FixedString<N>> : fmt::formatter<std::string_view> {
  template <typename FormatContext>
  auto format(const FixedString<N>& fs, FormatContext& ctx) const {
    return fmt::formatter<std::string_view>::format(std::string_view(fs), ctx);
  }
};
