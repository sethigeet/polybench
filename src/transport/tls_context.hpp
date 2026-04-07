#pragma once

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>

#include <stdexcept>
#include <string>

/// RAII wrapper around OpenSSL SSL_CTX and BIO memory pair session creation.
///
/// Usage:
///   TlsContext ctx;
///   auto session = ctx.create_session("example.com");
///   // Feed ciphertext into session.rbio, read plaintext from SSL_read(session.ssl)
///   // Write plaintext via SSL_write(session.ssl), read ciphertext from session.wbio
///   ctx.destroy_session(session);
class TlsContext {
 public:
  struct Session {
    SSL* ssl = nullptr;
    BIO* rbio = nullptr;  // feed ciphertext here (we write, OpenSSL reads)
    BIO* wbio = nullptr;  // read ciphertext from here (OpenSSL writes, we read)
  };

  TlsContext() {
    ctx_ = SSL_CTX_new(TLS_client_method());
    if (!ctx_) {
      throw std::runtime_error("SSL_CTX_new failed: " + ssl_error_string());
    }

    // Load system default CA certificates for server verification
    if (SSL_CTX_set_default_verify_paths(ctx_) != 1) {
      SSL_CTX_free(ctx_);
      throw std::runtime_error("SSL_CTX_set_default_verify_paths failed: " + ssl_error_string());
    }

    // Require server certificate verification
    SSL_CTX_set_verify(ctx_, SSL_VERIFY_PEER, nullptr);

    // Minimum TLS 1.2
    SSL_CTX_set_min_proto_version(ctx_, TLS1_2_VERSION);
  }

  ~TlsContext() {
    if (ctx_) {
      SSL_CTX_free(ctx_);
    }
  }

  // Non-copyable
  TlsContext(const TlsContext&) = delete;
  TlsContext& operator=(const TlsContext&) = delete;

  /// Create a new SSL session with BIO memory pairs.
  /// `hostname` is used for SNI and certificate hostname verification.
  Session create_session(const std::string& hostname) {
    Session s;
    s.ssl = SSL_new(ctx_);
    if (!s.ssl) {
      throw std::runtime_error("SSL_new failed: " + ssl_error_string());
    }

    // Create BIO memory pair
    s.rbio = BIO_new(BIO_s_mem());
    s.wbio = BIO_new(BIO_s_mem());
    if (!s.rbio || !s.wbio) {
      if (s.rbio) BIO_free(s.rbio);
      if (s.wbio) BIO_free(s.wbio);
      SSL_free(s.ssl);
      throw std::runtime_error("BIO_new failed");
    }

    // Make BIOs non-blocking (BIO_read returns -1 with BIO_should_retry instead of blocking)
    BIO_set_nbio(s.rbio, 1);
    BIO_set_nbio(s.wbio, 1);

    // Attach BIOs to SSL (SSL takes ownership)
    SSL_set_bio(s.ssl, s.rbio, s.wbio);

    // Set client mode
    SSL_set_connect_state(s.ssl);

    // SNI extension
    SSL_set_tlsext_host_name(s.ssl, hostname.c_str());

    // Certificate hostname verification
    SSL_set1_host(s.ssl, hostname.c_str());

    return s;
  }

  /// Destroy an SSL session. Safe to call with a zeroed session.
  static void destroy_session(Session& s) {
    if (s.ssl) {
      // SSL_free also frees the attached BIOs
      SSL_free(s.ssl);
      s.ssl = nullptr;
      s.rbio = nullptr;
      s.wbio = nullptr;
    }
  }

  /// Get a human-readable OpenSSL error string.
  static std::string ssl_error_string() {
    char buf[256];
    ERR_error_string_n(ERR_get_error(), buf, sizeof(buf));
    return buf;
  }

 private:
  SSL_CTX* ctx_ = nullptr;
};
