#pragma once

#include <simdjson.h>

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "engine.hpp"

class ConfigLoader {
 public:
  static EngineConfig load_from_file(const std::string& filepath) {
    simdjson::padded_string json_content;
    auto load_result = simdjson::padded_string::load(filepath);
    if (load_result.error()) {
      throw std::runtime_error("Failed to read config file: " + filepath);
    }
    json_content = std::move(load_result.value());

    auto doc_result = simdjson::ondemand::parser::get_parser().iterate(json_content);
    if (doc_result.error()) {
      throw std::runtime_error("Failed to parse config JSON: " +
                               std::string(simdjson::error_message(doc_result.error())));
    }

    return parse_json(doc_result.value());
  }

  static EngineConfig load_from_args(int argc, char* argv[]) {
    EngineConfig config;

    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];

      if (arg == "--config" && i + 1 < argc) {
        config = load_from_file(argv[++i]);
      } else if (arg == "--url" && i + 1 < argc) {
        config.ws_url = argv[++i];
      } else if (arg == "--transport" && i + 1 < argc) {
        config.runtime.mode = parse_transport_mode(argv[++i]);
      } else if (arg == "--enable-perf-stats") {
        config.runtime.perf_stats.enabled = true;
      } else if (arg == "--perf-log-interval" && i + 1 < argc) {
        config.runtime.perf_stats.log_interval_messages = parse_u64(argv[++i], "perf log interval");
      } else if (arg == "--queue-capacity" && i + 1 < argc) {
        config.runtime.message_queue_capacity = parse_u64(argv[++i], "queue capacity");
      } else if (arg == "--ingest-cpu" && i + 1 < argc) {
        config.runtime.ingest_cpu_affinity = parse_int(argv[++i], "ingest cpu");
      } else if (arg == "--engine-cpu" && i + 1 < argc) {
        config.runtime.engine_cpu_affinity = parse_int(argv[++i], "engine cpu");
      } else if (arg == "--asset" && i + 1 < argc) {
        // Format: ASSET_ID:MARKET_ID:YES or ASSET_ID:MARKET_ID:NO
        std::string asset_spec = argv[++i];
        auto parts = split(asset_spec, ':');
        if (parts.size() >= 3) {
          AssetId asset_id{parts[0]};
          MarketId market_id{parts[1]};
          Outcome outcome = (parts[2] == "YES" || parts[2] == "yes") ? Outcome::Yes : Outcome::No;

          config.asset_ids.push_back(asset_id);
          config.asset_mappings[asset_id] = {market_id, outcome};
        }
      } else if (arg == "--help" || arg == "-h") {
        print_usage();
        exit(0);
      }
    }

    return config;
  }

  static void print_usage() {
    std::cout << "Usage: polybench [OPTIONS]\n"
              << "\nOptions:\n"
              << "  --config FILE     Load configuration from JSON file\n"
              << "  --url URL         WebSocket URL (default: "
                 "wss://ws-subscriptions-clob.polymarket.com/ws/market)\n"
              << "  --transport MODE  Transport mode: ixwebsocket\n"
              << "  --enable-perf-stats Enable ingest/dispatch performance counters\n"
              << "  --perf-log-interval N  Log perf stats every N parsed messages\n"
              << "  --queue-capacity N Set transport queue capacity\n"
              << "  --ingest-cpu N    Pin ingest thread to CPU N on Linux\n"
              << "  --engine-cpu N    Pin engine thread to CPU N on Linux\n"
              << "  --asset SPEC      Add asset subscription (format: ASSET_ID:MARKET_ID:YES|NO)\n"
              << "  --help, -h        Show this help message\n"
              << "\nExample:\n"
              << "  polybench --config config.json\n"
              << "  polybench --asset 12345:0xabc:YES --asset 67890:0xabc:NO\n";
  }

 private:
  static EngineConfig parse_json(simdjson::ondemand::document& doc) {
    EngineConfig config;

    auto ws_url_result = doc["ws_url"].get_string();
    if (!ws_url_result.error()) {
      config.ws_url = std::string(ws_url_result.value());
    }

    doc.rewind();

    auto transport_result = doc["transport"].get_object();
    if (!transport_result.error()) {
      for (auto field : transport_result.value()) {
        std::string_view key = field.unescaped_key();
        if (key == "mode") {
          config.runtime.mode =
              parse_transport_mode(std::string(field.value().get_string().value()));
        } else if (key == "message_queue_capacity") {
          config.runtime.message_queue_capacity =
              static_cast<size_t>(field.value().get_uint64().value());
        } else if (key == "consumer_spin_count") {
          config.runtime.consumer_spin_count = int(field.value().get_int64().value());
        } else if (key == "consumer_wait_timeout_us") {
          config.runtime.consumer_wait_timeout_us = int(field.value().get_int64().value());
        } else if (key == "consumer_sleep_initial_us") {
          config.runtime.consumer_sleep_initial_us = int(field.value().get_int64().value());
        } else if (key == "consumer_sleep_max_us") {
          config.runtime.consumer_sleep_max_us = int(field.value().get_int64().value());
        } else if (key == "ingest_cpu_affinity") {
          config.runtime.ingest_cpu_affinity = int(field.value().get_int64().value());
        } else if (key == "engine_cpu_affinity") {
          config.runtime.engine_cpu_affinity = int(field.value().get_int64().value());
        } else if (key == "socket_rcvbuf_bytes") {
          config.runtime.socket_rcvbuf_bytes = int(field.value().get_int64().value());
        } else if (key == "busy_poll_us") {
          config.runtime.busy_poll_us = int(field.value().get_int64().value());
        } else if (key == "recv_batch_size") {
          config.runtime.recv_batch_size = static_cast<size_t>(field.value().get_uint64().value());
        } else if (key == "enable_perf_stats") {
          config.runtime.perf_stats.enabled = bool(field.value().get_bool().value());
        } else if (key == "perf_log_interval_messages") {
          config.runtime.perf_stats.log_interval_messages = field.value().get_uint64().value();
        }
      }
    }

    doc.rewind();

    auto assets_result = doc["assets"].get_array();
    if (!assets_result.error()) {
      for (auto asset : assets_result.value()) {
        auto asset_obj = asset.get_object();
        if (asset_obj.error()) continue;

        AssetId asset_id;
        MarketId market_id;
        Outcome outcome = Outcome::No;

        for (auto field : asset_obj.value()) {
          std::string_view key = field.unescaped_key();
          if (key == "asset_id") {
            asset_id = field.value().get_string().value();
          } else if (key == "market_id") {
            market_id = field.value().get_string().value();
          } else if (key == "outcome") {
            std::string_view outcome_str = field.value().get_string().value();
            outcome = (outcome_str == "YES" || outcome_str == "yes") ? Outcome::Yes : Outcome::No;
          }
        }

        if (!asset_id.empty()) {
          config.asset_ids.push_back(asset_id);
          config.asset_mappings[asset_id] = {market_id, outcome};
        }
      }
    }

    return config;
  }

  static std::vector<std::string> split(const std::string& str, char delimiter) {
    std::vector<std::string> parts;
    std::string part;
    std::istringstream stream(str);
    while (std::getline(stream, part, delimiter)) {
      parts.push_back(part);
    }
    return parts;
  }

  static TransportMode parse_transport_mode(const std::string& mode) {
    if (mode == "ixwebsocket" || mode == "ix_ws") return TransportMode::IxWebSocket;
    throw std::runtime_error("Unknown transport mode: " + mode);
  }

  static int parse_int(const std::string& value, const std::string& field) {
    try {
      return std::stoi(value);
    } catch (...) {
      throw std::runtime_error("Invalid integer for " + field + ": " + value);
    }
  }

  static uint64_t parse_u64(const std::string& value, const std::string& field) {
    try {
      return std::stoull(value);
    } catch (...) {
      throw std::runtime_error("Invalid unsigned integer for " + field + ": " + value);
    }
  }
};
