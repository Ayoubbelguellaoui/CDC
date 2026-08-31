#ifndef OPENCDC_CLOCK_CONSTRAINTS_H
#define OPENCDC_CLOCK_CONSTRAINTS_H

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

namespace opencdc::clock {

bool pattern_matches(const std::string& pattern, const std::string& value);

struct ClockDefinition {
    std::string name;
    double frequency_mhz = 0.0;
    double period_ns = 0.0;
    std::string source;
    bool is_virtual = false;
    bool is_generated = false;
    std::string master_clock;
    double divider_ratio = 1.0;
    double multiplier_ratio = 1.0;
    std::string waveform;
};

struct FalsePath {
    std::string from_clock;
    std::string to_clock;
    std::string from_reg;
    std::string to_reg;
    std::vector<std::string> through;
    std::string from_cell;
    std::string to_cell;
    std::string from_pin;
    std::string to_pin;
    std::string reason;
};

struct MultiCyclePath {
    std::string from_clock;
    std::string to_clock;
    int cycles = 1;
    std::string reason;
};

struct ClockGroup {
    std::string name;
    std::vector<std::string> clocks;
    bool asynchronous = false;
    bool exclusive = false;
    int set_id = -1;
};

struct PathMatchContext {
    std::string source_clock;
    std::string destination_clock;
    std::string source_register;
    std::string destination_register;
    std::string source_cell;
    std::string destination_cell;
    std::string source_pin;
    std::string destination_pin;
    std::vector<std::string> path_nodes;
};

struct ClockConstraints {
    std::vector<ClockDefinition> clocks;
    std::vector<FalsePath> false_paths;
    std::vector<MultiCyclePath> multi_cycle_paths;
    std::vector<ClockGroup> clock_groups;
    std::vector<std::string> warnings;

    std::unordered_map<std::string, ClockDefinition> clock_map;

    bool is_false_path(const PathMatchContext& ctx) const;
    bool is_false_path(const std::string& from, const std::string& to) const;
    bool is_asynchronous(const std::string& clk1, const std::string& clk2) const;
    std::optional<ClockDefinition> get_clock(const std::string& name) const;
};

class SdcReader {
public:
    ClockConstraints read_sdc(const std::string& path);
    ClockConstraints parse_sdc_content(const std::string& content);

private:
    void parse_create_clock(const std::string& line, ClockConstraints& constraints);
    void parse_create_generated_clock(const std::string& line, ClockConstraints& constraints);
    void parse_set_false_path(const std::string& line, ClockConstraints& constraints);
    void parse_set_multicycle_path(const std::string& line, ClockConstraints& constraints);
    void parse_set_clock_groups(const std::string& line, ClockConstraints& constraints);

    std::string extract_quoted_string(const std::string& s, size_t& pos);
    std::vector<std::string> tokenize(const std::string& line);
    std::vector<std::string> extract_bracket_args(const std::string& token);
};

class ConstraintsParser {
public:
    ClockConstraints parse_yaml(const std::string& content);
    ClockConstraints parse_file(const std::string& path, std::string* error = nullptr);

private:
    void parse_clocks_section(const std::string& content, ClockConstraints& constraints);
    void parse_false_paths_section(const std::string& content, ClockConstraints& constraints);
    void parse_multi_cycle_section(const std::string& content, ClockConstraints& constraints);
    void parse_groups_section(const std::string& content, ClockConstraints& constraints);
};

} // namespace opencdc::clock

#endif // OPENCDC_CLOCK_CONSTRAINTS_H
