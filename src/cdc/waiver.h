#ifndef OPENCDC_CDC_WAIVER_H
#define OPENCDC_CDC_WAIVER_H

#include "cdc/crossing.h"
#include <string>
#include <vector>

namespace opencdc::cdc {

struct Waiver {
    std::string rule_id;
    std::string source_reg_name;
    std::string dest_reg_name;
    std::string source_domain;
    std::string dest_domain;
    std::string justification;
    std::string owner;
    std::string expiry;
};

class WaiverEngine {
public:
    bool load_from_file(const std::string& path);
    void add_waiver(const Waiver& w);

    std::vector<Finding> apply(const std::vector<Finding>& findings) const;
    bool matches(const Finding& f, const Waiver& w) const;

    const std::vector<Waiver>& waivers() const { return waivers_; }

private:
    std::vector<Waiver> waivers_;

    static bool is_expired(const std::string& expiry);
    static bool fields_match(const std::string& a, const std::string& b);
};

} // namespace opencdc::cdc

#endif // OPENCDC_CDC_WAIVER_H
