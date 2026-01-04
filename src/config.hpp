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
      } else if (arg == "--asset" && i + 1 < argc) {
        // Format: ASSET_ID:MARKET_ID:YES or ASSET_ID:MARKET_ID:NO
        std::string asset_spec = argv[++i];
        auto parts = split(asset_spec, ':');
        if (parts.size() >= 3) {
          std::string asset_id = parts[0];
          std::string market_id = parts[1];
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

    auto assets_result = doc["assets"].get_array();
    if (!assets_result.error()) {
      for (auto asset : assets_result.value()) {
        auto asset_obj = asset.get_object();
        if (asset_obj.error()) continue;

        std::string asset_id;
        std::string market_id;
        Outcome outcome = Outcome::No;

        for (auto field : asset_obj.value()) {
          std::string_view key = field.unescaped_key();
          if (key == "asset_id") {
            asset_id = std::string(field.value().get_string().value());
          } else if (key == "market_id") {
            market_id = std::string(field.value().get_string().value());
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
};
