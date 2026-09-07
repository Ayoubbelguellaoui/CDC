#include "config/config.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace opencdc::config {

std::string ConfigParser::trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string ConfigParser::to_lower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return r;
}

static std::string strip_quotes(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

void ConfigParser::parse_rule_section(const std::string& content, Config& config) const {
    std::istringstream iss(content);
    std::string line;
    std::string current_rule;

    while (std::getline(iss, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#')
            continue;

        if (line.back() == ':' && line.size() > 1) {
            std::string stripped = trim(line.substr(0, line.size() - 1));
            if (!stripped.empty() && stripped.find(' ') == std::string::npos) {
                current_rule = stripped;
                continue;
            }
        }

        if (!current_rule.empty()) {
            size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::string key = trim(line.substr(0, colon));
                std::string value = trim(line.substr(colon + 1));

                if (to_lower(key) == "enabled") {
                    std::string v = to_lower(value);
                    if (v == "true") {
                        config.rules[current_rule].enabled = true;
                    } else if (v == "false") {
                        config.rules[current_rule].enabled = false;
                    }
                    // Ignore invalid booleans (typos silently suppressed before)
                } else if (to_lower(key) == "severity") {
                    std::string v = to_lower(value);
                    if (v == "error" || v == "warning" || v == "info") {
                        config.rules[current_rule].severity = v;
                    }
                }
            }
        }
    }
}

void ConfigParser::parse_waiver_section(const std::string& content, Config& config) const {
    std::istringstream iss(content);
    std::string line;

    while (std::getline(iss, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#')
            continue;

        if (line[0] != '-')
            continue;

        std::string rest = trim(line.substr(1));
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

            if (to_lower(key) == "rule")
                w.rule_id = value;
            else if (to_lower(key) == "source")
                w.source_reg = value;
            else if (to_lower(key) == "dest")
                w.dest_reg = value;
            else if (to_lower(key) == "source_domain")
                w.source_domain = value;
            else if (to_lower(key) == "dest_domain")
                w.dest_domain = value;
            else if (to_lower(key) == "justification")
                w.justification = value;
            else if (to_lower(key) == "owner")
                w.owner = value;
            else if (to_lower(key) == "expiry")
                w.expiry = value;
        }

        if (!w.rule_id.empty()) {
            config.waivers.push_back(std::move(w));
        }
    }
}

void ConfigParser::parse_output_section(const std::string& content, Config& config) const {
    std::istringstream iss(content);
    std::string line;

    while (std::getline(iss, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#')
            continue;

        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = trim(line.substr(0, colon));
            std::string value = trim(line.substr(colon + 1));

            if (to_lower(key) == "format")
                config.output.format = value;
            else if (to_lower(key) == "file")
                config.output.file = value;
            else if (to_lower(key) == "suppress_reset_crossings")
                config.suppress_reset_crossings = (to_lower(value) == "true");
            else if (to_lower(key) == "reconvergence_depth") {
                try {
                    int d = std::stoi(value);
                    if (d >= 1 && d <= 50)
                        config.reconvergence_depth = d;
                } catch (...) {
                }
            }
        }
    }
}

void ConfigParser::parse_false_path_section(const std::string& content, Config& config) const {
    std::istringstream iss(content);
    std::string line;

    while (std::getline(iss, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#')
            continue;
        if (line[0] != '-')
            continue;

        std::string rest = trim(line.substr(1));
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
            std::string key = trim(tok.substr(0, colon));
            std::string value = strip_quotes(trim(tok.substr(colon + 1)));
            std::string lk = to_lower(key);
            if (lk == "source")
                fp.source_reg = value;
            else if (lk == "dest")
                fp.dest_reg = value;
            else if (lk == "source_clock" || lk == "from_clock")
                fp.source_clock = value;
            else if (lk == "dest_clock" || lk == "to_clock")
                fp.dest_clock = value;
        }

        if ((!fp.source_reg.empty() && !fp.dest_reg.empty()) ||
            (!fp.source_clock.empty() && !fp.dest_clock.empty())) {
            config.false_paths.push_back(std::move(fp));
        }
    }
}

void ConfigParser::parse_reset_policy_section(const std::string& content, Config& config) const {
    std::istringstream iss(content);
    std::string line;

    while (std::getline(iss, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#')
            continue;

        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = trim(line.substr(0, colon));
            std::string value = to_lower(trim(line.substr(colon + 1)));

            if (to_lower(key) == "require_cdc_register_reset") {
                config.reset_policy.require_cdc_register_reset = (value == "true");
            } else if (to_lower(key) == "check_same_clock_reset_crossings") {
                config.reset_policy.check_same_clock_reset_crossings = (value == "true");
            } else if (to_lower(key) == "detect_reset_synchronizer") {
                config.reset_policy.detect_reset_synchronizer = (value == "true");
            }
        }
    }
}

void ConfigParser::parse_multicycle_policy_section(const std::string& content,
                                                   Config& config) const {
    std::istringstream iss(content);
    std::string line;
    bool in_suppress_rules = false;

    while (std::getline(iss, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#')
            continue;

        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = trim(line.substr(0, colon));
            std::string value = to_lower(trim(line.substr(colon + 1)));

            if (to_lower(key) == "suppress_findings") {
                config.multicycle_path_policy.suppress_findings = (value == "true");
                in_suppress_rules = false;
            } else if (to_lower(key) == "suppress_rules") {
                in_suppress_rules = true;
                config.multicycle_path_policy.suppress_rules.clear();
                // Inline list: suppress_rules: [CDC001, CDC002]
                if (!value.empty() && value.front() == '[') {
                    in_suppress_rules = false;
                    std::string inner = value;
                    if (inner.front() == '[')
                        inner = inner.substr(1);
                    if (!inner.empty() && inner.back() == ']')
                        inner.pop_back();
                    std::istringstream vss(inner);
                    std::string rule;
                    while (std::getline(vss, rule, ',')) {
                        rule = trim(rule);
                        if (!rule.empty())
                            config.multicycle_path_policy.suppress_rules.push_back(rule);
                    }
                }
            }
        } else if (in_suppress_rules) {
            // YAML list item:  - CDC001
            std::string trimmed_line = trim(line);
            if (trimmed_line.front() == '-') {
                trimmed_line = trim(trimmed_line.substr(1));
            }
            if (!trimmed_line.empty())
                config.multicycle_path_policy.suppress_rules.push_back(trimmed_line);
        }
    }
}

void ConfigParser::parse_blackbox_section(const std::string& content, Config& config) const {
    std::istringstream iss(content);
    std::string line;
    BlackBoxConfig* cur = nullptr;

    while (std::getline(iss, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#')
            continue;

        // New entry:  - module_name: xpm_cdc_gray
        if (line[0] == '-') {
            std::string rest = trim(line.substr(1));
            size_t colon = rest.find(':');
            if (colon != std::string::npos) {
                std::string key = to_lower(trim(rest.substr(0, colon)));
                std::string value = strip_quotes(trim(rest.substr(colon + 1)));
                if (key == "module_name" && !value.empty()) {
                    config.blackboxes.emplace_back();
                    cur = &config.blackboxes.back();
                    cur->module_name = value;
                    continue;
                }
            }
            cur = nullptr;
            continue;
        }

        if (!cur)
            continue;

        size_t colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        std::string key = to_lower(trim(line.substr(0, colon)));
        std::string value = to_lower(strip_quotes(trim(line.substr(colon + 1))));

        if (key == "vendor")
            cur->vendor = value;
        else if (key == "is_safe_crossing")
            cur->is_safe_crossing = (value == "true");
        else if (key == "has_synchronizer")
            cur->has_synchronizer = (value == "true");
        else if (key == "has_gray_encoding")
            cur->has_gray_encoding = (value == "true");
        else if (key == "has_async_fifo")
            cur->has_async_fifo = (value == "true");
        else if (key == "has_handshake")
            cur->has_handshake = (value == "true");
    }
}

Config ConfigParser::parse_string(const std::string& content, std::string* error) const {
    Config config;

    std::string rules_section, waivers_section, output_section, false_paths_section,
        clock_groups_section, reset_policy_section, multicycle_policy_section, blackboxes_section;
    std::string current_section;

    std::istringstream iss(content);
    std::string line;
    std::string current_rule;

    size_t line_number = 0;
    while (std::getline(iss, line)) {
        line_number++;
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#')
            continue;

        std::string lower = to_lower(trimmed);
        if (lower == "rules:" || lower == "waivers:" || lower == "output:" ||
            lower == "false_paths:" || lower == "clock_groups:" || lower == "reset_policy:" ||
            lower == "multicycle_path_policy:" || lower == "blackboxes:") {
            current_section = to_lower(trimmed.substr(0, trimmed.size() - 1));
            current_rule.clear();
            continue;
        }

        if (line.find_first_not_of(" \t") == 0 && trimmed.back() == ':') {
            if (error) {
                *error = "Unknown config section at line " + std::to_string(line_number) + ": " +
                         trimmed;
            }
            return Config();
        }

        if (current_section.empty() && line.find_first_not_of(" \t") == 0) {
            if (error) {
                *error = "Unexpected top-level config entry at line " +
                         std::to_string(line_number) + ": " + trimmed;
            }
            return Config();
        }

        if (current_section == "rules") {
            if (trimmed.back() == ':' && trimmed.find(' ') == std::string::npos) {
                current_rule = trim(trimmed.substr(0, trimmed.size() - 1));
            } else if (!current_rule.empty()) {
                size_t colon = trimmed.find(':');
                if (colon != std::string::npos) {
                    std::string key = to_lower(trim(trimmed.substr(0, colon)));
                    std::string value = to_lower(strip_quotes(trim(trimmed.substr(colon + 1))));
                    bool invalid = (key == "enabled" && value != "true" && value != "false") ||
                                   (key == "severity" && value != "error" && value != "warning" &&
                                    value != "info");
                    if (invalid) {
                        if (error)
                            *error = "Invalid value at line " + std::to_string(line_number) + ": " +
                                     trimmed;
                        return Config();
                    }
                }
            }
        } else if (current_section == "output") {
            size_t colon = trimmed.find(':');
            if (colon != std::string::npos) {
                std::string key = to_lower(trim(trimmed.substr(0, colon)));
                std::string value = to_lower(strip_quotes(trim(trimmed.substr(colon + 1))));
                if (key == "suppress_reset_crossings" && value != "true" && value != "false") {
                    if (error)
                        *error =
                            "Invalid value at line " + std::to_string(line_number) + ": " + trimmed;
                    return Config();
                }
                if (key == "format" && value != "text" && value != "json" && value != "html") {
                    if (error)
                        *error = "Invalid output format at line " + std::to_string(line_number) +
                                 ": " + value;
                    return Config();
                }
            }
        }

        if (current_section == "rules") {
            rules_section += line + "\n";
        } else if (current_section == "waivers") {
            waivers_section += line + "\n";
        } else if (current_section == "output") {
            output_section += line + "\n";
        } else if (current_section == "false_paths") {
            false_paths_section += line + "\n";
        } else if (current_section == "clock_groups") {
            clock_groups_section += line + "\n";
        } else if (current_section == "reset_policy") {
            reset_policy_section += line + "\n";
        } else if (current_section == "multicycle_path_policy") {
            multicycle_policy_section += line + "\n";
        } else if (current_section == "blackboxes") {
            blackboxes_section += line + "\n";
        }
    }

    if (!rules_section.empty())
        parse_rule_section(rules_section, config);
    if (!waivers_section.empty())
        parse_waiver_section(waivers_section, config);
    if (!output_section.empty())
        parse_output_section(output_section, config);
    if (!false_paths_section.empty())
        parse_false_path_section(false_paths_section, config);
    if (!clock_groups_section.empty()) {
        std::istringstream ciss(clock_groups_section);
        std::string line;
        ClockGroupConfig* cur = nullptr;
        while (std::getline(ciss, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#')
                continue;
            if (line.back() == ':' && line.size() > 1) {
                config.clock_groups.emplace_back();
                cur = &config.clock_groups.back();
                cur->exclusive = true;
                continue;
            }
            if (!cur)
                continue;
            size_t colon = line.find(':');
            if (colon == std::string::npos)
                continue;
            std::string key = to_lower(trim(line.substr(0, colon)));
            std::string value = trim(line.substr(colon + 1));
            if (key == "clocks") {
                std::istringstream vss(value);
                std::string clk;
                while (std::getline(vss, clk, ',')) {
                    clk = trim(clk);
                    if (!clk.empty())
                        cur->clocks.push_back(clk);
                }
            } else if (key == "exclusive") {
                cur->exclusive = (to_lower(value) == "true");
            }
        }
    }

    if (!reset_policy_section.empty())
        parse_reset_policy_section(reset_policy_section, config);
    if (!multicycle_policy_section.empty())
        parse_multicycle_policy_section(multicycle_policy_section, config);
    if (!blackboxes_section.empty())
        parse_blackbox_section(blackboxes_section, config);

    return config;
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
