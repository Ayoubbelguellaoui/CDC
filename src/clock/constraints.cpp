#include "clock/constraints.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <regex>
#include <sstream>

namespace opencdc::clock {

static bool iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
            return false;
    }
    return true;
}

static bool wildcard_match(const std::string& pattern, const std::string& text) {
    size_t pi = 0, ti = 0;
    size_t star_pi = std::string::npos, star_ti = 0;
    while (ti < text.size()) {
        if (pi < pattern.size() && (pattern[pi] == '?' || pattern[pi] == text[ti])) {
            pi++;
            ti++;
        } else if (pi < pattern.size() && pattern[pi] == '*') {
            star_pi = pi++;
            star_ti = ti;
        } else if (star_pi != std::string::npos) {
            pi = star_pi + 1;
            ti = ++star_ti;
        } else {
            return false;
        }
    }
    while (pi < pattern.size() && pattern[pi] == '*')
        pi++;
    return pi == pattern.size();
}

bool pattern_matches(const std::string& pattern, const std::string& value) {
    if (pattern.empty() || value.empty())
        return false;
    if (iequals(pattern, value))
        return true;

    bool has_wildcard =
        pattern.find('*') != std::string::npos || pattern.find('?') != std::string::npos;

    if (!has_wildcard) {
        return value.find(pattern) != std::string::npos;
    }

    if (wildcard_match(pattern, value))
        return true;

    // Check against segments split by '.' first, then by '.' or '_'
    // (more permissive). The second pass handles hierarchical names
    // with underscores in the segment.
    for (int pass = 0; pass < 2; ++pass) {
        size_t pos = 0;
        while (pos <= value.size()) {
            size_t next = (pass == 0) ? value.find('.', pos) : value.find_first_of("._", pos);
            std::string segment =
                value.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
            if (wildcard_match(pattern, segment))
                return true;
            if (next == std::string::npos)
                break;
            pos = next + 1;
        }
    }

    return false;
}

static bool selector_matches(const std::string& pattern, const std::string& value) {
    return pattern_matches(pattern, value);
}

static bool any_through_matches(const std::vector<std::string>& through,
                                const std::vector<std::string>& path_nodes) {
    if (through.empty())
        return true;
    for (const auto& tp : through) {
        bool found = false;
        for (const auto& node : path_nodes) {
            if (pattern_matches(tp, node)) {
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }
    return true;
}

bool ClockConstraints::is_false_path(const PathMatchContext& ctx) const {
    for (const auto& fp : false_paths) {
        bool from_clock_set = !fp.from_clock.empty();
        bool to_clock_set = !fp.to_clock.empty();
        bool from_reg_set = !fp.from_reg.empty();
        bool to_reg_set = !fp.to_reg.empty();
        bool from_cell_set = !fp.from_cell.empty();
        bool to_cell_set = !fp.to_cell.empty();
        bool from_pin_set = !fp.from_pin.empty();
        bool to_pin_set = !fp.to_pin.empty();
        bool through_set = !fp.through.empty();

        bool has_any_selector = from_clock_set || to_clock_set || from_reg_set || to_reg_set ||
                                from_cell_set || to_cell_set || from_pin_set || to_pin_set ||
                                through_set;

        if (!has_any_selector)
            continue;

        if (from_clock_set && !selector_matches(fp.from_clock, ctx.source_clock))
            continue;
        if (to_clock_set && !selector_matches(fp.to_clock, ctx.destination_clock))
            continue;
        if (from_reg_set && !selector_matches(fp.from_reg, ctx.source_register))
            continue;
        if (to_reg_set && !selector_matches(fp.to_reg, ctx.destination_register))
            continue;
        if (from_cell_set && !selector_matches(fp.from_cell, ctx.source_cell))
            continue;
        if (to_cell_set && !selector_matches(fp.to_cell, ctx.destination_cell))
            continue;
        if (from_pin_set && !selector_matches(fp.from_pin, ctx.source_pin))
            continue;
        if (to_pin_set && !selector_matches(fp.to_pin, ctx.destination_pin))
            continue;
        if (through_set && !any_through_matches(fp.through, ctx.path_nodes))
            continue;

        return true;
    }
    return false;
}

bool ClockConstraints::is_false_path(const std::string& from, const std::string& to) const {
    PathMatchContext ctx;
    ctx.source_clock = from;
    ctx.destination_clock = to;
    ctx.source_register = from;
    ctx.destination_register = to;
    return is_false_path(ctx);
}

bool ClockConstraints::is_asynchronous(const std::string& clk1, const std::string& clk2) const {
    for (const auto& g1 : clock_groups) {
        if (!g1.asynchronous && !g1.exclusive)
            continue;
        for (const auto& g2 : clock_groups) {
            if (&g1 == &g2)
                continue;
            if (g1.set_id != g2.set_id)
                continue;

            bool clk1_in_g1 = false, clk2_in_g2 = false;
            for (const auto& clk : g1.clocks) {
                if (pattern_matches(clk, clk1))
                    clk1_in_g1 = true;
            }
            for (const auto& clk : g2.clocks) {
                if (pattern_matches(clk, clk2))
                    clk2_in_g2 = true;
            }
            if (clk1_in_g1 && clk2_in_g2)
                return true;
        }
    }
    return false;
}

std::optional<ClockDefinition> ClockConstraints::get_clock(const std::string& name) const {
    auto it = clock_map.find(name);
    if (it != clock_map.end())
        return it->second;

    for (const auto& clk : clocks) {
        if (pattern_matches(clk.name, name)) {
            return clk;
        }
    }
    return std::nullopt;
}

std::string SdcReader::extract_quoted_string(const std::string& s, size_t& pos) {
    while (pos < s.size() && std::isspace(s[pos]))
        pos++;

    if (pos >= s.size() || s[pos] != '"')
        return "";
    pos++;

    std::string result;
    while (pos < s.size() && s[pos] != '"') {
        result += s[pos++];
    }
    if (pos < s.size())
        pos++;
    return result;
}

std::vector<std::string> SdcReader::tokenize(const std::string& line) {
    std::vector<std::string> tokens;
    std::string current;
    bool in_quotes = false;
    int bracket_depth = 0;

    for (char c : line) {
        if (c == '"') {
            in_quotes = !in_quotes;
            current += c;
        } else if (!in_quotes && c == '[') {
            bracket_depth++;
            current += c;
        } else if (!in_quotes && c == ']') {
            if (bracket_depth > 0)
                bracket_depth--;
            current += c;
        } else if (std::isspace(c) && !in_quotes && bracket_depth == 0) {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else {
            current += c;
        }
    }
    if (!current.empty())
        tokens.push_back(current);

    return tokens;
}

std::vector<std::string> SdcReader::extract_bracket_args(const std::string& token) {
    std::vector<std::string> args;
    size_t start = token.find('[');
    size_t end = token.rfind(']');
    if (start == std::string::npos || end == std::string::npos || end <= start)
        return args;

    std::string inner = token.substr(start + 1, end - start - 1);

    size_t brace_start = inner.find('{');
    size_t brace_end = inner.rfind('}');
    if (brace_start != std::string::npos && brace_end != std::string::npos &&
        brace_end > brace_start) {
        std::string list_content = inner.substr(brace_start + 1, brace_end - brace_start - 1);
        std::string item;
        for (char c : list_content) {
            if (std::isspace(c) || c == ',') {
                if (!item.empty()) {
                    if (item.front() == '"' && item.back() == '"')
                        item = item.substr(1, item.size() - 2);
                    args.push_back(item);
                    item.clear();
                }
            } else {
                item += c;
            }
        }
        if (!item.empty()) {
            if (item.front() == '"' && item.back() == '"')
                item = item.substr(1, item.size() - 2);
            args.push_back(item);
        }
        return args;
    }

    std::string cmd;
    size_t pos = 0;
    while (pos < inner.size() && !std::isspace(inner[pos]) && inner[pos] != ']')
        cmd += inner[pos++];

    std::string rest;
    while (pos < inner.size())
        rest += inner[pos++];

    auto strip_quotes = [](std::string s) -> std::string {
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
            return s.substr(1, s.size() - 2);
        return s;
    };

    if (cmd == "get_clocks" || cmd == "get_cells" || cmd == "get_pins") {
        std::string arg;
        for (char c : rest) {
            if (std::isspace(c) || c == ']') {
                if (!arg.empty()) {
                    args.push_back(strip_quotes(arg));
                    arg.clear();
                }
            } else {
                arg += c;
            }
        }
        if (!arg.empty())
            args.push_back(strip_quotes(arg));
    }

    return args;
}

void SdcReader::parse_create_clock(const std::string& line, ClockConstraints& constraints) {
    auto tokens = tokenize(line);
    ClockDefinition clk;

    for (size_t i = 1; i < tokens.size(); ++i) {
        const auto& tok = tokens[i];
        if (tok == "-name" && i + 1 < tokens.size()) {
            clk.name = tokens[++i];
            if (!clk.name.empty() && clk.name.front() == '"') {
                clk.name = clk.name.substr(1);
                if (!clk.name.empty() && clk.name.back() == '"') {
                    clk.name.pop_back();
                }
            }
        } else if (tok == "-period" && i + 1 < tokens.size()) {
            try {
                clk.period_ns = std::stod(tokens[++i]);
                if (!std::isfinite(clk.period_ns) || clk.period_ns <= 0) {
                    clk.period_ns = 0;
                } else {
                    clk.frequency_mhz = 1000.0 / clk.period_ns;
                }
            } catch (const std::invalid_argument&) {
                constraints.warnings.push_back("Invalid period value in create_clock: '" +
                                               tokens[i] + "'");
            } catch (const std::out_of_range&) {
                constraints.warnings.push_back("Period value out of range in create_clock: '" +
                                               tokens[i] + "'");
            }
        } else if (tok.find('/') != std::string::npos ||
                   (!tok.empty() && tok[0] != '-' &&
                    tok.find_first_of("0123456789") == std::string::npos)) {
            if (clk.name.empty()) {
                std::string port = tok;
                auto bracket_args = extract_bracket_args(port);
                if (!bracket_args.empty()) {
                    port = bracket_args[0];
                } else {
                    if (!port.empty() && port.front() == '"') {
                        port = port.substr(1);
                        if (!port.empty() && port.back() == '"') {
                            port.pop_back();
                        }
                    }
                }
                size_t last_slash = port.rfind('/');
                if (last_slash != std::string::npos) {
                    clk.name = port.substr(last_slash + 1);
                } else {
                    clk.name = port;
                }
            }
        }
    }

    if (!clk.name.empty()) {
        constraints.clocks.push_back(clk);
        constraints.clock_map[clk.name] = clk;
    }
}

void SdcReader::parse_create_generated_clock(const std::string& line,
                                             ClockConstraints& constraints) {
    auto tokens = tokenize(line);
    ClockDefinition clk;
    clk.is_generated = true;

    for (size_t i = 1; i < tokens.size(); ++i) {
        const auto& tok = tokens[i];
        if (tok == "-name" && i + 1 < tokens.size()) {
            clk.name = tokens[++i];
            if (!clk.name.empty() && clk.name.front() == '"') {
                clk.name = clk.name.substr(1);
                if (!clk.name.empty() && clk.name.back() == '"') {
                    clk.name.pop_back();
                }
            }
        } else if (tok == "-master_clock" && i + 1 < tokens.size()) {
            clk.master_clock = tokens[++i];
            if (!clk.master_clock.empty() && clk.master_clock.front() == '"') {
                clk.master_clock = clk.master_clock.substr(1);
                if (!clk.master_clock.empty() && clk.master_clock.back() == '"') {
                    clk.master_clock.pop_back();
                }
            }
        } else if (tok == "-divide_by" && i + 1 < tokens.size()) {
            try {
                clk.divider_ratio = std::stod(tokens[++i]);
            } catch (const std::invalid_argument&) {
                constraints.warnings.push_back(
                    "Invalid divider value in create_generated_clock: '" + tokens[i] + "'");
            } catch (const std::out_of_range&) {
            }
        } else if (tok == "-multiply_by" && i + 1 < tokens.size()) {
            try {
                clk.multiplier_ratio = std::stod(tokens[++i]);
            } catch (const std::invalid_argument&) {
                constraints.warnings.push_back(
                    "Invalid multiplier value in create_generated_clock: '" + tokens[i] + "'");
            } catch (const std::out_of_range&) {
            }
        }
    }

    if (!clk.name.empty()) {
        constraints.clocks.push_back(clk);
        constraints.clock_map[clk.name] = clk;
    }
}

void SdcReader::parse_set_false_path(const std::string& line, ClockConstraints& constraints) {
    auto tokens = tokenize(line);
    FalsePath fp;

    for (size_t i = 0; i < tokens.size(); ++i) {
        const auto& tok = tokens[i];
        if (tok == "-from" && i + 1 < tokens.size()) {
            std::string from = tokens[++i];
            auto args = extract_bracket_args(from);
            if (!args.empty()) {
                bool is_clock_cmd = from.find("get_clocks") != std::string::npos;
                bool is_cell_cmd = from.find("get_cells") != std::string::npos;
                bool is_pin_cmd = from.find("get_pins") != std::string::npos;
                if (is_clock_cmd) {
                    fp.from_clock = args[0];
                } else if (is_cell_cmd) {
                    fp.from_cell = args[0];
                } else if (is_pin_cmd) {
                    fp.from_pin = args[0];
                } else {
                    fp.from_reg = args[0];
                }
            } else {
                auto strip = [](std::string s) -> std::string {
                    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
                        return s.substr(1, s.size() - 2);
                    return s;
                };
                fp.from_reg = strip(from);
            }
        } else if (tok == "-to" && i + 1 < tokens.size()) {
            std::string to = tokens[++i];
            auto args = extract_bracket_args(to);
            if (!args.empty()) {
                bool is_clock_cmd = to.find("get_clocks") != std::string::npos;
                bool is_cell_cmd = to.find("get_cells") != std::string::npos;
                bool is_pin_cmd = to.find("get_pins") != std::string::npos;
                if (is_clock_cmd) {
                    fp.to_clock = args[0];
                } else if (is_cell_cmd) {
                    fp.to_cell = args[0];
                } else if (is_pin_cmd) {
                    fp.to_pin = args[0];
                } else {
                    fp.to_reg = args[0];
                }
            } else {
                auto strip = [](std::string s) -> std::string {
                    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
                        return s.substr(1, s.size() - 2);
                    return s;
                };
                fp.to_reg = strip(to);
            }
        } else if (tok == "-through" && i + 1 < tokens.size()) {
            std::string through = tokens[++i];
            auto args = extract_bracket_args(through);
            if (!args.empty()) {
                for (const auto& a : args)
                    fp.through.push_back(a);
            } else {
                auto strip = [](std::string s) -> std::string {
                    if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
                        return s.substr(1, s.size() - 2);
                    return s;
                };
                fp.through.push_back(strip(through));
            }
        }
    }

    constraints.false_paths.push_back(fp);
}

void SdcReader::parse_set_multicycle_path(const std::string& line, ClockConstraints& constraints) {
    auto tokens = tokenize(line);
    MultiCyclePath mcp;
    std::vector<std::string> from_args;
    std::vector<std::string> to_args;
    bool from_is_clock = false;
    bool to_is_clock = false;

    for (size_t i = 0; i < tokens.size(); ++i) {
        const auto& tok = tokens[i];
        if (tok == "-from" && i + 1 < tokens.size()) {
            std::string from = tokens[++i];
            from_args = extract_bracket_args(from);
            from_is_clock = from.find("get_clocks") != std::string::npos;
            if (from_args.empty()) {
                from_args = {from};
                from_is_clock = true;  // bare name assumed to be a clock
            }
        } else if (tok == "-to" && i + 1 < tokens.size()) {
            std::string to = tokens[++i];
            to_args = extract_bracket_args(to);
            to_is_clock = to.find("get_clocks") != std::string::npos;
            if (to_args.empty()) {
                to_args = {to};
                to_is_clock = true;
            }
        } else if (!tok.empty() && tok[0] != '-' &&
                   std::all_of(tok.begin(), tok.end(), [](unsigned char c) {
                       return c == '-' || c == '+' || std::isdigit(c);
                   })) {
            try {
                int val = std::stoi(tok);
                if (val >= 1)
                    mcp.cycles = val;
            } catch (const std::invalid_argument&) {
            } catch (const std::out_of_range&) {
            }
        }
    }

    // Only create MCP entries for get_clocks or bare-name args. Non-clock args
    // (get_cells, get_pins) are rejected since we only match against clock domains.
    if (!from_is_clock && !to_is_clock)
        return;

    // Create entries for all combinations of from × to args.
    if (from_args.empty())
        from_args = {""};
    if (to_args.empty())
        to_args = {""};
    for (const auto& fa : from_args) {
        for (const auto& ta : to_args) {
            MultiCyclePath entry = mcp;
            entry.from_clock = fa;
            entry.to_clock = ta;
            constraints.multi_cycle_paths.push_back(entry);
        }
    }
}

void SdcReader::parse_set_clock_groups(const std::string& line, ClockConstraints& constraints) {
    auto tokens = tokenize(line);

    bool asynchronous = false;
    bool exclusive = false;
    for (const auto& tok : tokens) {
        if (tok == "-asynchronous")
            asynchronous = true;
        else if (tok == "-exclusive")
            exclusive = true;
    }

    int set_id = static_cast<int>(constraints.clock_groups.size());
    ClockGroup current_group;
    current_group.asynchronous = asynchronous;
    current_group.exclusive = exclusive;
    current_group.set_id = set_id;

    for (size_t i = 1; i < tokens.size(); ++i) {
        const auto& tok = tokens[i];
        if (tok == "-group") {
            if (!current_group.clocks.empty()) {
                current_group.name = "group_" + std::to_string(constraints.clock_groups.size());
                constraints.clock_groups.push_back(current_group);
                current_group.clocks.clear();
            }
        } else if (!tok.empty() && tok[0] != '-' && tok != "-asynchronous" && tok != "-exclusive") {
            if (tok.find("get_clocks") != std::string::npos) {
                size_t start = tok.find('[');
                size_t end = tok.rfind(']');
                if (start != std::string::npos && end != std::string::npos && end > start) {
                    std::string clk = tok.substr(start + 1, end - start - 1);
                    if (!clk.empty() && clk.front() == '"') {
                        clk = clk.substr(1);
                        if (!clk.empty() && clk.back() == '"') {
                            clk.pop_back();
                        }
                    }
                    if (clk.find("get_clocks ") == 0) {
                        clk = clk.substr(11);
                    }
                    // Handle brace-enclosed multi-clock lists: {clk1 clk2 clk3}
                    size_t brace_start = clk.find('{');
                    size_t brace_end = clk.rfind('}');
                    if (brace_start != std::string::npos && brace_end != std::string::npos &&
                        brace_end > brace_start) {
                        std::string list_content =
                            clk.substr(brace_start + 1, brace_end - brace_start - 1);
                        std::string item;
                        for (char c : list_content) {
                            if (std::isspace(c) || c == ',') {
                                if (!item.empty()) {
                                    if (item.front() == '"' && item.back() == '"')
                                        item = item.substr(1, item.size() - 2);
                                    current_group.clocks.push_back(item);
                                    item.clear();
                                }
                            } else {
                                item += c;
                            }
                        }
                        if (!item.empty()) {
                            if (item.front() == '"' && item.back() == '"')
                                item = item.substr(1, item.size() - 2);
                            current_group.clocks.push_back(item);
                        }
                    } else {
                        current_group.clocks.push_back(clk);
                    }
                }
            } else {
                // Bare clock name (e.g. `set_clock_groups -group clk1 -group clk2`)
                std::string clk = tok;
                if (clk.front() == '"' && clk.back() == '"')
                    clk = clk.substr(1, clk.size() - 2);
                current_group.clocks.push_back(clk);
            }
        }
    }

    if (!current_group.clocks.empty()) {
        current_group.name = "group_" + std::to_string(constraints.clock_groups.size());
        constraints.clock_groups.push_back(current_group);
    }
}

ClockConstraints SdcReader::read_sdc(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open())
        return ClockConstraints{};

    static constexpr size_t MAX_FILE_SIZE = 2 * 1024 * 1024;
    auto file_size = file.tellg();
    if (file_size > static_cast<std::streampos>(MAX_FILE_SIZE))
        return ClockConstraints{};
    file.seekg(0, std::ios::beg);

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return parse_sdc_content(content);
}

ClockConstraints SdcReader::parse_sdc_content(const std::string& content) {
    ClockConstraints constraints;
    std::istringstream iss(content);
    std::string raw_line;
    std::string accumulated;

    while (std::getline(iss, raw_line)) {
        size_t comment = raw_line.find('#');
        if (comment != std::string::npos) {
            raw_line = raw_line.substr(0, comment);
        }

        while (!raw_line.empty() && std::isspace(raw_line.back())) {
            raw_line.pop_back();
        }

        bool continuation = !raw_line.empty() && raw_line.back() == '\\';
        if (continuation) {
            raw_line.pop_back();
            while (!raw_line.empty() && std::isspace(raw_line.back())) {
                raw_line.pop_back();
            }
            accumulated += raw_line;
            continue;
        }

        accumulated += raw_line;

        {
            auto it = std::find_if(accumulated.begin(), accumulated.end(),
                                   [](unsigned char c) { return !std::isspace(c); });
            accumulated.erase(accumulated.begin(), it);
        }
        while (!accumulated.empty() && std::isspace(accumulated.back())) {
            accumulated.pop_back();
        }

        if (accumulated.empty()) {
            accumulated.clear();
            continue;
        }

        std::string line = std::move(accumulated);
        accumulated.clear();

        if (line.find("create_clock") == 0) {
            parse_create_clock(line, constraints);
        } else if (line.find("create_generated_clock") == 0) {
            parse_create_generated_clock(line, constraints);
        } else if (line.find("set_false_path") == 0) {
            parse_set_false_path(line, constraints);
        } else if (line.find("set_multicycle_path") == 0) {
            parse_set_multicycle_path(line, constraints);
        } else if (line.find("set_clock_groups") == 0) {
            parse_set_clock_groups(line, constraints);
        }
    }

    return constraints;
}

std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string to_lower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return r;
}

void ConstraintsParser::parse_clocks_section(const std::string& content,
                                             ClockConstraints& constraints) {
    std::istringstream iss(content);
    std::string line;
    ClockDefinition* current_clock = nullptr;

    while (std::getline(iss, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#')
            continue;

        if (line.back() == ':' && line.size() > 1) {
            std::string name = trim(line.substr(0, line.size() - 1));
            if (name.find(' ') == std::string::npos && name.find('-') == std::string::npos) {
                constraints.clocks.push_back(ClockDefinition{});
                current_clock = &constraints.clocks.back();
                current_clock->name = name;
                continue;
            }
        }

        if (current_clock) {
            size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::string key = to_lower(trim(line.substr(0, colon)));
                std::string value = trim(line.substr(colon + 1));

                if (key == "frequency" || key == "frequency_mhz") {
                    try {
                        auto v = std::stod(value);
                        if (std::isfinite(v) && v >= 0)
                            current_clock->frequency_mhz = v;
                    } catch (...) {
                    }
                } else if (key == "period" || key == "period_ns") {
                    try {
                        auto v = std::stod(value);
                        if (std::isfinite(v) && v > 0)
                            current_clock->period_ns = v;
                    } catch (...) {
                    }
                } else if (key == "source") {
                    current_clock->source = value;
                } else if (key == "master_clock") {
                    current_clock->master_clock = value;
                    current_clock->is_generated = true;
                } else if (key == "divider" || key == "divider_ratio") {
                    try {
                        auto v = std::stod(value);
                        if (std::isfinite(v) && v > 0)
                            current_clock->divider_ratio = v;
                    } catch (...) {
                    }
                } else if (key == "multiplier" || key == "multiplier_ratio") {
                    try {
                        auto v = std::stod(value);
                        if (std::isfinite(v) && v > 0)
                            current_clock->multiplier_ratio = v;
                    } catch (...) {
                    }
                }
            }
        }
    }

    for (const auto& clk : constraints.clocks) {
        constraints.clock_map[clk.name] = clk;
    }
}

void ConstraintsParser::parse_false_paths_section(const std::string& content,
                                                  ClockConstraints& constraints) {
    std::istringstream iss(content);
    std::string line;

    while (std::getline(iss, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#')
            continue;
        if (line[0] != '-')
            continue;

        FalsePath fp;
        std::string rest = trim(line.substr(1));
        std::istringstream fiss(rest);
        std::string token;

        while (std::getline(fiss, token, ',')) {
            token = trim(token);
            size_t colon = token.find(':');
            if (colon == std::string::npos)
                continue;

            std::string key = to_lower(trim(token.substr(0, colon)));
            std::string value = trim(token.substr(colon + 1));
            if (!value.empty() && value.front() == '"') {
                value = value.substr(1);
                if (!value.empty() && value.back() == '"') {
                    value.pop_back();
                }
            }

            if (key == "from_clock" || key == "from")
                fp.from_clock = value;
            else if (key == "to_clock" || key == "to")
                fp.to_clock = value;
            else if (key == "from_reg")
                fp.from_reg = value;
            else if (key == "to_reg")
                fp.to_reg = value;
            else if (key == "source")
                fp.from_reg = value;
            else if (key == "dest")
                fp.to_reg = value;
            else if (key == "through")
                fp.through.push_back(value);
            else if (key == "reason")
                fp.reason = value;
        }

        constraints.false_paths.push_back(fp);
    }
}

void ConstraintsParser::parse_multi_cycle_section(const std::string& content,
                                                  ClockConstraints& constraints) {
    std::istringstream iss(content);
    std::string line;

    while (std::getline(iss, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#')
            continue;
        if (line[0] != '-')
            continue;

        MultiCyclePath mcp;
        std::string rest = trim(line.substr(1));
        std::istringstream fiss(rest);
        std::string token;

        while (std::getline(fiss, token, ',')) {
            token = trim(token);
            size_t colon = token.find(':');
            if (colon == std::string::npos)
                continue;

            std::string key = to_lower(trim(token.substr(0, colon)));
            std::string value = trim(token.substr(colon + 1));

            if (key == "from_clock" || key == "from")
                mcp.from_clock = value;
            else if (key == "to_clock" || key == "to")
                mcp.to_clock = value;
            else if (key == "cycles") {
                try {
                    mcp.cycles = std::stoi(value);
                } catch (...) {
                }
            }
        }

        constraints.multi_cycle_paths.push_back(mcp);
    }
}

void ConstraintsParser::parse_groups_section(const std::string& content,
                                             ClockConstraints& constraints) {
    std::istringstream iss(content);
    std::string line;
    ClockGroup* current_group = nullptr;

    while (std::getline(iss, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#')
            continue;

        if (line.back() == ':' && line.size() > 1) {
            constraints.clock_groups.push_back(ClockGroup{});
            current_group = &constraints.clock_groups.back();
            current_group->name = trim(line.substr(0, line.size() - 1));
            continue;
        }

        if (current_group) {
            size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::string key = to_lower(trim(line.substr(0, colon)));
                std::string value = trim(line.substr(colon + 1));

                if (key == "clocks") {
                    std::istringstream vss(value);
                    std::string clk;
                    while (std::getline(vss, clk, ',')) {
                        clk = trim(clk);
                        if (!clk.empty()) {
                            current_group->clocks.push_back(clk);
                        }
                    }
                } else if (key == "asynchronous") {
                    current_group->asynchronous = (to_lower(value) == "true");
                } else if (key == "exclusive") {
                    current_group->exclusive = (to_lower(value) == "true");
                }
            }
        }
    }
}

ClockConstraints ConstraintsParser::parse_yaml(const std::string& content) {
    ClockConstraints constraints;
    std::string clocks_section, false_paths_section, multi_cycle_section, groups_section;
    std::string current_section;

    std::istringstream iss(content);
    std::string line;

    while (std::getline(iss, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#')
            continue;

        std::string lower = to_lower(trimmed);
        if (lower == "clocks:" || lower == "false_paths:" || lower == "multi_cycle_paths:" ||
            lower == "clock_groups:") {
            current_section = lower.substr(0, lower.size() - 1);
            continue;
        }

        if (current_section == "clocks") {
            clocks_section += line + "\n";
        } else if (current_section == "false_paths") {
            false_paths_section += line + "\n";
        } else if (current_section == "multi_cycle_paths") {
            multi_cycle_section += line + "\n";
        } else if (current_section == "clock_groups") {
            groups_section += line + "\n";
        }
    }

    if (!clocks_section.empty())
        parse_clocks_section(clocks_section, constraints);
    if (!false_paths_section.empty())
        parse_false_paths_section(false_paths_section, constraints);
    if (!multi_cycle_section.empty())
        parse_multi_cycle_section(multi_cycle_section, constraints);
    if (!groups_section.empty())
        parse_groups_section(groups_section, constraints);

    return constraints;
}

ClockConstraints ConstraintsParser::parse_file(const std::string& path, std::string* error) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        if (error)
            *error = "Could not open constraints file: " + path;
        return ClockConstraints{};
    }

    static constexpr size_t MAX_FILE_SIZE = 2 * 1024 * 1024;
    auto file_size = file.tellg();
    if (file_size > static_cast<std::streampos>(MAX_FILE_SIZE)) {
        if (error)
            *error = "Constraints file exceeds 2MB limit: " + path;
        return ClockConstraints{};
    }
    file.seekg(0, std::ios::beg);

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    ClockConstraints constraints;
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".sdc") {
        SdcReader sdc_reader;
        constraints = sdc_reader.parse_sdc_content(content);
    } else {
        constraints = parse_yaml(content);
    }

    if (!trim(content).empty() && constraints.clocks.empty() && constraints.false_paths.empty() &&
        constraints.multi_cycle_paths.empty() && constraints.clock_groups.empty()) {
        if (error)
            *error = "No recognized constraints found in: " + path;
        return ClockConstraints{};
    }

    for (const auto& clock : constraints.clocks) {
        if (clock.name.empty() || !std::isfinite(clock.period_ns) ||
            !std::isfinite(clock.frequency_mhz) || clock.period_ns < 0.0 ||
            clock.frequency_mhz < 0.0) {
            if (error)
                *error = "Invalid clock definition in: " + path;
            return ClockConstraints{};
        }
    }
    for (const auto& path_info : constraints.multi_cycle_paths) {
        if (path_info.cycles < 1 || (path_info.from_clock.empty() && path_info.to_clock.empty())) {
            if (error)
                *error = "Invalid multicycle path in: " + path;
            return ClockConstraints{};
        }
    }

    return constraints;
}

}  // namespace opencdc::clock
