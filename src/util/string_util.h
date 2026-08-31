#ifndef OPENCDC_UTIL_STRING_UTIL_H
#define OPENCDC_UTIL_STRING_UTIL_H

#include <algorithm>
#include <cctype>
#include <string>

namespace opencdc::util {

/// Return a lowercased copy of \p s.
inline std::string to_lower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return r;
}

/// Case-insensitive wildcard match.
/// Supports '*' (any sequence of characters) and '?' (any single character).
/// Both pattern and text are lowercased before comparison so the match is
/// always case-insensitive, consistent with the waiver subsystem.
inline bool wildcard_match(const std::string& pattern, const std::string& text) {
    std::string lp = to_lower(pattern);
    std::string lt = to_lower(text);

    size_t pi = 0, ti = 0;
    size_t star_pi = std::string::npos;
    size_t star_ti = 0;

    while (ti < lt.size()) {
        if (pi < lp.size() && (lp[pi] == '?' || lp[pi] == lt[ti])) {
            ++pi; ++ti;
        } else if (pi < lp.size() && lp[pi] == '*') {
            star_pi = pi++;
            star_ti = ti;
        } else if (star_pi != std::string::npos) {
            pi = star_pi + 1;
            ti = ++star_ti;
        } else {
            return false;
        }
    }
    // Consume trailing '*' wildcards
    while (pi < lp.size() && lp[pi] == '*') ++pi;
    return pi == lp.size();
}

} // namespace opencdc::util

#endif // OPENCDC_UTIL_STRING_UTIL_H
