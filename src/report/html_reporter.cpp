#include "report/html_reporter.h"
#include "opencdc/version.h"
#include <sys/stat.h>
#include <sstream>
#include <algorithm>
#include <unordered_map>
#include <filesystem>

namespace opencdc::report {

void HtmlReporter::ensure_directory_exists(const std::string& path) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec) {
        throw std::runtime_error("Failed to create directory: " + path + " (" + ec.message() + ")");
    }
}

std::string HtmlReporter::escape_html(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': result += "&amp;"; break;
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            case '"': result += "&quot;"; break;
            case '\'': result += "&#39;"; break;
            default: result += c; break;
        }
    }
    return result;
}

std::string HtmlReporter::severity_class(const std::string& severity) {
    if (severity == "error") return "severity-error";
    if (severity == "warning") return "severity-warning";
    return "severity-info";
}

std::string HtmlReporter::severity_icon(const std::string& severity) {
    if (severity == "error") return "&#10060;";
    if (severity == "warning") return "&#9888;";
    return "&#8505;";
}

std::string HtmlReporter::generate_summary_dashboard(const std::vector<cdc::Finding>& findings) {
    std::unordered_map<std::string, int> by_severity;
    std::unordered_map<std::string, int> by_rule;
    int waived_count = 0;
    
    for (const auto& f : findings) {
        if (!f.waived) {
            by_severity[f.severity]++;
        }
        by_rule[f.rule_id]++;
        if (f.waived) waived_count++;
    }
    
    std::ostringstream html;
    html << "<div class=\"dashboard\">\n";
    html << "  <div class=\"summary-cards\">\n";
    html << "    <div class=\"card card-total\">\n";
    html << "      <div class=\"card-value\">" << findings.size() << "</div>\n";
    html << "      <div class=\"card-label\">Total Findings</div>\n";
    html << "    </div>\n";
    html << "    <div class=\"card card-error\">\n";
    html << "      <div class=\"card-value\">" << by_severity["error"] << "</div>\n";
    html << "      <div class=\"card-label\">Errors</div>\n";
    html << "    </div>\n";
    html << "    <div class=\"card card-warning\">\n";
    html << "      <div class=\"card-value\">" << by_severity["warning"] << "</div>\n";
    html << "      <div class=\"card-label\">Warnings</div>\n";
    html << "    </div>\n";
    html << "    <div class=\"card card-waived\">\n";
    html << "      <div class=\"card-value\">" << waived_count << "</div>\n";
    html << "      <div class=\"card-label\">Waived</div>\n";
    html << "    </div>\n";
    html << "  </div>\n";
    html << "</div>\n";
    
    return html.str();
}

std::string HtmlReporter::generate_findings_table(const std::vector<cdc::Finding>& findings,
                                                  bool include_source_snippets) {
    std::ostringstream html;
    html << "<table class=\"findings-table\">\n";
    html << "  <thead>\n";
    html << "    <tr>\n";
    html << "      <th>Rule</th>\n";
    html << "      <th>Severity</th>\n";
    html << "      <th>Source</th>\n";
    html << "      <th>Destination</th>\n";
    html << "      <th>Reason</th>\n";
    html << "      <th>Location</th>\n";
    html << "      <th>Status</th>\n";
    html << "    </tr>\n";
    html << "  </thead>\n";
    html << "  <tbody>\n";
    
    for (const auto& f : findings) {
        html << "    <tr class=\"finding-row " << severity_class(f.severity) << "\">\n";
        html << "      <td><span class=\"rule-id\">" << escape_html(f.rule_id) << "</span></td>\n";
        html << "      <td><span class=\"severity-badge\">" << severity_icon(f.severity) 
             << " " << escape_html(f.severity) << "</span></td>\n";
        html << "      <td><span class=\"register-name\">" << escape_html(f.source_reg_name) 
             << "</span><br><span class=\"domain-name\">" << escape_html(f.source_domain) 
             << "</span></td>\n";
        html << "      <td><span class=\"register-name\">" << escape_html(f.dest_reg_name) 
             << "</span><br><span class=\"domain-name\">" << escape_html(f.dest_domain) 
             << "</span></td>\n";
        html << "      <td class=\"reason-cell\">" << escape_html(f.reason) << "</td>\n";
        html << "      <td class=\"location-cell\">";
        if (include_source_snippets && !f.source_loc.file.empty()) {
            html << escape_html(f.source_loc.file) << ":" << f.source_loc.line;
        }
        html << "</td>\n";
        html << "      <td>";
        if (f.waived) {
            html << "<span class=\"waived-badge\">WAIVED</span>";
            if (!f.waiver_justification.empty()) {
                html << "<br><span class=\"waiver-reason\">" << escape_html(f.waiver_justification) 
                     << "</span>";
            }
        } else {
            html << "<span class=\"active-badge\">ACTIVE</span>";
        }
        html << "</td>\n";
        html << "    </tr>\n";
    }
    
    html << "  </tbody>\n";
    html << "</table>\n";
    
    return html.str();
}

std::string HtmlReporter::generate_severity_chart(const std::vector<cdc::Finding>& findings) {
    std::unordered_map<std::string, int> counts;
    for (const auto& f : findings) {
        if (f.waived) continue;
        counts[f.severity]++;
    }
    
    size_t active_count = 0;
    for (const auto& f : findings) {
        if (!f.waived) active_count++;
    }
    
    std::ostringstream html;
    html << "<div class=\"chart-container\">\n";
    html << "  <h3>By Severity</h3>\n";
    html << "  <div class=\"bar-chart\">\n";
    
    for (const auto& [sev, count] : counts) {
        int percent = active_count == 0 ? 0 : (count * 100 / active_count);
        html << "    <div class=\"bar-row\">\n";
        html << "      <span class=\"bar-label\">" << escape_html(sev) << "</span>\n";
        html << "      <div class=\"bar-track\">\n";
        html << "        <div class=\"bar-fill " << severity_class(sev) 
             << "\" style=\"width: " << percent << "%\"></div>\n";
        html << "      </div>\n";
        html << "      <span class=\"bar-value\">" << count << "</span>\n";
        html << "    </div>\n";
    }
    
    html << "  </div>\n";
    html << "</div>\n";
    
    return html.str();
}

std::string HtmlReporter::generate_rule_chart(const std::vector<cdc::Finding>& findings) {
    std::unordered_map<std::string, int> counts;
    for (const auto& f : findings) {
        counts[f.rule_id]++;
    }
    
    std::vector<std::pair<std::string, int>> sorted(counts.begin(), counts.end());
    std::sort(sorted.begin(), sorted.end(), 
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    std::ostringstream html;
    html << "<div class=\"chart-container\">\n";
    html << "  <h3>By Rule</h3>\n";
    html << "  <div class=\"bar-chart\">\n";
    
    for (const auto& [rule, count] : sorted) {
        int percent = findings.empty() ? 0 : (count * 100 / findings.size());
        html << "    <div class=\"bar-row\">\n";
        html << "      <span class=\"bar-label\">" << escape_html(rule) << "</span>\n";
        html << "      <div class=\"bar-track\">\n";
        html << "        <div class=\"bar-fill\" style=\"width: " << percent << "%\"></div>\n";
        html << "      </div>\n";
        html << "      <span class=\"bar-value\">" << count << "</span>\n";
        html << "    </div>\n";
    }
    
    html << "  </div>\n";
    html << "</div>\n";
    
    return html.str();
}

void HtmlReporter::write_css(const HtmlReportOptions& options) {
    std::string css = R"CSS(
:root {
    --bg-primary: #ffffff;
    --bg-secondary: #f5f5f5;
    --text-primary: #333333;
    --text-secondary: #666666;
    --border-color: #e0e0e0;
    --error-color: #dc3545;
    --warning-color: #ffc107;
    --info-color: #17a2b8;
    --waived-color: #28a745;
    --link-color: #007bff;
}

@media (prefers-color-scheme: dark) {
    :root {
        --bg-primary: #1a1a1a;
        --bg-secondary: #2d2d2d;
        --text-primary: #e0e0e0;
        --text-secondary: #a0a0a0;
        --border-color: #404040;
    }
}

body {
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, sans-serif;
    background-color: var(--bg-primary);
    color: var(--text-primary);
    margin: 0;
    padding: 20px;
    line-height: 1.6;
}

.container {
    max-width: 1400px;
    margin: 0 auto;
}

header {
    border-bottom: 1px solid var(--border-color);
    padding-bottom: 20px;
    margin-bottom: 30px;
}

header h1 {
    margin: 0;
    font-size: 2em;
}

header .subtitle {
    color: var(--text-secondary);
    margin-top: 5px;
}

nav {
    margin-top: 20px;
}

nav a {
    color: var(--link-color);
    text-decoration: none;
    margin-right: 20px;
    font-weight: 500;
}

nav a:hover {
    text-decoration: underline;
}

.dashboard {
    margin-bottom: 30px;
}

.summary-cards {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
    gap: 20px;
}

.card {
    background: var(--bg-secondary);
    border-radius: 8px;
    padding: 20px;
    text-align: center;
    border: 1px solid var(--border-color);
}

.card-value {
    font-size: 2.5em;
    font-weight: bold;
    margin-bottom: 5px;
}

.card-label {
    color: var(--text-secondary);
    font-size: 0.9em;
}

.card-error .card-value { color: var(--error-color); }
.card-warning .card-value { color: var(--warning-color); }
.card-waived .card-value { color: var(--waived-color); }

.charts {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
    gap: 30px;
    margin-bottom: 30px;
}

.chart-container {
    background: var(--bg-secondary);
    border-radius: 8px;
    padding: 20px;
    border: 1px solid var(--border-color);
}

.chart-container h3 {
    margin-top: 0;
    margin-bottom: 15px;
    font-size: 1.1em;
}

.bar-chart {
    display: flex;
    flex-direction: column;
    gap: 10px;
}

.bar-row {
    display: grid;
    grid-template-columns: 100px 1fr 50px;
    align-items: center;
    gap: 10px;
}

.bar-label {
    font-size: 0.9em;
    font-family: monospace;
}

.bar-track {
    height: 20px;
    background: var(--bg-primary);
    border-radius: 4px;
    overflow: hidden;
}

.bar-fill {
    height: 100%;
    background: var(--link-color);
    border-radius: 4px;
    transition: width 0.3s ease;
}

.bar-fill.severity-error { background: var(--error-color); }
.bar-fill.severity-warning { background: var(--warning-color); }
.bar-fill.severity-info { background: var(--info-color); }

.bar-value {
    text-align: right;
    font-weight: bold;
}

.findings-section {
    background: var(--bg-secondary);
    border-radius: 8px;
    padding: 20px;
    border: 1px solid var(--border-color);
}

.findings-section h2 {
    margin-top: 0;
}

.findings-table {
    width: 100%;
    border-collapse: collapse;
    font-size: 0.9em;
}

.findings-table th,
.findings-table td {
    padding: 12px;
    text-align: left;
    border-bottom: 1px solid var(--border-color);
}

.findings-table th {
    background: var(--bg-primary);
    font-weight: 600;
    position: sticky;
    top: 0;
}

.findings-table tr:hover {
    background: var(--bg-primary);
}

.rule-id {
    font-family: monospace;
    font-weight: bold;
}

.severity-badge {
    display: inline-flex;
    align-items: center;
    gap: 5px;
    padding: 4px 8px;
    border-radius: 4px;
    font-size: 0.85em;
    font-weight: 500;
}

.severity-error .severity-badge { background: rgba(220, 53, 69, 0.2); color: var(--error-color); }
.severity-warning .severity-badge { background: rgba(255, 193, 7, 0.2); color: var(--warning-color); }

.register-name {
    font-family: monospace;
    font-size: 0.85em;
}

.domain-name {
    font-size: 0.8em;
    color: var(--text-secondary);
}

.waived-badge {
    background: rgba(40, 167, 69, 0.2);
    color: var(--waived-color);
    padding: 2px 6px;
    border-radius: 4px;
    font-size: 0.8em;
    font-weight: bold;
}

.active-badge {
    background: rgba(220, 53, 69, 0.2);
    color: var(--error-color);
    padding: 2px 6px;
    border-radius: 4px;
    font-size: 0.8em;
    font-weight: bold;
}

.waiver-reason {
    font-size: 0.8em;
    color: var(--text-secondary);
    font-style: italic;
}

.location-cell {
    font-family: monospace;
    font-size: 0.85em;
}

.reason-cell {
    max-width: 400px;
}

.filter-controls {
    margin-bottom: 20px;
    display: flex;
    gap: 15px;
    flex-wrap: wrap;
}

.filter-controls input,
.filter-controls select {
    padding: 8px 12px;
    border: 1px solid var(--border-color);
    border-radius: 4px;
    background: var(--bg-primary);
    color: var(--text-primary);
}

.filter-controls input {
    flex: 1;
    min-width: 200px;
}

.no-findings {
    text-align: center;
    padding: 40px;
    color: var(--text-secondary);
}

.no-findings .icon {
    font-size: 3em;
    margin-bottom: 10px;
}

footer {
    margin-top: 40px;
    padding-top: 20px;
    border-top: 1px solid var(--border-color);
    text-align: center;
    color: var(--text-secondary);
    font-size: 0.85em;
}
)CSS";

    if (options.dark_mode) {
        css += "\n/* Explicit dark mode */\n:root { --bg-primary: #1a1a1a; --bg-secondary: #2d2d2d; --text-primary: #e0e0e0; --text-secondary: #a0a0a0; --border-color: #404040; }\n";
    }
    css += options.custom_css;
    
    std::ofstream file(options.output_dir + "/style.css");
    file << css;
}

void HtmlReporter::write_js(const HtmlReportOptions& options) {
    std::string js = R"JS(
document.addEventListener('DOMContentLoaded', function() {
    const searchInput = document.getElementById('search-input');
    const severityFilter = document.getElementById('severity-filter');
    const ruleFilter = document.getElementById('rule-filter');
    const table = document.querySelector('.findings-table tbody');
    
    if (!table) return;
    
    function filterRows() {
        const searchTerm = searchInput ? searchInput.value.toLowerCase() : '';
        const severityValue = severityFilter ? severityFilter.value : '';
        const ruleValue = ruleFilter ? ruleFilter.value : '';
        
        const rows = table.querySelectorAll('tr');
        let visibleCount = 0;
        
        rows.forEach(row => {
            const text = row.textContent.toLowerCase();
            const severity = row.classList.contains('severity-error') ? 'error' :
                            row.classList.contains('severity-warning') ? 'warning' : 'info';
            const ruleCell = row.querySelector('.rule-id');
            const rule = ruleCell ? ruleCell.textContent : '';
            
            const matchesSearch = !searchTerm || text.includes(searchTerm);
            const matchesSeverity = !severityValue || severity === severityValue;
            const matchesRule = !ruleValue || rule === ruleValue;
            
            if (matchesSearch && matchesSeverity && matchesRule) {
                row.style.display = '';
                visibleCount++;
            } else {
                row.style.display = 'none';
            }
        });
        
        const noResults = document.querySelector('.no-results');
        if (noResults) {
            noResults.style.display = visibleCount === 0 ? 'block' : 'none';
        }
    }
    
    if (searchInput) searchInput.addEventListener('input', filterRows);
    if (severityFilter) severityFilter.addEventListener('change', filterRows);
    if (ruleFilter) ruleFilter.addEventListener('change', filterRows);
    
    const rows = table.querySelectorAll('tr');
    rows.forEach(row => {
        row.addEventListener('click', function() {
            this.classList.toggle('expanded');
        });
    });
});
)JS";
    
    std::ofstream file(options.output_dir + "/script.js");
    file << js;
}

void HtmlReporter::generate_report(const std::vector<cdc::Finding>& findings,
                                   const HtmlReportOptions& options) {
    ensure_directory_exists(options.output_dir);
    
    write_css(options);
    write_js(options);
    
    std::ofstream index(options.output_dir + "/index.html");
    if (!index.is_open()) {
        throw std::runtime_error("Failed to write index.html in " + options.output_dir);
    }
    index << "<!DOCTYPE html>\n";
    index << "<html lang=\"en\">\n";
    index << "<head>\n";
    index << "  <meta charset=\"UTF-8\">\n";
    index << "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
    index << "  <title>" << escape_html(options.title) << "</title>\n";
    index << "  <link rel=\"stylesheet\" href=\"style.css\">\n";
    index << "</head>\n";
    index << "<body>\n";
    index << "  <div class=\"container\">\n";
    index << "    <header>\n";
    index << "      <h1>" << escape_html(options.title) << "</h1>\n";
    index << "      <div class=\"subtitle\">OpenCDC Static Analysis Report</div>\n";
    index << "      <nav>\n";
    index << "        <a href=\"index.html\">Dashboard</a>\n";
    index << "        <a href=\"findings.html\">All Findings</a>\n";
    index << "      </nav>\n";
    index << "    </header>\n";
    
    if (options.include_summary_dashboard) {
        index << generate_summary_dashboard(findings);
    }
    
    index << "    <div class=\"charts\">\n";
    index << generate_severity_chart(findings);
    index << generate_rule_chart(findings);
    index << "    </div>\n";
    
    index << "    <section class=\"findings-section\">\n";
    index << "      <h2>Recent Findings</h2>\n";
    
    size_t recent_count = std::min(findings.size(), size_t(10));
    std::vector<cdc::Finding> recent(findings.begin(), findings.begin() + recent_count);
     index << generate_findings_table(recent, options.include_source_snippets);
    
    index << "      <p style=\"text-align: center; margin-top: 20px;\">\n";
    index << "        <a href=\"findings.html\">View all " << findings.size() << " findings</a>\n";
    index << "      </p>\n";
    index << "    </section>\n";
    
    index << "    <footer>\n";
    index << "      Generated by OpenCDC v" << OPENCDC_VERSION << "\n";
    index << "    </footer>\n";
    index << "  </div>\n";
    index << "  <script src=\"script.js\"></script>\n";
    index << "</body>\n";
    index << "</html>\n";
    
    std::ofstream findings_file(options.output_dir + "/findings.html");
    if (!findings_file.is_open()) {
        throw std::runtime_error("Failed to write findings.html in " + options.output_dir);
    }
    findings_file << "<!DOCTYPE html>\n";
    findings_file << "<html lang=\"en\">\n";
    findings_file << "<head>\n";
    findings_file << "  <meta charset=\"UTF-8\">\n";
    findings_file << "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
    findings_file << "  <title>All Findings - " << escape_html(options.title) << "</title>\n";
    findings_file << "  <link rel=\"stylesheet\" href=\"style.css\">\n";
    findings_file << "</head>\n";
    findings_file << "<body>\n";
    findings_file << "  <div class=\"container\">\n";
    findings_file << "    <header>\n";
    findings_file << "      <h1>" << escape_html(options.title) << "</h1>\n";
    findings_file << "      <div class=\"subtitle\">All Findings</div>\n";
    findings_file << "      <nav>\n";
    findings_file << "        <a href=\"index.html\">Dashboard</a>\n";
    findings_file << "        <a href=\"findings.html\">All Findings</a>\n";
    findings_file << "      </nav>\n";
    findings_file << "    </header>\n";
    
    findings_file << "    <div class=\"filter-controls\">\n";
    findings_file << "      <input type=\"text\" id=\"search-input\" placeholder=\"Search findings...\">\n";
     findings_file << "      <select id=\"severity-filter\">\n";
    findings_file << "        <option value=\"\">All Severities</option>\n";
    findings_file << "        <option value=\"error\">Errors</option>\n";
     findings_file << "        <option value=\"warning\">Warnings</option>\n";
     findings_file << "        <option value=\"info\">Info</option>\n";
     findings_file << "      </select>\n";
     std::vector<std::string> rules;
     for (const auto& f : findings) {
         if (std::find(rules.begin(), rules.end(), f.rule_id) == rules.end()) rules.push_back(f.rule_id);
     }
     findings_file << "      <select id=\"rule-filter\">\n        <option value=\"\">All Rules</option>\n";
     for (const auto& rule : rules) {
         findings_file << "        <option value=\"" << escape_html(rule) << "\">" << escape_html(rule) << "</option>\n";
     }
     findings_file << "      </select>\n";
    findings_file << "    </div>\n";
    
    findings_file << "    <section class=\"findings-section\">\n";
    findings_file << "      <h2>" << findings.size() << " Findings</h2>\n";
     findings_file << generate_findings_table(findings, options.include_source_snippets);
     findings_file << "      <p class=\"no-results\" style=\"display:none\">No matching findings.</p>\n";
    findings_file << "    </section>\n";
    
    findings_file << "    <footer>\n";
    findings_file << "      Generated by OpenCDC v" << OPENCDC_VERSION << "\n";
    findings_file << "    </footer>\n";
    findings_file << "  </div>\n";
    findings_file << "  <script src=\"script.js\"></script>\n";
    findings_file << "</body>\n";
    findings_file << "</html>\n";
}

} // namespace opencdc::report
