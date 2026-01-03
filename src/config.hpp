#pragma once

#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <vector>

#include "engine.hpp"

class ConfigLoader {
 public:
  static EngineConfig load_from_file(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
      throw std::runtime_error("Failed to open config file: " + filepath);
    }

    nlohmann::json j;
    file >> j;

    return parse_json(j);
  }

  static EngineConfig load_from_string(const std::string& json_str) {
    auto j = nlohmann::json::parse(json_str);
    return parse_json(j);
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
  static EngineConfig parse_json(const nlohmann::json& j) {
    EngineConfig config;

    if (j.contains("ws_url")) {
      config.ws_url = j["ws_url"].get<std::string>();
    }

    if (j.contains("assets") && j["assets"].is_array()) {
      for (const auto& asset : j["assets"]) {
        std::string asset_id = asset["asset_id"].get<std::string>();
        std::string market_id = asset["market_id"].get<std::string>();
        std::string outcome_str = asset["outcome"].get<std::string>();
        Outcome outcome =
            (outcome_str == "YES" || outcome_str == "yes") ? Outcome::Yes : Outcome::No;

        config.asset_ids.push_back(asset_id);
        config.asset_mappings[asset_id] = {market_id, outcome};
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
