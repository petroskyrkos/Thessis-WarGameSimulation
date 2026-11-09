#include "config/Config.h"

#include <fstream>

namespace wargame {

namespace {

// Splits "world.rows" into {"world", "rows"}.
std::vector<std::string> splitPath(std::string_view path) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= path.size()) {
        const size_t dot = path.find('.', start);
        if (dot == std::string_view::npos) {
            parts.emplace_back(path.substr(start));
            break;
        }
        parts.emplace_back(path.substr(start, dot - start));
        start = dot + 1;
    }
    return parts;
}

}  // namespace

void Config::loadFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw ConfigError("cannot open config file: " + path);
    }

    try {
        // Comments are allowed so a TODO can carry a note next to it.
        root_ = nlohmann::json::parse(in, nullptr, true, /*ignore_comments=*/true);
    } catch (const nlohmann::json::parse_error& e) {
        throw ConfigError("cannot parse " + path + ": " + e.what());
    }

    if (!root_.is_object()) {
        throw ConfigError(path + ": top level must be a JSON object");
    }

    source_ = path;
}

const nlohmann::json& Config::resolve(std::string_view path) const {
    const nlohmann::json* node = &root_;
    std::string walked;

    for (const std::string& key : splitPath(path)) {
        if (!node->is_object() || !node->contains(key)) {
            throw ConfigError("config has no value at \"" + std::string(path) +
                              "\" (stopped after \"" + walked + "\") in " + source_);
        }
        node = &(*node)[key];
        walked = walked.empty() ? key : walked + "." + key;
    }

    return *node;
}

void Config::set(std::string_view path, const nlohmann::json& value) {
    // resolve() proves the path exists. The node belongs to root_, which this
    // object owns and is not const, so writing through it is well defined.
    const nlohmann::json& node = resolve(path);
    const_cast<nlohmann::json&>(node) = value;
}

void Config::collectTodos(const nlohmann::json& node, const std::string& prefix,
                          std::vector<std::string>& out) {
    if (node.is_null()) {
        out.push_back(prefix);
        return;
    }
    if (!node.is_object()) {
        return;
    }
    for (const auto& [key, value] : node.items()) {
        collectTodos(value, prefix.empty() ? key : prefix + "." + key, out);
    }
}

std::vector<std::string> Config::todoPaths() const {
    std::vector<std::string> out;
    collectTodos(root_, "", out);
    return out;
}

}  // namespace wargame
