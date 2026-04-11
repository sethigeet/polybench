#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "transport/ws_frame_parser.hpp"

// ---------------------------------------------------------------------------
// Helpers — build raw server-side (unmasked) frames for feeding into the parser
// ---------------------------------------------------------------------------
namespace {

std::string make_server_frame(WsFrameParser::Opcode opcode, std::string_view payload,
                              bool fin = true) {
  std::string frame;
  uint8_t byte0 = static_cast<uint8_t>(opcode);
  if (fin) byte0 |= 0x80;
  frame.push_back(static_cast<char>(byte0));

  // No mask bit (server → client)
  if (payload.size() < 126) {
    frame.push_back(static_cast<char>(payload.size()));
  } else if (payload.size() <= 65535) {
    frame.push_back(static_cast<char>(126));
    frame.push_back(static_cast<char>((payload.size() >> 8) & 0xFF));
    frame.push_back(static_cast<char>(payload.size() & 0xFF));
  } else {
    frame.push_back(static_cast<char>(127));
    for (int i = 7; i >= 0; --i) {
      frame.push_back(static_cast<char>((payload.size() >> (i * 8)) & 0xFF));
    }
  }

  frame.append(payload);
  return frame;
}

struct CapturedFrame {
  WsFrameParser::Opcode opcode;
  bool fin;
  std::string payload;
};

std::vector<CapturedFrame> feed_all(WsFrameParser& parser, std::string_view data) {
  std::vector<CapturedFrame> frames;
  parser.feed(reinterpret_cast<const uint8_t*>(data.data()), data.size(),
              [&](const WsFrameParser::Frame& f) {
                frames.push_back({f.opcode, f.fin, std::string(f.payload_view())});
              });
  return frames;
}

}  // namespace

TEST(WsFrameParserTest, ParseSingleTextFrame) {
  WsFrameParser parser;
  auto raw = make_server_frame(WsFrameParser::Opcode::Text, "hello");
  auto frames = feed_all(parser, raw);

  ASSERT_EQ(frames.size(), 1);
  EXPECT_EQ(frames[0].opcode, WsFrameParser::Opcode::Text);
  EXPECT_TRUE(frames[0].fin);
  EXPECT_EQ(frames[0].payload, "hello");
}

TEST(WsFrameParserTest, ParseSingleBinaryFrame) {
  WsFrameParser parser;
  std::string payload{'\x00', '\x01', '\x02', '\x03'};
  auto raw = make_server_frame(WsFrameParser::Opcode::Binary, payload);
  auto frames = feed_all(parser, raw);

  ASSERT_EQ(frames.size(), 1);
  EXPECT_EQ(frames[0].opcode, WsFrameParser::Opcode::Binary);
  EXPECT_EQ(frames[0].payload, payload);
}

TEST(WsFrameParserTest, ParseEmptyPayloadFrame) {
  WsFrameParser parser;
  auto raw = make_server_frame(WsFrameParser::Opcode::Text, "");
  auto frames = feed_all(parser, raw);

  ASSERT_EQ(frames.size(), 1);
  EXPECT_EQ(frames[0].opcode, WsFrameParser::Opcode::Text);
  EXPECT_EQ(frames[0].payload, "");
}

TEST(WsFrameParserTest, ParsePingFrame) {
  WsFrameParser parser;
  auto raw = make_server_frame(WsFrameParser::Opcode::Ping, "ping-data");
  auto frames = feed_all(parser, raw);

  ASSERT_EQ(frames.size(), 1);
  EXPECT_EQ(frames[0].opcode, WsFrameParser::Opcode::Ping);
  EXPECT_EQ(frames[0].payload, "ping-data");
}

TEST(WsFrameParserTest, ParsePongFrame) {
  WsFrameParser parser;
  auto raw = make_server_frame(WsFrameParser::Opcode::Pong, "");
  auto frames = feed_all(parser, raw);

  ASSERT_EQ(frames.size(), 1);
  EXPECT_EQ(frames[0].opcode, WsFrameParser::Opcode::Pong);
}

TEST(WsFrameParserTest, ParseCloseFrame) {
  WsFrameParser parser;
  // Close frame with status code 1000
  std::string close_payload;
  close_payload.push_back(static_cast<char>(0x03));  // 1000 >> 8
  close_payload.push_back(static_cast<char>(0xE8));  // 1000 & 0xFF
  close_payload.append("normal closure");

  auto raw = make_server_frame(WsFrameParser::Opcode::Close, close_payload);
  auto frames = feed_all(parser, raw);

  ASSERT_EQ(frames.size(), 1);
  EXPECT_EQ(frames[0].opcode, WsFrameParser::Opcode::Close);
  EXPECT_EQ(frames[0].payload, close_payload);
}

// ---------------------------------------------------------------------------
// Extended length (16-bit)
// ---------------------------------------------------------------------------

TEST(WsFrameParserTest, Parse16BitLengthFrame) {
  WsFrameParser parser;
  std::string payload(200, 'A');  // > 125 bytes, triggers 16-bit length
  auto raw = make_server_frame(WsFrameParser::Opcode::Text, payload);
  auto frames = feed_all(parser, raw);

  ASSERT_EQ(frames.size(), 1);
  EXPECT_EQ(frames[0].payload.size(), 200);
  EXPECT_EQ(frames[0].payload, payload);
}

// ---------------------------------------------------------------------------
// Extended length (64-bit)
// ---------------------------------------------------------------------------

TEST(WsFrameParserTest, Parse64BitLengthFrame) {
  WsFrameParser parser;
  std::string payload(70000, 'B');  // > 65535 bytes, triggers 64-bit length
  auto raw = make_server_frame(WsFrameParser::Opcode::Binary, payload);
  auto frames = feed_all(parser, raw);

  ASSERT_EQ(frames.size(), 1);
  EXPECT_EQ(frames[0].payload.size(), 70000);
  EXPECT_EQ(frames[0].payload, payload);
}

// ---------------------------------------------------------------------------
// Multiple frames concatenated in one chunk
// ---------------------------------------------------------------------------

TEST(WsFrameParserTest, ParseMultipleConcatenatedFrames) {
  WsFrameParser parser;
  std::string combined;
  combined += make_server_frame(WsFrameParser::Opcode::Text, "first");
  combined += make_server_frame(WsFrameParser::Opcode::Text, "second");
  combined += make_server_frame(WsFrameParser::Opcode::Text, "third");

  auto frames = feed_all(parser, combined);

  ASSERT_EQ(frames.size(), 3);
  EXPECT_EQ(frames[0].payload, "first");
  EXPECT_EQ(frames[1].payload, "second");
  EXPECT_EQ(frames[2].payload, "third");
}

// ---------------------------------------------------------------------------
// Frame split across multiple feed() calls (TCP segmentation)
// ---------------------------------------------------------------------------

TEST(WsFrameParserTest, ParseFrameSplitByteByByte) {
  WsFrameParser parser;
  auto raw = make_server_frame(WsFrameParser::Opcode::Text, "split-test");
  std::vector<CapturedFrame> frames;

  // Feed one byte at a time
  for (size_t i = 0; i < raw.size(); ++i) {
    parser.feed(reinterpret_cast<const uint8_t*>(raw.data() + i), 1,
                [&](const WsFrameParser::Frame& f) {
                  frames.push_back({f.opcode, f.fin, std::string(f.payload_view())});
                });
  }

  ASSERT_EQ(frames.size(), 1);
  EXPECT_EQ(frames[0].payload, "split-test");
}

TEST(WsFrameParserTest, ParseFrameSplitInHeader) {
  WsFrameParser parser;
  std::string payload(200, 'X');  // 16-bit length header
  auto raw = make_server_frame(WsFrameParser::Opcode::Text, payload);

  // Split right in the middle of the 16-bit length field (after byte 2, before byte 3)
  std::vector<CapturedFrame> frames;
  auto feeder = [&](const uint8_t* d, size_t n) {
    parser.feed(d, n, [&](const WsFrameParser::Frame& f) {
      frames.push_back({f.opcode, f.fin, std::string(f.payload_view())});
    });
  };

  feeder(reinterpret_cast<const uint8_t*>(raw.data()), 3);  // opcode + len7 + 1 ext byte
  EXPECT_EQ(frames.size(), 0);                              // not complete yet
  feeder(reinterpret_cast<const uint8_t*>(raw.data() + 3), raw.size() - 3);  // rest
  ASSERT_EQ(frames.size(), 1);
  EXPECT_EQ(frames[0].payload, payload);
}

TEST(WsFrameParserTest, ParseFrameSplitInPayload) {
  WsFrameParser parser;
  auto raw = make_server_frame(WsFrameParser::Opcode::Text, "abcdef");

  std::vector<CapturedFrame> frames;
  auto feeder = [&](const uint8_t* d, size_t n) {
    parser.feed(d, n, [&](const WsFrameParser::Frame& f) {
      frames.push_back({f.opcode, f.fin, std::string(f.payload_view())});
    });
  };

  // Header (2 bytes) + first 3 payload bytes
  feeder(reinterpret_cast<const uint8_t*>(raw.data()), 5);
  EXPECT_EQ(frames.size(), 0);
  // Last 3 payload bytes
  feeder(reinterpret_cast<const uint8_t*>(raw.data() + 5), raw.size() - 5);
  ASSERT_EQ(frames.size(), 1);
  EXPECT_EQ(frames[0].payload, "abcdef");
}

// ---------------------------------------------------------------------------
// Fragmented messages (continuation frames)
// ---------------------------------------------------------------------------

TEST(WsFrameParserTest, ParseFragmentedMessage) {
  WsFrameParser parser;
  std::string combined;
  // Fragment 1: opcode=Text, fin=false
  combined += make_server_frame(WsFrameParser::Opcode::Text, "Hello ", false);
  // Fragment 2: opcode=Continuation, fin=false
  combined += make_server_frame(WsFrameParser::Opcode::Continuation, "Wo", false);
  // Fragment 3: opcode=Continuation, fin=true
  combined += make_server_frame(WsFrameParser::Opcode::Continuation, "rld", true);

  auto frames = feed_all(parser, combined);

  // Should yield one reassembled frame
  ASSERT_EQ(frames.size(), 1);
  EXPECT_EQ(frames[0].opcode, WsFrameParser::Opcode::Text);
  EXPECT_TRUE(frames[0].fin);
  EXPECT_EQ(frames[0].payload, "Hello World");
}

TEST(WsFrameParserTest, ControlFrameInterleavesDuringFragmentation) {
  WsFrameParser parser;
  std::string combined;
  // Fragment 1
  combined += make_server_frame(WsFrameParser::Opcode::Text, "part1", false);
  // Control frame (ping) in the middle — allowed by RFC 6455
  combined += make_server_frame(WsFrameParser::Opcode::Ping, "keepalive");
  // Fragment 2 (final)
  combined += make_server_frame(WsFrameParser::Opcode::Continuation, "part2", true);

  auto frames = feed_all(parser, combined);

  // Expect: ping delivered immediately, then the reassembled text frame
  ASSERT_EQ(frames.size(), 2);
  EXPECT_EQ(frames[0].opcode, WsFrameParser::Opcode::Ping);
  EXPECT_EQ(frames[0].payload, "keepalive");
  EXPECT_EQ(frames[1].opcode, WsFrameParser::Opcode::Text);
  EXPECT_EQ(frames[1].payload, "part1part2");
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------

TEST(WsFrameParserTest, ResetClearsState) {
  WsFrameParser parser;

  // Feed partial frame
  auto raw = make_server_frame(WsFrameParser::Opcode::Text, "abcdef");
  parser.feed(reinterpret_cast<const uint8_t*>(raw.data()), 3,
              [](const WsFrameParser::Frame&) { FAIL() << "Should not emit on partial data"; });

  // Reset should discard partial state
  parser.reset();

  // Now feed a complete new frame — should parse cleanly
  auto raw2 = make_server_frame(WsFrameParser::Opcode::Text, "fresh");
  auto frames = feed_all(parser, raw2);
  ASSERT_EQ(frames.size(), 1);
  EXPECT_EQ(frames[0].payload, "fresh");
}

// ---------------------------------------------------------------------------
// Frame building (client-to-server, masked)
// ---------------------------------------------------------------------------

TEST(WsFrameParserTest, BuildTextFrameIsMasked) {
  auto frame = WsFrameParser::build_text_frame("test");

  // Byte 0: FIN=1, opcode=1 (text)
  EXPECT_EQ(static_cast<uint8_t>(frame[0]), 0x81);
  // Byte 1: MASK=1, length=4
  EXPECT_EQ(static_cast<uint8_t>(frame[1]), 0x84);
  // Total: 2 header + 4 mask + 4 payload = 10
  EXPECT_EQ(frame.size(), 10);
}

TEST(WsFrameParserTest, BuildAndParseRoundtrip) {
  // Build a masked frame, then parse it — should recover the original payload
  std::string original = "round-trip test payload";
  auto wire = WsFrameParser::build_text_frame(original);

  WsFrameParser parser;
  auto frames = feed_all(parser, wire);

  ASSERT_EQ(frames.size(), 1);
  EXPECT_EQ(frames[0].opcode, WsFrameParser::Opcode::Text);
  EXPECT_EQ(frames[0].payload, original);
}

TEST(WsFrameParserTest, BuildPingFrame) {
  auto frame = WsFrameParser::build_ping_frame("ping");
  EXPECT_EQ(static_cast<uint8_t>(frame[0]), 0x89);         // FIN + Ping opcode
  EXPECT_EQ(static_cast<uint8_t>(frame[1]) & 0x80, 0x80);  // MASK bit set
}

TEST(WsFrameParserTest, BuildCloseFrame) {
  auto frame = WsFrameParser::build_close_frame(1000, "bye");
  EXPECT_EQ(static_cast<uint8_t>(frame[0]), 0x88);  // FIN + Close opcode
  // Payload: 2 (status) + 3 ("bye") = 5, masked
  EXPECT_EQ(static_cast<uint8_t>(frame[1]) & 0x7F, 5);
}

TEST(WsFrameParserTest, Build16BitLengthFrame) {
  std::string payload(200, 'Z');
  auto frame = WsFrameParser::build_text_frame(payload);

  // Byte 1: MASK + 126 (16-bit extended length)
  EXPECT_EQ(static_cast<uint8_t>(frame[1]), 0x80 | 126);
  // 2 header + 2 ext len + 4 mask + 200 payload = 208
  EXPECT_EQ(frame.size(), 208);

  // Round-trip verify
  WsFrameParser parser;
  auto frames = feed_all(parser, frame);
  ASSERT_EQ(frames.size(), 1);
  EXPECT_EQ(frames[0].payload, payload);
}

// ---------------------------------------------------------------------------
// Realistic JSON message (like Polymarket would send)
// ---------------------------------------------------------------------------

TEST(WsFrameParserTest, ParseRealisticPolymarketMessage) {
  WsFrameParser parser;
  std::string json = R"({
    "event_type": "book",
    "asset_id": "21742633143463906290569050155826241533067272736897614950488156847949938836455",
    "market": "0x1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef",
    "timestamp": "1704067200000000",
    "bids": [{"price": "0.53", "size": "100.5"}, {"price": "0.52", "size": "200.0"}],
    "asks": [{"price": "0.55", "size": "120.0"}, {"price": "0.56", "size": "180.0"}]
  })";

  auto raw = make_server_frame(WsFrameParser::Opcode::Text, json);
  auto frames = feed_all(parser, raw);

  ASSERT_EQ(frames.size(), 1);
  EXPECT_EQ(frames[0].opcode, WsFrameParser::Opcode::Text);
  EXPECT_EQ(frames[0].payload, json);
}

// ---------------------------------------------------------------------------
// Payload too large
// ---------------------------------------------------------------------------

TEST(WsFrameParserTest, RejectsOversizedPayload) {
  WsFrameParser parser;

  // Manually construct a frame header claiming 128 MB payload (> 64 MB limit)
  std::string raw;
  raw.push_back(static_cast<char>(0x81));  // FIN + Text
  raw.push_back(static_cast<char>(127));   // 64-bit length follows
  uint64_t fake_len = 128ULL * 1024 * 1024;
  for (int i = 7; i >= 0; --i) {
    raw.push_back(static_cast<char>((fake_len >> (i * 8)) & 0xFF));
  }

  EXPECT_THROW(feed_all(parser, raw), std::runtime_error);
}
