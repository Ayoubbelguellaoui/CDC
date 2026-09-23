#ifndef OPENCDC_UTIL_YAML_COMPAT_H
#define OPENCDC_UTIL_YAML_COMPAT_H

#include <cctype>
#include <sstream>
#include <string>
#include <vector>

namespace opencdc::util {

/// Split a compact YAML list-item line on commas, respecting quoted values.
/// Input: "- rule: CDC001, source: mod.src, dest: mod.dst"
/// Output: {"- rule: CDC001", "source: mod.src", "dest: mod.dst"}
inline std::vector<std::string> split_compact_kv(const std::string& line) {
    std::vector<std::string> parts;
    std::string current;
    bool in_quotes = false;

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '"') {
            in_quotes = !in_quotes;
            current += c;
        } else if (c == ',' && !in_quotes) {
            parts.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty())
        parts.push_back(current);
    return parts;
}

/// Rewrite compact list items into proper nested YAML.
///
/// Detects lines like:
///   "  - rule: CDC001, source: mod.src, dest: mod.dst"
/// and rewrites them to:
///   "  - rule: CDC001\n    source: mod.src\n    dest: mod.dst"
///
/// Scalar value lists (e.g. "clocks: clk_a, clk_b") are NOT rewritten —
/// yaml-cpp handles those natively.
inline std::string expand_compact_yaml(const std::string& content) {
    std::string result;
    result.reserve(content.size());

    std::istringstream iss(content);
    std::string line;

    while (std::getline(iss, line)) {
        // Detect compact list item: line contains "- key: value, key: value"
        // We need: a dash, a key after it, and at least one more ", key:" outside quotes.
        size_t dash_pos = line.find("- ");
        if (dash_pos == std::string::npos) {
            result += line + "\n";
            continue;
        }

        std::string after_dash = line.substr(dash_pos + 2);
        auto parts = split_compact_kv(after_dash);

        if (parts.size() <= 1) {
            // No commas found — not compact.
            result += line + "\n";
            continue;
        }

        // Verify every part looks like "key: value" (at least the first).
        // Guard against rewriting lines like "- clk_a, clk_b" (scalar lists).
        bool all_kv = true;
        bool has_comma_kv = false;
        for (size_t i = 0; i < parts.size(); ++i) {
            std::string trimmed = parts[i];
            // Trim leading/trailing whitespace
            size_t s = trimmed.find_first_not_of(" \t");
            if (s == std::string::npos) {
                all_kv = false;
                break;
            }
            trimmed = trimmed.substr(s);
            size_t e = trimmed.find_last_not_of(" \t");
            if (e != std::string::npos)
                trimmed = trimmed.substr(0, e + 1);

            size_t colon = trimmed.find(':');
            if (colon == std::string::npos || colon == 0) {
                all_kv = false;
                break;
            }
            // Key must be a simple identifier (alphanumeric, underscore).
            std::string key = trimmed.substr(0, colon);
            for (char k : key) {
                if (!std::isalnum(static_cast<unsigned char>(k)) && k != '_') {
                    all_kv = false;
                    break;
                }
            }
            if (!all_kv)
                break;
            if (i > 0)
                has_comma_kv = true;
        }

        if (!all_kv || !has_comma_kv) {
            result += line + "\n";
            continue;
        }

        // Rewrite: first part stays on the "- " line, rest indented.
        // Compute the indent before the dash.
        std::string prefix = line.substr(0, dash_pos);
        std::string child_indent = prefix + "  ";

        for (size_t i = 0; i < parts.size(); ++i) {
            // Trim the part
            std::string p = parts[i];
            size_t s = p.find_first_not_of(" \t");
            if (s == std::string::npos)
                continue;
            p = p.substr(s);
            size_t e = p.find_last_not_of(" \t");
            if (e != std::string::npos)
                p = p.substr(0, e + 1);

            if (i == 0) {
                result += prefix + "- " + p + "\n";
            } else {
                result += child_indent + p + "\n";
            }
        }
    }

    return result;
}

}  // namespace opencdc::util

#endif  // OPENCDC_UTIL_YAML_COMPAT_H
