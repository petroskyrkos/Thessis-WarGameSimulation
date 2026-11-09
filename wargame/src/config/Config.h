// Configuration loader.
//
// Every numeric parameter of the simulation lives in a JSON file, never in the
// source. Values are read by dotted path, e.g. cfg.req<int>("world.rows").
//
// A parameter whose real-world value has not been researched yet is written as
// null in the JSON. Reading it throws, so a missing figure surfaces as a loud
// error naming the key instead of a silent wrong result.

#pragma once

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace wargame {

class ConfigError : public std::runtime_error {
  public:
    explicit ConfigError(const std::string& what) : std::runtime_error(what) {}
};

class Config {
  public:
    Config() = default;

    // Loads and parses the file. Throws ConfigError if it cannot be read or is
    // not valid JSON.
    void loadFile(const std::string& path);

    // Reads the value at a dotted path. Throws ConfigError if the path does
    // not exist, is null (an unresearched TODO), or holds the wrong type.
    template <typename T>
    T req(std::string_view path) const {
        const nlohmann::json& node = resolve(path);
        if (node.is_null()) {
            throw ConfigError("config value \"" + std::string(path) +
                              "\" is still TODO (null) in " + source_);
        }
        try {
            return node.get<T>();
        } catch (const nlohmann::json::exception& e) {
            throw ConfigError("config value \"" + std::string(path) +
                              "\" has the wrong type: " + e.what());
        }
    }

    // Replaces the value at an EXISTING path, in memory only (the file is not
    // touched). For self-checks and experiments that need a variation of the
    // loaded config, such as a hit probability of 1. Throws ConfigError if the
    // path does not exist, so a typo cannot silently create a new key.
    void set(std::string_view path, const nlohmann::json& value);

    // Paths that exist but are still null, in the order they appear. Printed at
    // startup so it is obvious which figures are still placeholders.
    std::vector<std::string> todoPaths() const;

    const std::string& source() const { return source_; }

  private:
    const nlohmann::json& resolve(std::string_view path) const;
    static void collectTodos(const nlohmann::json& node,
                             const std::string& prefix,
                             std::vector<std::string>& out);

    nlohmann::json root_;
    std::string source_;
};

}  // namespace wargame
