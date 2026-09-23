#include "config/config.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <fstream>
#include <sstream>

#include "util/yaml_compat.h"

namespace opencdc::config {

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static std::string to_lower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return r;
}

static std::string strip_quotes(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        return s.substr(1, s.size() - 2);
    return s;
}

// ---------------------------------------------------------------------------
// Legacy line-based parser: handles old compact one-line format
// "- rule: CDC001, source: mod.src, dest: mod.dst, justification: ..."
// Used as fallback when yaml-cpp rejects invalid YAML (compact comma format).
// ---------------------------------------------------------------------------

static Config parse_legacy(const std::string& content, std::string* error) {
    Config config;
    std::string current_section;
    std::string current_rule;
    size_t line_number = 0;

    std::istringstream iss(content);
    std::string line;

    while (std::getline(iss, line)) {
        line_number++;
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#')
            continue;

        std::string lower = to_lower(trimmed);
        if (lower == "rules:" || lower == "waivers:" || lower == "output:" ||
            lower == "false_paths:" || lower == "clock_groups:") {
            current_section = to_lower(trimmed.substr(0, trimmed.size() - 1));
            current_rule.clear();
            continue;
        }

        if (current_section == "rules") {
            if (trimmed.back() == ':' && trimmed.find(' ') == std::string::npos) {
                current_rule = trim(trimmed.substr(0, trimmed.size() - 1));
            } else if (!current_rule.empty()) {
                size_t colon = trimmed.find(':');
                if (colon != std::string::npos) {
                    std::string key = to_lower(trim(trimmed.substr(0, colon)));
                    std::string value = to_lower(strip_quotes(trim(trimmed.substr(colon + 1))));
                    if (key == "enabled") {
                        if (value == "true")
                            config.rules[current_rule].enabled = true;
                        else if (value == "false")
                            config.rules[current_rule].enabled = false;
                    } else if (key == "severity") {
                        if (value == "error" || value == "warning" || value == "info")
                            config.rules[current_rule].severity = value;
                    }
                }
            }
        } else if (current_section == "waivers") {
            if (trimmed[0] == '-') {
                std::string rest = trim(trimmed.substr(1));
                WaiverConfig w;
                std::string token;
                bool in_quotes = false;
                std::string current;
                std::vector<std::string> tokens;
                for (char c : rest) {
                    if (c == '"') {
                        in_quotes = !in_quotes;
                        current += c;
                    } else if (c == ',' && !in_quotes) {
                        tokens.push_back(trim(current));
                        current.clear();
                    } else {
                        current += c;
                    }
                }
                if (!current.empty())
                    tokens.push_back(trim(current));
                for (const auto& tok : tokens) {
                    size_t colon = tok.find(':');
                    if (colon == std::string::npos)
                        continue;
                    std::string key = trim(tok.substr(0, colon));
                    std::string value = strip_quotes(trim(tok.substr(colon + 1)));
                    std::string lk = to_lower(key);
                    if (lk == "rule")
                        w.rule_id = value;
                    else if (lk == "source")
                        w.source_reg = value;
                    else if (lk == "dest")
                        w.dest_reg = value;
                    else if (lk == "source_domain")
                        w.source_domain = value;
                    else if (lk == "dest_domain")
                        w.dest_domain = value;
                    else if (lk == "justification")
                        w.justification = value;
                    else if (lk == "owner")
                        w.owner = value;
                    else if (lk == "expiry")
                        w.expiry = value;
                }
                if (!w.rule_id.empty())
                    config.waivers.push_back(std::move(w));
            }
        } else if (current_section == "output") {
            size_t colon = trimmed.find(':');
            if (colon != std::string::npos) {
                std::string key = to_lower(trim(trimmed.substr(0, colon)));
                std::string value = to_lower(strip_quotes(trim(trimmed.substr(colon + 1))));
                if (key == "format")
                    config.output.format = value;
                else if (key == "file")
                    config.output.file = value;
                else if (key == "suppress_reset_crossings")
                    config.suppress_reset_crossings = (value == "true");
            }
        } else if (current_section == "false_paths") {
            if (trimmed[0] == '-') {
                std::string rest = trim(trimmed.substr(1));
                FalsePathConfig fp;
                std::string token;
                bool in_quotes = false;
                std::string current;
                std::vector<std::string> tokens;
                for (char c : rest) {
                    if (c == '"') {
                        in_quotes = !in_quotes;
                        current += c;
                    } else if (c == ',' && !in_quotes) {
                        tokens.push_back(trim(current));
                        current.clear();
                    } else {
                        current += c;
                    }
                }
                if (!current.empty())
                    tokens.push_back(trim(current));
                for (const auto& tok : tokens) {
                    size_t colon = tok.find(':');
                    if (colon == std::string::npos)
                        continue;
                    std::string key = to_lower(trim(tok.substr(0, colon)));
                    std::string value = strip_quotes(trim(tok.substr(colon + 1)));
                    if (key == "source")
                        fp.source_reg = value;
                    else if (key == "dest")
                        fp.dest_reg = value;
                    else if (key == "source_clock" || key == "from_clock")
                        fp.source_clock = value;
                    else if (key == "dest_clock" || key == "to_clock")
                        fp.dest_clock = value;
                }
                if ((!fp.source_reg.empty() && !fp.dest_reg.empty()) ||
                    (!fp.source_clock.empty() && !fp.dest_clock.empty())) {
                    config.false_paths.push_back(std::move(fp));
                }
            }
        } else if (current_section == "clock_groups") {
            // Legacy clock_groups parser (compact format).
            if (trimmed.back() == ':' && trimmed.size() > 1) {
                config.clock_groups.emplace_back();
                config.clock_groups.back().exclusive = true;
            } else if (!config.clock_groups.empty()) {
                size_t colon = trimmed.find(':');
                if (colon != std::string::npos) {
                    std::string key = to_lower(trim(trimmed.substr(0, colon)));
                    std::string value = trim(trimmed.substr(colon + 1));
                    auto& cur = config.clock_groups.back();
                    if (key == "clocks") {
                        std::istringstream vss(value);
                        std::string clk;
                        while (std::getline(vss, clk, ',')) {
                            clk = trim(clk);
                            if (!clk.empty())
                                cur.clocks.push_back(clk);
                        }
                    } else if (key == "exclusive") {
                        cur.exclusive = (to_lower(value) == "true");
                    }
                }
            }
        }
    }
    return config;
}

// ---------------------------------------------------------------------------
// yaml-cpp based parser
// ---------------------------------------------------------------------------

static bool parse_rules_node(const YAML::Node& rules_node, Config& config,
                             std::string* error = nullptr) {
    for (auto it = rules_node.begin(); it != rules_node.end(); ++it) {
        std::string rule_id = it->first.as<std::string>();
        YAML::Node rule_node = it->second;
        RuleConfig rc;
        if (rule_node["enabled"]) {
            std::string v = to_lower(rule_node["enabled"].as<std::string>());
            if (v == "true")
                rc.enabled = true;
            else if (v == "false")
                rc.enabled = false;
            else {
                if (error)
                    *error = "Invalid value for enabled in rule " + rule_id + ": " + v;
                return false;
            }
        }
        if (rule_node["severity"]) {
            std::string v = to_lower(rule_node["severity"].as<std::string>());
            if (v == "error" || v == "warning" || v == "info")
                rc.severity = v;
            else {
                if (error)
                    *error = "Invalid severity for rule " + rule_id + ": " + v;
                return false;
            }
        }
        config.rules[rule_id] = rc;
    }
    return true;
}

static void parse_waivers_node(const YAML::Node& waivers_node, Config& config) {
    if (!waivers_node || !waivers_node.IsSequence())
        return;
    for (const auto& item : waivers_node) {
        if (!item.IsMap())
            continue;
        WaiverConfig w;
        if (item["rule"])
            w.rule_id = item["rule"].as<std::string>();
        if (item["source"])
            w.source_reg = item["source"].as<std::string>();
        if (item["dest"])
            w.dest_reg = item["dest"].as<std::string>();
        if (item["source_domain"])
            w.source_domain = item["source_domain"].as<std::string>();
        if (item["dest_domain"])
            w.dest_domain = item["dest_domain"].as<std::string>();
        if (item["justification"])
            w.justification = item["justification"].as<std::string>();
        if (item["owner"])
            w.owner = item["owner"].as<std::string>();
        if (item["expiry"])
            w.expiry = item["expiry"].as<std::string>();
        if (!w.rule_id.empty())
            config.waivers.push_back(std::move(w));
    }
}

static void parse_output_node(const YAML::Node& output_node, Config& config,
                              std::string* error = nullptr) {
    if (!output_node || !output_node.IsMap())
        return;
    if (output_node["format"])
        config.output.format = output_node["format"].as<std::string>();
    if (output_node["file"])
        config.output.file = output_node["file"].as<std::string>();
    if (output_node["suppress_reset_crossings"]) {
        std::string v = to_lower(output_node["suppress_reset_crossings"].as<std::string>());
        if (v == "true")
            config.suppress_reset_crossings = true;
        else if (v == "false")
            config.suppress_reset_crossings = false;
        else if (error)
            *error = "Invalid value for suppress_reset_crossings: " + v;
    }
}

static void parse_false_paths_node(const YAML::Node& fp_node, Config& config) {
    if (!fp_node || !fp_node.IsSequence())
        return;
    for (const auto& item : fp_node) {
        if (!item.IsMap())
            continue;
        FalsePathConfig fp;
        if (item["source"])
            fp.source_reg = item["source"].as<std::string>();
        if (item["dest"])
            fp.dest_reg = item["dest"].as<std::string>();
        if (item["source_clock"] || item["from_clock"])
            fp.source_clock = (item["source_clock"] ? item["source_clock"] : item["from_clock"])
                                  .as<std::string>();
        if (item["dest_clock"] || item["to_clock"])
            fp.dest_clock =
                (item["dest_clock"] ? item["dest_clock"] : item["to_clock"]).as<std::string>();
        if (item["from_reg"])
            fp.source_reg = item["from_reg"].as<std::string>();
        if (item["to_reg"])
            fp.dest_reg = item["to_reg"].as<std::string>();
        if ((!fp.source_reg.empty() && !fp.dest_reg.empty()) ||
            (!fp.source_clock.empty() && !fp.dest_clock.empty())) {
            config.false_paths.push_back(std::move(fp));
        }
    }
}

static void parse_clock_groups_node(const YAML::Node& cg_node, Config& config) {
    if (!cg_node || !cg_node.IsMap())
        return;
    for (auto it = cg_node.begin(); it != cg_node.end(); ++it) {
        ClockGroupConfig grp;
        grp.exclusive = true;
        YAML::Node grp_node = it->second;
        if (grp_node["clocks"]) {
            if (grp_node["clocks"].IsSequence()) {
                for (const auto& c : grp_node["clocks"]) {
                    grp.clocks.push_back(c.as<std::string>());
                }
            } else if (grp_node["clocks"].IsScalar()) {
                std::string val = grp_node["clocks"].as<std::string>();
                std::istringstream ss(val);
                std::string clk;
                while (std::getline(ss, clk, ',')) {
                    auto a = clk.find_first_not_of(" \t");
                    auto b = clk.find_last_not_of(" \t");
                    if (a != std::string::npos)
                        grp.clocks.push_back(clk.substr(a, b - a + 1));
                }
            }
        }
        if (grp_node["exclusive"]) {
            grp.exclusive = to_lower(grp_node["exclusive"].as<std::string>()) == "true";
        }
        config.clock_groups.push_back(std::move(grp));
    }
}

static Config parse_yaml_content(const std::string& content, std::string* error) {
    Config config;
    YAML::Node root = YAML::Load(content);

    if (root["rules"]) {
        if (!parse_rules_node(root["rules"], config, error))
            return Config();
    }
    if (root["waivers"])
        parse_waivers_node(root["waivers"], config);
    if (root["output"]) {
        parse_output_node(root["output"], config, error);
        if (error && !error->empty())
            return Config();
    }
    if (root["false_paths"])
        parse_false_paths_node(root["false_paths"], config);
    if (root["clock_groups"])
        parse_clock_groups_node(root["clock_groups"], config);

    if (!config.output.format.empty() && config.output.format != "json" &&
        config.output.format != "text" && config.output.format != "html") {
        if (error)
            *error = "Invalid output format: " + config.output.format;
        return Config();
    }
    return config;
}

Config ConfigParser::parse_string(const std::string& content, std::string* error) const {
    if (content.empty())
        return Config();

    // Rewrite compact comma-separated list items into proper nested YAML,
    // then parse with yaml-cpp. Legacy parser is only a throw-only fallback.
    std::string expanded = util::expand_compact_yaml(content);
    try {
        return parse_yaml_content(expanded, error);
    } catch (const YAML::Exception&) {
        // Fall through to legacy line-based parser for truly malformed input.
    }

    return parse_legacy(content, error);
}

Config ConfigParser::parse_file(const std::string& path, std::string* error) const {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        if (error)
            *error = "Could not open config file: " + path;
        return Config();
    }

    static constexpr size_t MAX_FILE_SIZE = 1 * 1024 * 1024;
    auto file_size = file.tellg();
    if (file_size > static_cast<std::streampos>(MAX_FILE_SIZE)) {
        if (error)
            *error = "Config file exceeds 1MB limit: " + path;
        return Config();
    }
    file.seekg(0, std::ios::beg);

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    return parse_string(content, error);
}

}  // namespace opencdc::config
