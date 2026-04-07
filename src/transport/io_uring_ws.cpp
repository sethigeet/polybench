#if defined(__linux__) && defined(HAS_IO_URING)

#include "transport/io_uring_ws.hpp"

#include <arpa/inet.h>
#include <liburing.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <sstream>

#include "transport/tls_context.hpp"
#include "transport/ws_frame_parser.hpp"
#include "utils/thread.hpp"

#define LOGGER_NAME "IoUringWS"
#include "utils/logger.hpp"

// io_uring user_data tags to identify CQE sources
enum class IoOp : uint64_t {
  Connect = 1,
  Recv = 2,
  Send = 3,
};

// IoContext — all io_uring + TLS + WebSocket state, hidden from header
struct IoUringWS::IoContext {
  // io_uring
  struct io_uring ring{};
  bool ring_initialized = false;

  // Provided buffer ring
  struct io_uring_buf_ring* buf_ring = nullptr;
  uint8_t* buf_pool = nullptr;
  int buf_count = 256;
  int buf_size = 16384;
  static constexpr int kBufGroupId = 0;

  // TLS
  TlsContext tls_ctx;
  TlsContext::Session tls_session{};

  // WebSocket frame parser
  WsFrameParser ws_parser;

  // Socket
  int sockfd = -1;

  // Resolved address
  struct sockaddr_storage server_addr{};
  socklen_t server_addr_len = 0;

  // Parsed URL components
  std::string host;
  std::string port;
  std::string path;
  bool use_tls = true;

  // Connection state machine
  enum class Phase {
    Disconnected,
    TcpConnecting,
    TlsHandshaking,
    WsUpgrading,
    Connected,
  } phase = Phase::Disconnected;

  // HTTP upgrade response accumulation
  std::string http_response_buf;

  // Outgoing data staging (ciphertext from wbio)
  std::vector<uint8_t> pending_send;

  // Reconnection
  int reconnect_attempt = 0;
  std::chrono::steady_clock::time_point next_reconnect_time;

  // Ping timer
  std::chrono::steady_clock::time_point last_ping_time;

  // Multishot recv tracking
  bool multishot_recv_active = false;
};

static bool parse_ws_url(const std::string& url, std::string& host, std::string& port,
                         std::string& path, bool& use_tls) {
  std::string_view sv = url;
  if (sv.starts_with("wss://")) {
    use_tls = true;
    sv.remove_prefix(6);
    port = "443";
  } else if (sv.starts_with("ws://")) {
    use_tls = false;
    sv.remove_prefix(5);
    port = "80";
  } else {
    return false;
  }

  auto slash_pos = sv.find('/');
  std::string_view authority = (slash_pos != std::string_view::npos) ? sv.substr(0, slash_pos) : sv;
  path = (slash_pos != std::string_view::npos) ? std::string(sv.substr(slash_pos)) : "/";

  auto colon_pos = authority.find(':');
  if (colon_pos != std::string_view::npos) {
    host = std::string(authority.substr(0, colon_pos));
    port = std::string(authority.substr(colon_pos + 1));
  } else {
    host = std::string(authority);
  }

  return !host.empty();
}

static bool resolve_host(const std::string& host, const std::string& port,
                         struct sockaddr_storage& addr, socklen_t& addr_len) {
  struct addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  struct addrinfo* result = nullptr;
  int rc = getaddrinfo(host.c_str(), port.c_str(), &hints, &result);
  if (rc != 0 || !result) {
    return false;
  }

  std::memcpy(&addr, result->ai_addr, result->ai_addrlen);
  addr_len = static_cast<socklen_t>(result->ai_addrlen);
  freeaddrinfo(result);
  return true;
}

IoUringWS::IoUringWS(const TransportConfig& config)
    : config_(config),
      perf_stats_(config_.perf_stats),
      pipeline_(config_.message_queue_capacity, &perf_stats_),
      io_ctx_(std::make_unique<IoContext>()) {
  current_subscriptions_ = {config.asset_ids.begin(), config.asset_ids.end()};

  // Parse URL
  if (!parse_ws_url(config_.url, io_ctx_->host, io_ctx_->port, io_ctx_->path, io_ctx_->use_tls)) {
    throw std::runtime_error("Invalid WebSocket URL: " + config_.url);
  }

  // Apply io_uring config
  io_ctx_->buf_count = config_.io_uring_buf_count;
  io_ctx_->buf_size = config_.io_uring_buf_size;
}

IoUringWS::~IoUringWS() { stop(); }

void IoUringWS::on_error(ErrorCallback callback) {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  error_callback_ = std::move(callback);
}

void IoUringWS::on_connect(ConnectCallback callback) {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  connect_callback_ = std::move(callback);
}

void IoUringWS::on_disconnect(DisconnectCallback callback) {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  disconnect_callback_ = std::move(callback);
}

size_t IoUringWS::poll_messages(SmallVector<PolymarketMessage, kMessageBatchSize>& out,
                                size_t max_messages) {
  size_t count = pipeline_.poll_messages(out, max_messages);
  if (count > 0) {
    perf_stats_.record_poll();
  }
  return count;
}

bool IoUringWS::wait_for_messages(std::chrono::microseconds timeout) {
  return pipeline_.wait_for_messages(timeout);
}

bool IoUringWS::is_connected() const { return connected_.load(std::memory_order_acquire); }

const PerfStats& IoUringWS::perf_stats() const { return perf_stats_; }
PerfStats& IoUringWS::perf_stats() { return perf_stats_; }

void IoUringWS::send_ws_text(std::string_view payload) {
  auto frame = WsFrameParser::build_text_frame(payload);
  std::lock_guard<std::mutex> lock(send_mutex_);
  send_queue_.push_back(PendingSend{std::move(frame)});
}

void IoUringWS::start() {
  LOG_INFO("Starting io_uring WebSocket connection to {}", config_.url);
  running_.store(true, std::memory_order_release);
  io_thread_ = std::thread([this]() { io_loop(); });
}

void IoUringWS::stop() {
  if (running_.load(std::memory_order_acquire)) {
    LOG_INFO("Stopping io_uring WebSocket connection");
    running_.store(false, std::memory_order_release);
    pipeline_.notify_shutdown();
    if (io_thread_.joinable()) {
      io_thread_.join();
    }
  }
}

static void setup_io_uring(IoUringWS::IoContext& ctx, int queue_depth) {
  struct io_uring_params params{};
  int rc = io_uring_queue_init_params(static_cast<unsigned>(queue_depth), &ctx.ring, &params);
  if (rc < 0) {
    throw std::runtime_error("io_uring_queue_init failed: " + std::string(strerror(-rc)));
  }
  ctx.ring_initialized = true;
}

static void setup_provided_buffers(IoUringWS::IoContext& ctx) {
  // Allocate buffer pool
  size_t pool_size = static_cast<size_t>(ctx.buf_count) * ctx.buf_size;
  ctx.buf_pool = static_cast<uint8_t*>(aligned_alloc(4096, pool_size));
  if (!ctx.buf_pool) {
    throw std::runtime_error("Failed to allocate buffer pool");
  }

  // Register provided buffer ring
  int rc = 0;
  ctx.buf_ring = io_uring_setup_buf_ring(&ctx.ring, static_cast<unsigned>(ctx.buf_count),
                                         IoUringWS::IoContext::kBufGroupId, 0, &rc);
  if (!ctx.buf_ring) {
    free(ctx.buf_pool);
    ctx.buf_pool = nullptr;
    throw std::runtime_error("io_uring_setup_buf_ring failed: " + std::string(strerror(-rc)));
  }

  // Add all buffers to the ring
  for (int i = 0; i < ctx.buf_count; ++i) {
    io_uring_buf_ring_add(ctx.buf_ring, ctx.buf_pool + (i * ctx.buf_size),
                          static_cast<unsigned>(ctx.buf_size), static_cast<unsigned short>(i),
                          io_uring_buf_ring_mask(static_cast<unsigned>(ctx.buf_count)), i);
  }
  io_uring_buf_ring_advance(ctx.buf_ring, ctx.buf_count);
}

static void recycle_buffer(IoUringWS::IoContext& ctx, int buf_id) {
  io_uring_buf_ring_add(ctx.buf_ring, ctx.buf_pool + (buf_id * ctx.buf_size),
                        static_cast<unsigned>(ctx.buf_size), static_cast<unsigned short>(buf_id),
                        io_uring_buf_ring_mask(static_cast<unsigned>(ctx.buf_count)), 0);
  io_uring_buf_ring_advance(ctx.buf_ring, 1);
}

static int create_tcp_socket(const struct sockaddr_storage& addr) {
  int fd = socket(addr.ss_family, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP);
  if (fd < 0) {
    return -1;
  }

  // Set TCP_NODELAY for low latency
  int one = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

  return fd;
}

static void submit_connect(IoUringWS::IoContext& ctx) {
  struct io_uring_sqe* sqe = io_uring_get_sqe(&ctx.ring);
  if (!sqe) return;
  io_uring_prep_connect(sqe, ctx.sockfd, reinterpret_cast<struct sockaddr*>(&ctx.server_addr),
                        ctx.server_addr_len);
  io_uring_sqe_set_data64(sqe, static_cast<uint64_t>(IoOp::Connect));
}

static void submit_multishot_recv(IoUringWS::IoContext& ctx) {
  struct io_uring_sqe* sqe = io_uring_get_sqe(&ctx.ring);
  if (!sqe) return;
  io_uring_prep_recv_multishot(sqe, ctx.sockfd, nullptr, 0, 0);
  sqe->flags |= IOSQE_BUFFER_SELECT;
  sqe->buf_group = IoUringWS::IoContext::kBufGroupId;
  io_uring_sqe_set_data64(sqe, static_cast<uint64_t>(IoOp::Recv));
  ctx.multishot_recv_active = true;
}

static void submit_send(IoUringWS::IoContext& ctx, const void* data, size_t len) {
  // Copy data because io_uring send needs the buffer to live until CQE
  ctx.pending_send.assign(static_cast<const uint8_t*>(data),
                          static_cast<const uint8_t*>(data) + len);

  struct io_uring_sqe* sqe = io_uring_get_sqe(&ctx.ring);
  if (!sqe) return;
  io_uring_prep_send(sqe, ctx.sockfd, ctx.pending_send.data(), ctx.pending_send.size(), 0);
  io_uring_sqe_set_data64(sqe, static_cast<uint64_t>(IoOp::Send));
}

// Flush any pending TLS ciphertext from the write BIO to the socket via io_uring
static void flush_tls_wbio(IoUringWS::IoContext& ctx) {
  if (!ctx.tls_session.wbio) return;

  char buf[16384];
  int n;
  while ((n = BIO_read(ctx.tls_session.wbio, buf, sizeof(buf))) > 0) {
    submit_send(ctx, buf, static_cast<size_t>(n));
  }
}

// Feed ciphertext into the TLS read BIO
static void feed_tls_ciphertext(IoUringWS::IoContext& ctx, const uint8_t* data, size_t len) {
  BIO_write(ctx.tls_session.rbio, data, static_cast<int>(len));
}

// Attempt to read decrypted data from TLS and feed it to the WebSocket parser
static void pump_tls_read(IoUringWS::IoContext& ctx, MessagePipeline& pipeline,
                          PerfStats& perf_stats) {
  uint8_t plaintext[16384];
  int n;
  while ((n = SSL_read(ctx.tls_session.ssl, plaintext, sizeof(plaintext))) > 0) {
    if (ctx.phase == IoUringWS::IoContext::Phase::WsUpgrading) {
      // Accumulate HTTP response during upgrade phase
      ctx.http_response_buf.append(reinterpret_cast<const char*>(plaintext),
                                   static_cast<size_t>(n));
    } else if (ctx.phase == IoUringWS::IoContext::Phase::Connected) {
      // Feed to WebSocket frame parser
      ctx.ws_parser.feed(plaintext, static_cast<size_t>(n),
                         [&pipeline, &perf_stats](const WsFrameParser::Frame& frame) {
                           if (frame.opcode == WsFrameParser::Opcode::Text ||
                               frame.opcode == WsFrameParser::Opcode::Binary) {
                             pipeline.ingest_message(frame.payload_view());
                           }
                           // Ping/Pong/Close handled at a higher level
                         });
    }
  }

  // Check if SSL_read needs us to write (e.g. renegotiation)
  int err = SSL_get_error(ctx.tls_session.ssl, n);
  if (err == SSL_ERROR_WANT_WRITE) {
    flush_tls_wbio(ctx);
  }
}

static std::string build_http_upgrade_request(const std::string& host, const std::string& path) {
  // Generate a random 16-byte base64-encoded key
  // For simplicity, use a fixed key — the server doesn't verify it matches anything
  // we compute; it just echoes back a SHA-1 hash we could verify but don't need to.
  static constexpr const char* kWebSocketKey = "dGhlIHNhbXBsZSBub25jZQ==";

  std::ostringstream oss;
  oss << "GET " << path << " HTTP/1.1\r\n"
      << "Host: " << host << "\r\n"
      << "Upgrade: websocket\r\n"
      << "Connection: Upgrade\r\n"
      << "Sec-WebSocket-Key: " << kWebSocketKey << "\r\n"
      << "Sec-WebSocket-Version: 13\r\n"
      << "User-Agent: polybench/1.0\r\n"
      << "\r\n";
  return oss.str();
}

static bool is_http_upgrade_complete(const std::string& response) {
  return response.find("\r\n\r\n") != std::string::npos;
}

static bool validate_http_upgrade_response(const std::string& response) {
  // Check for "HTTP/1.1 101" status line
  return response.find("101") != std::string::npos;
}

void IoUringWS::io_loop() {
  // Pin to configured CPU
  if (config_.ingest_cpu_affinity >= 0) {
    utils::thread::pin_current_thread_to_cpu(config_.ingest_cpu_affinity, "io_uring_ingest");
  }

  // Setup io_uring
  setup_io_uring(*io_ctx_, config_.io_uring_queue_depth);
  setup_provided_buffers(*io_ctx_);

  // Resolve hostname
  if (!resolve_host(io_ctx_->host, io_ctx_->port, io_ctx_->server_addr, io_ctx_->server_addr_len)) {
    LOG_ERROR("Failed to resolve host: {}", io_ctx_->host);
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (error_callback_) error_callback_("DNS resolution failed for " + io_ctx_->host);
    return;
  }

  // Start initial connection
  auto start_connect = [this]() {
    io_ctx_->sockfd = create_tcp_socket(io_ctx_->server_addr);
    if (io_ctx_->sockfd < 0) {
      LOG_ERROR("Failed to create socket: {}", strerror(errno));
      return;
    }

    // Apply socket buffer tuning
    if (config_.socket_rcvbuf_bytes > 0) {
      setsockopt(io_ctx_->sockfd, SOL_SOCKET, SO_RCVBUF, &config_.socket_rcvbuf_bytes,
                 sizeof(config_.socket_rcvbuf_bytes));
    }

    io_ctx_->phase = IoContext::Phase::TcpConnecting;
    submit_connect(*io_ctx_);
    io_uring_submit(&io_ctx_->ring);
  };

  start_connect();

  // Event loop
  while (running_.load(std::memory_order_acquire)) {
    // 1. Drain send queue (engine thread → io_thread)
    {
      std::vector<PendingSend> sends;
      {
        std::lock_guard<std::mutex> lock(send_mutex_);
        sends.swap(send_queue_);
      }
      for (auto& s : sends) {
        if (io_ctx_->phase == IoContext::Phase::Connected && io_ctx_->tls_session.ssl) {
          SSL_write(io_ctx_->tls_session.ssl, s.data.data(), static_cast<int>(s.data.size()));
          flush_tls_wbio(*io_ctx_);
        }
      }
    }

    // 2. Ping keepalive
    if (io_ctx_->phase == IoContext::Phase::Connected) {
      auto now = std::chrono::steady_clock::now();
      auto elapsed =
          std::chrono::duration_cast<std::chrono::seconds>(now - io_ctx_->last_ping_time);
      if (elapsed.count() >= config_.ping_interval_secs) {
        auto ping_frame = WsFrameParser::build_ping_frame();
        SSL_write(io_ctx_->tls_session.ssl, ping_frame.data(), static_cast<int>(ping_frame.size()));
        flush_tls_wbio(*io_ctx_);
        io_ctx_->last_ping_time = now;
      }
    }

    // 3. Submit any pending SQEs
    io_uring_submit(&io_ctx_->ring);

    // 4. Reap CQEs
    struct io_uring_cqe* cqe = nullptr;
    struct __kernel_timespec ts{};
    ts.tv_nsec = 1'000'000;  // 1ms timeout
    int rc = io_uring_wait_cqe_timeout(&io_ctx_->ring, &cqe, &ts);

    if (rc == -ETIME || rc == -EINTR) {
      continue;
    }

    if (rc < 0) {
      LOG_ERROR("io_uring_wait_cqe_timeout error: {}", strerror(-rc));
      continue;
    }

    // Process this CQE and any others that are ready
    unsigned head;
    unsigned cqe_count = 0;
    io_uring_for_each_cqe(&io_ctx_->ring, head, cqe) {
      auto op = static_cast<IoOp>(io_uring_cqe_get_data64(cqe));
      int res = cqe->res;
      uint32_t flags = cqe->flags;

      switch (op) {
        case IoOp::Connect: {
          if (res < 0) {
            LOG_ERROR("TCP connect failed: {}", strerror(-res));
            close(io_ctx_->sockfd);
            io_ctx_->sockfd = -1;
            io_ctx_->phase = IoContext::Phase::Disconnected;
            io_ctx_->reconnect_attempt++;
            int wait_secs =
                std::min(config_.reconnect_wait_secs * (1 << io_ctx_->reconnect_attempt),
                         config_.reconnect_wait_max_secs);
            io_ctx_->next_reconnect_time =
                std::chrono::steady_clock::now() + std::chrono::seconds(wait_secs);
            break;
          }

          LOG_INFO("TCP connected to {}:{}", io_ctx_->host, io_ctx_->port);

          if (io_ctx_->use_tls) {
            // Start TLS handshake
            io_ctx_->tls_session = io_ctx_->tls_ctx.create_session(io_ctx_->host);
            io_ctx_->phase = IoContext::Phase::TlsHandshaking;

            // Initiate handshake
            SSL_do_handshake(io_ctx_->tls_session.ssl);
            flush_tls_wbio(*io_ctx_);

            // Submit recv to get server's TLS response
            submit_multishot_recv(*io_ctx_);
          } else {
            // No TLS — go straight to WS upgrade
            io_ctx_->phase = IoContext::Phase::WsUpgrading;
            std::string upgrade = build_http_upgrade_request(io_ctx_->host, io_ctx_->path);
            submit_send(*io_ctx_, upgrade.data(), upgrade.size());
            submit_multishot_recv(*io_ctx_);
          }
          break;
        }

        case IoOp::Recv: {
          if (res <= 0) {
            // Connection closed or error
            if (res < 0 && res != -ENOBUFS) {
              LOG_ERROR("recv error: {}", strerror(-res));
            }
            if (res == -ENOBUFS) {
              // Buffer pool exhausted — re-submit non-multishot recv
              LOG_WARN("io_uring buffer pool exhausted");
              io_ctx_->multishot_recv_active = false;
              submit_multishot_recv(*io_ctx_);
              break;
            }
            if (res == 0) {
              LOG_INFO("Connection closed by server");
            }

            // Clean up and trigger reconnect
            if (io_ctx_->tls_session.ssl) {
              TlsContext::destroy_session(io_ctx_->tls_session);
            }
            if (io_ctx_->sockfd >= 0) {
              close(io_ctx_->sockfd);
              io_ctx_->sockfd = -1;
            }
            connected_.store(false, std::memory_order_release);
            io_ctx_->phase = IoContext::Phase::Disconnected;
            io_ctx_->multishot_recv_active = false;
            io_ctx_->ws_parser.reset();
            io_ctx_->http_response_buf.clear();
            io_ctx_->reconnect_attempt++;
            int wait_secs = std::min(
                config_.reconnect_wait_secs * (1 << std::min(io_ctx_->reconnect_attempt, 10)),
                config_.reconnect_wait_max_secs);
            io_ctx_->next_reconnect_time =
                std::chrono::steady_clock::now() + std::chrono::seconds(wait_secs);

            {
              std::lock_guard<std::mutex> lk(callback_mutex_);
              if (disconnect_callback_) disconnect_callback_();
            }
            break;
          }

          // Extract buffer ID from CQE flags (provided buffers)
          int buf_id = flags >> IORING_CQE_BUFFER_SHIFT;
          uint8_t* buf = io_ctx_->buf_pool + (buf_id * io_ctx_->buf_size);
          size_t data_len = static_cast<size_t>(res);

          if (io_ctx_->use_tls) {
            // Feed ciphertext to TLS
            feed_tls_ciphertext(*io_ctx_, buf, data_len);

            if (io_ctx_->phase == IoContext::Phase::TlsHandshaking) {
              int hs_rc = SSL_do_handshake(io_ctx_->tls_session.ssl);
              if (hs_rc == 1) {
                // Handshake complete
                LOG_INFO("TLS handshake complete");
                io_ctx_->phase = IoContext::Phase::WsUpgrading;

                // Send HTTP upgrade through TLS
                std::string upgrade = build_http_upgrade_request(io_ctx_->host, io_ctx_->path);
                SSL_write(io_ctx_->tls_session.ssl, upgrade.data(),
                          static_cast<int>(upgrade.size()));
                flush_tls_wbio(*io_ctx_);

                // Try reading any leftover plaintext (shouldn't be any yet)
                pump_tls_read(*io_ctx_, pipeline_, perf_stats_);
              } else {
                int err = SSL_get_error(io_ctx_->tls_session.ssl, hs_rc);
                if (err == SSL_ERROR_WANT_WRITE) {
                  flush_tls_wbio(*io_ctx_);
                } else if (err != SSL_ERROR_WANT_READ) {
                  LOG_ERROR("TLS handshake error: {}", TlsContext::ssl_error_string());
                  // Will reconnect on next recv error
                }
              }
            } else {
              // WsUpgrading or Connected — pump TLS read
              pump_tls_read(*io_ctx_, pipeline_, perf_stats_);
            }
          } else {
            // No TLS — feed raw data
            if (io_ctx_->phase == IoContext::Phase::WsUpgrading) {
              io_ctx_->http_response_buf.append(reinterpret_cast<const char*>(buf), data_len);
            } else if (io_ctx_->phase == IoContext::Phase::Connected) {
              io_ctx_->ws_parser.feed(buf, data_len, [this](const WsFrameParser::Frame& frame) {
                if (frame.opcode == WsFrameParser::Opcode::Text ||
                    frame.opcode == WsFrameParser::Opcode::Binary) {
                  pipeline_.ingest_message(frame.payload_view());
                }
              });
            }
          }

          // Check if HTTP upgrade is complete
          if (io_ctx_->phase == IoContext::Phase::WsUpgrading &&
              is_http_upgrade_complete(io_ctx_->http_response_buf)) {
            if (validate_http_upgrade_response(io_ctx_->http_response_buf)) {
              LOG_INFO("WebSocket upgrade complete — connected");
              io_ctx_->phase = IoContext::Phase::Connected;
              io_ctx_->reconnect_attempt = 0;
              io_ctx_->last_ping_time = std::chrono::steady_clock::now();
              io_ctx_->http_response_buf.clear();
              connected_.store(true, std::memory_order_release);

              // Send initial subscription
              {
                std::lock_guard<std::mutex> lk(subscription_mutex_);
                if (!current_subscriptions_.empty()) {
                  simdjson::builder::string_builder sb;
                  sb.start_object();
                  sb.append_key_value<"assets_ids">(current_subscriptions_);
                  sb.append_comma();
                  sb.append_key_value<"type">("market");
                  sb.append_comma();
                  sb.append_key_value<"operation">("subscribe");
                  sb.append_comma();
                  sb.append_key_value<"custom_feature_enabled">(true);
                  sb.end_object();

                  std::string_view msg = sb;
                  LOG_INFO("Sending subscription for {} assets", current_subscriptions_.size());
                  auto ws_frame = WsFrameParser::build_text_frame(msg);
                  SSL_write(io_ctx_->tls_session.ssl, ws_frame.data(),
                            static_cast<int>(ws_frame.size()));
                  flush_tls_wbio(*io_ctx_);
                }
              }

              {
                std::lock_guard<std::mutex> lk(callback_mutex_);
                if (connect_callback_) connect_callback_();
              }
            } else {
              LOG_ERROR("WebSocket upgrade rejected: {}", io_ctx_->http_response_buf);
              io_ctx_->http_response_buf.clear();
              // Connection will be cleaned up by close
            }
          }

          // Recycle the provided buffer
          recycle_buffer(*io_ctx_, buf_id);

          // If multishot was cancelled (CQE without IORING_CQE_F_MORE), re-submit
          if (!(flags & IORING_CQE_F_MORE)) {
            io_ctx_->multishot_recv_active = false;
            if (io_ctx_->phase != IoContext::Phase::Disconnected) {
              submit_multishot_recv(*io_ctx_);
            }
          }

          break;
        }

        case IoOp::Send: {
          if (res < 0) {
            LOG_ERROR("send error: {}", strerror(-res));
          }
          break;
        }
      }

      cqe_count++;
    }

    io_uring_cq_advance(&io_ctx_->ring, cqe_count);

    if (cqe_count > 0 && perf_stats_.enabled()) {
      perf_stats_.record_io_uring_batch(cqe_count);
    }

    // 5. Reconnection logic
    if (io_ctx_->phase == IoContext::Phase::Disconnected &&
        std::chrono::steady_clock::now() >= io_ctx_->next_reconnect_time) {
      LOG_INFO("Attempting reconnection (attempt {})", io_ctx_->reconnect_attempt);
      start_connect();
    }
  }

  // Cleanup
  if (io_ctx_->tls_session.ssl) {
    TlsContext::destroy_session(io_ctx_->tls_session);
  }
  if (io_ctx_->sockfd >= 0) {
    close(io_ctx_->sockfd);
    io_ctx_->sockfd = -1;
  }
  if (io_ctx_->buf_pool) {
    free(io_ctx_->buf_pool);
    io_ctx_->buf_pool = nullptr;
  }
  if (io_ctx_->ring_initialized) {
    io_uring_queue_exit(&io_ctx_->ring);
    io_ctx_->ring_initialized = false;
  }

  connected_.store(false, std::memory_order_release);
}

// Explicit template instantiations
template void IoUringWS::subscribe<std::vector<AssetId>>(const std::vector<AssetId>&);
template void IoUringWS::unsubscribe<SmallVector<AssetId, 2>>(const SmallVector<AssetId, 2>&);

#endif  // defined(__linux__) && defined(HAS_IO_URING)
