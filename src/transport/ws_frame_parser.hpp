#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

/// Streaming RFC 6455 WebSocket frame parser and frame builder.
///
/// The parser is fed arbitrary-length byte chunks (e.g. from TLS decrypt output)
/// and yields zero or more complete frames per `feed()` call via a visitor
/// callback.  It correctly handles:
///   - frames split across multiple TCP segments
///   - multiple frames concatenated in a single chunk
///   - continuation (fragmented) frames
///
/// Frame building utilities produce masked client-to-server frames per RFC 6455.
///
/// Header-only so the compiler can inline the full parse→emit path.
class WsFrameParser {
 public:
  enum class Opcode : uint8_t {
    Continuation = 0x0,
    Text = 0x1,
    Binary = 0x2,
    // 0x3-0x7 reserved
    Close = 0x8,
    Ping = 0x9,
    Pong = 0xA,
    // 0xB-0xF reserved
  };

  struct Frame {
    Opcode opcode;
    bool fin;
    const uint8_t* payload;
    size_t payload_len;

    [[nodiscard]] std::string_view payload_view() const {
      return {reinterpret_cast<const char*>(payload), payload_len};
    }
  };

  WsFrameParser() = default;

  /// Feed raw decrypted data.  Calls `on_frame(const Frame&)` for each complete
  /// frame extracted from the stream.  It is safe for the visitor to throw; the
  /// parser state remains consistent.
  template <typename Visitor>
  void feed(const uint8_t* data, size_t len, Visitor&& on_frame);

  /// Reset parser state (e.g. on reconnect).
  inline void reset();

  static inline std::string build_text_frame(std::string_view payload);
  static inline std::string build_binary_frame(std::string_view payload);
  static inline std::string build_ping_frame(std::string_view payload = {});
  static inline std::string build_pong_frame(std::string_view payload = {});
  static inline std::string build_close_frame(uint16_t code = 1000, std::string_view reason = {});

  /// Maximum payload size we will accept (64 MB). Frames exceeding this are
  /// treated as a protocol error.
  static constexpr size_t kMaxPayloadSize = 64 * 1024 * 1024;

 private:
  // Three-phase state machine:
  //   1. ReadMinHeader  — accumulate first 2 bytes to determine extended header size
  //   2. ReadExtHeader  — accumulate remaining header (ext len + mask key)
  //   3. ReadPayload    — accumulate payload bytes
  enum class State : uint8_t {
    ReadMinHeader,
    ReadExtHeader,
    ReadPayload,
  };

  State state_ = State::ReadMinHeader;

  // Header accumulator (max header = 2 + 8 + 4 = 14 bytes)
  uint8_t header_buf_[14]{};
  size_t header_pos_ = 0;
  size_t header_total_ = 2;  // total header bytes needed (starts at 2)

  // Parsed header fields
  bool fin_ = false;
  Opcode opcode_ = Opcode::Text;
  bool masked_ = false;
  uint64_t payload_len_ = 0;
  uint8_t mask_key_[4]{};

  // Payload accumulation
  std::vector<uint8_t> payload_buf_;
  size_t payload_received_ = 0;

  // Fragmentation reassembly
  std::vector<uint8_t> fragment_buf_;
  Opcode fragment_opcode_ = Opcode::Text;
  bool in_fragment_ = false;

  inline void on_min_header_complete();
  inline void on_ext_header_complete();

  static inline std::string build_frame(Opcode opcode, const uint8_t* payload, size_t len);
  static inline std::array<uint8_t, 4> random_mask_key();
};

inline void WsFrameParser::reset() {
  state_ = State::ReadMinHeader;
  header_pos_ = 0;
  header_total_ = 2;
  payload_buf_.clear();
  payload_received_ = 0;
  fragment_buf_.clear();
  in_fragment_ = false;
  fin_ = false;
  masked_ = false;
  payload_len_ = 0;
}

inline void WsFrameParser::on_min_header_complete() {
  fin_ = (header_buf_[0] & 0x80) != 0;
  opcode_ = static_cast<Opcode>(header_buf_[0] & 0x0F);
  masked_ = (header_buf_[1] & 0x80) != 0;
  uint8_t len7 = header_buf_[1] & 0x7F;

  size_t ext_len_bytes = 0;
  if (len7 == 126) {
    ext_len_bytes = 2;
  } else if (len7 == 127) {
    ext_len_bytes = 8;
  }
  size_t mask_bytes = masked_ ? 4 : 0;
  header_total_ = 2 + ext_len_bytes + mask_bytes;

  if (header_total_ > 2) {
    state_ = State::ReadExtHeader;
  } else {
    payload_len_ = len7;
    state_ = State::ReadPayload;
    if (payload_len_ > 0) {
      payload_buf_.reserve(payload_len_);
    }
  }
}

inline void WsFrameParser::on_ext_header_complete() {
  uint8_t len7 = header_buf_[1] & 0x7F;
  size_t cursor = 2;

  if (len7 == 126) {
    payload_len_ = (static_cast<uint16_t>(header_buf_[cursor]) << 8) | header_buf_[cursor + 1];
    cursor += 2;
  } else if (len7 == 127) {
    payload_len_ = 0;
    for (int i = 0; i < 8; ++i) {
      payload_len_ = (payload_len_ << 8) | header_buf_[cursor + i];
    }
    cursor += 8;
  } else {
    // len7 < 126, but we're here because masked=true
    payload_len_ = len7;
  }

  if (payload_len_ > kMaxPayloadSize) {
    throw std::runtime_error("WebSocket frame payload too large");
  }

  if (masked_) {
    std::memcpy(mask_key_, header_buf_ + cursor, 4);
  }

  state_ = State::ReadPayload;
  if (payload_len_ > 0) {
    payload_buf_.reserve(static_cast<size_t>(payload_len_));
  }
}

inline std::array<uint8_t, 4> WsFrameParser::random_mask_key() {
  thread_local std::mt19937 rng{std::random_device{}()};
  uint32_t key = rng();
  std::array<uint8_t, 4> mask{};
  std::memcpy(mask.data(), &key, 4);
  return mask;
}

inline std::string WsFrameParser::build_frame(Opcode opcode, const uint8_t* payload, size_t len) {
  std::string frame;
  frame.reserve(2 + 8 + 4 + len);

  frame.push_back(static_cast<char>(0x80 | static_cast<uint8_t>(opcode)));

  if (len < 126) {
    frame.push_back(static_cast<char>(0x80 | static_cast<uint8_t>(len)));
  } else if (len <= 65535) {
    frame.push_back(static_cast<char>(0x80 | 126));
    frame.push_back(static_cast<char>((len >> 8) & 0xFF));
    frame.push_back(static_cast<char>(len & 0xFF));
  } else {
    frame.push_back(static_cast<char>(0x80 | 127));
    for (int i = 7; i >= 0; --i) {
      frame.push_back(static_cast<char>((len >> (i * 8)) & 0xFF));
    }
  }

  auto mask = random_mask_key();
  frame.append(reinterpret_cast<const char*>(mask.data()), 4);

  for (size_t i = 0; i < len; ++i) {
    frame.push_back(static_cast<char>(payload[i] ^ mask[i % 4]));
  }

  return frame;
}

inline std::string WsFrameParser::build_text_frame(std::string_view payload) {
  return build_frame(Opcode::Text, reinterpret_cast<const uint8_t*>(payload.data()),
                     payload.size());
}

inline std::string WsFrameParser::build_binary_frame(std::string_view payload) {
  return build_frame(Opcode::Binary, reinterpret_cast<const uint8_t*>(payload.data()),
                     payload.size());
}

inline std::string WsFrameParser::build_ping_frame(std::string_view payload) {
  return build_frame(Opcode::Ping, reinterpret_cast<const uint8_t*>(payload.data()),
                     payload.size());
}

inline std::string WsFrameParser::build_pong_frame(std::string_view payload) {
  return build_frame(Opcode::Pong, reinterpret_cast<const uint8_t*>(payload.data()),
                     payload.size());
}

inline std::string WsFrameParser::build_close_frame(uint16_t code, std::string_view reason) {
  std::vector<uint8_t> payload;
  payload.reserve(2 + reason.size());
  payload.push_back(static_cast<uint8_t>((code >> 8) & 0xFF));
  payload.push_back(static_cast<uint8_t>(code & 0xFF));
  payload.insert(payload.end(), reason.begin(), reason.end());
  return build_frame(Opcode::Close, payload.data(), payload.size());
}

template <typename Visitor>
void WsFrameParser::feed(const uint8_t* data, size_t len, Visitor&& on_frame) {
  size_t pos = 0;

  while (pos < len) {
    switch (state_) {
      case State::ReadMinHeader:
      case State::ReadExtHeader: {
        const size_t need = header_total_ - header_pos_;
        const size_t avail = len - pos;
        const size_t take = std::min(need, avail);
        std::memcpy(header_buf_ + header_pos_, data + pos, take);
        header_pos_ += take;
        pos += take;

        if (header_pos_ < header_total_) {
          return;  // need more data
        }

        if (state_ == State::ReadMinHeader) {
          on_min_header_complete();
          // on_min_header_complete() may have advanced to ReadExtHeader or ReadPayload
          if (state_ == State::ReadExtHeader) {
            continue;  // go read ext header bytes
          }
          // else: fell through to ReadPayload with payload_len_ possibly 0
        } else {
          on_ext_header_complete();
          // Now in ReadPayload
        }

        // If payload is empty, fall through to ReadPayload which will emit immediately
        break;
      }

      case State::ReadPayload: {
        if (payload_len_ > 0) {
          const size_t remaining = static_cast<size_t>(payload_len_) - payload_received_;
          const size_t avail = len - pos;
          const size_t take = std::min(remaining, avail);

          payload_buf_.insert(payload_buf_.end(), data + pos, data + pos + take);
          payload_received_ += take;
          pos += take;

          if (payload_received_ < payload_len_) {
            return;  // need more data
          }
        }
        // Unmask if needed (server-to-client frames are normally unmasked)
        if (masked_) {
          for (size_t i = 0; i < payload_buf_.size(); ++i) {
            payload_buf_[i] ^= mask_key_[i % 4];
          }
        }

        // Handle control frames (must not be fragmented per RFC 6455)
        bool is_control = (static_cast<uint8_t>(opcode_) & 0x08) != 0;

        if (is_control) {
          Frame frame{};
          frame.opcode = opcode_;
          frame.fin = true;
          frame.payload = payload_buf_.data();
          frame.payload_len = payload_buf_.size();
          on_frame(frame);
        } else if (opcode_ != Opcode::Continuation && !fin_) {
          // First fragment of a fragmented message
          in_fragment_ = true;
          fragment_opcode_ = opcode_;
          fragment_buf_.assign(payload_buf_.begin(), payload_buf_.end());
        } else if (opcode_ == Opcode::Continuation) {
          // Middle or final continuation fragment
          fragment_buf_.insert(fragment_buf_.end(), payload_buf_.begin(), payload_buf_.end());
          if (fin_) {
            Frame frame{};
            frame.opcode = fragment_opcode_;
            frame.fin = true;
            frame.payload = fragment_buf_.data();
            frame.payload_len = fragment_buf_.size();
            on_frame(frame);
            fragment_buf_.clear();
            in_fragment_ = false;
          }
        } else {
          // Unfragmented frame (fin=true, opcode != continuation)
          Frame frame{};
          frame.opcode = opcode_;
          frame.fin = true;
          frame.payload = payload_buf_.data();
          frame.payload_len = payload_buf_.size();
          on_frame(frame);
        }

        // Reset for next frame
        state_ = State::ReadMinHeader;
        header_pos_ = 0;
        header_total_ = 2;
        payload_buf_.clear();
        payload_received_ = 0;
        break;
      }
    }
  }
}
