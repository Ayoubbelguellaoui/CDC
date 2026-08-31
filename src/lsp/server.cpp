#include "lsp/server.h"
#include "analysis/analyzer.h"
#include <nlohmann/json.hpp>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <cstring>
#include <cerrno>
#include <cctype>
#include <sstream>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <cstdio>
#include <limits>

namespace opencdc {
namespace lsp {

namespace {
// Removes a temporary analysis file on scope exit, including early returns.
struct temp_file_guard {
    std::string path;
    ~temp_file_guard() {
        if (!path.empty()) std::remove(path.c_str());
    }
};
} // namespace

static bool write_all_fd(int fd, const char* data, size_t len) {
    size_t written = 0;
    while (written < len) {
        ssize_t n = send(fd, data + written, len - written, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        written += static_cast<size_t>(n);
    }
    return true;
}

static bool parse_content_length(const std::string& header, size_t* length) {
    std::istringstream lines(header);
    std::string line;
    bool found = false;
    size_t value = 0;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = line.substr(0, colon);
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (name != "content-length") continue;
        if (found) return false;
        found = true;
        size_t pos = colon + 1;
        while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) ++pos;
        if (pos == line.size()) return false;
        size_t parsed = 0;
        for (; pos < line.size(); ++pos) {
            unsigned char c = static_cast<unsigned char>(line[pos]);
            if (std::isspace(c)) {
                while (++pos < line.size()) {
                    if (!std::isspace(static_cast<unsigned char>(line[pos]))) return false;
                }
                break;
            }
            if (!std::isdigit(c)) return false;
            size_t digit = static_cast<size_t>(c - '0');
            if (parsed > (std::numeric_limits<size_t>::max() - digit) / 10) return false;
            parsed = parsed * 10 + digit;
        }
        value = parsed;
    }
    if (!found) return false;
    *length = value;
    return true;
}

LspServer::LspServer() {}

LspServer::~LspServer() {
    stop();
}

void LspServer::start(int port) {
    if (server_thread_.joinable()) {
        server_thread_.join();  // Join any prior run before restart
    }
    port_ = port;
    bound_port_ = 0;
    running_ = true;
    server_thread_ = std::thread(&LspServer::server_loop, this);
    // Wait until server is bound (or failed)
    {
        std::unique_lock<std::mutex> lock(startup_mutex_);
        startup_cv_.wait_for(lock, std::chrono::seconds(5), [this] {
            return bound_port_.load() != 0 || !running_.load();
        });
    }
}

void LspServer::stop() {
    running_ = false;
    cv_.notify_all();
    // Only shutdown to unblock blocking reads; server_loop owns FD cleanup
    {
        std::lock_guard<std::mutex> lock(socket_mutex_);
        if (client_fd_ >= 0) shutdown(client_fd_, SHUT_RDWR);
        if (socket_fd_ >= 0) shutdown(socket_fd_, SHUT_RDWR);
    }
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
    // Now safe to reset FDs — server_loop has exited
    client_fd_ = -1;
    socket_fd_ = -1;
}

void LspServer::set_publish_diagnostics_callback(PublishDiagnosticsCallback callback) {
    publish_callback_ = std::move(callback);
}

void LspServer::did_open(const TextDocument& document) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        open_documents_[document.uri] = document;
    }
    
    auto diagnostics = analyze_document(document.uri, document.text);
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        diagnostics_cache_[document.uri] = diagnostics;
    }
    
    if (publish_callback_) {
        PublishDiagnosticsParams params;
        params.uri = document.uri;
        params.diagnostics = diagnostics;
        publish_callback_(params);
    } else {
        PublishDiagnosticsParams params{document.uri, diagnostics};
        send_diagnostics_notification(params);
    }
}

void LspServer::did_change(const TextDocument& document) {
    {
        std::lock_guard<std::mutex> lock(cancel_mutex_);
        cancel_flags_[document.uri] = std::make_shared<std::atomic<bool>>(true);
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        open_documents_[document.uri] = document;
    }

    auto diagnostics = analyze_document(document.uri, document.text);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        diagnostics_cache_[document.uri] = diagnostics;
    }

    if (publish_callback_) {
        PublishDiagnosticsParams params;
        params.uri = document.uri;
        params.diagnostics = diagnostics;
        publish_callback_(params);
    } else {
        PublishDiagnosticsParams params{document.uri, diagnostics};
        send_diagnostics_notification(params);
    }
}

void LspServer::did_close(const std::string& uri) {
    {
        std::lock_guard<std::mutex> lock(cancel_mutex_);
        cancel_flags_.erase(uri);
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        open_documents_.erase(uri);
        diagnostics_cache_.erase(uri);
    }
}

void LspServer::did_save(const TextDocument& document) {
    did_change(document);
}

void LspServer::send_diagnostics_notification(const PublishDiagnosticsParams& params) {
    std::string content = nlohmann::json{
        {"jsonrpc", "2.0"},
        {"method", "textDocument/publishDiagnostics"},
        {"params", nlohmann::json::parse(serialize_diagnostics(params))}
    }.dump();
    std::string header = "Content-Length: " + std::to_string(content.size()) + "\r\n\r\n";

    std::lock_guard<std::mutex> lock(socket_mutex_);
    if (client_fd_ < 0 || !write_all_fd(client_fd_, header.c_str(), header.size()) ||
        !write_all_fd(client_fd_, content.c_str(), content.size())) {
        if (client_fd_ >= 0) running_ = false;
    }
}

std::vector<Diagnostic> LspServer::analyze_document(const std::string& uri, const std::string& content) {
    std::vector<Diagnostic> diagnostics;
    
    // D3+D4: Set per-URI cancellation flag, record start time
    std::shared_ptr<std::atomic<bool>> cancel_flag;
    {
        std::lock_guard<std::mutex> lock(cancel_mutex_);
        cancel_flag = std::make_shared<std::atomic<bool>>(false);
        cancel_flags_[uri] = cancel_flag;
    }
    auto start_time = std::chrono::steady_clock::now();
    
    // D4: Timeout/cancel check lambda
    auto check_timeout = [&]() -> bool {
        if (cancel_flag->load()) return true;
        if (analysis_timeout_sec_ > 0) {
            auto elapsed = std::chrono::steady_clock::now() - start_time;
            if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() >= analysis_timeout_sec_) {
                Diagnostic diag;
                diag.range.start.line = 0;
                diag.range.start.character = 0;
                diag.range.end.line = 0;
                diag.range.end.character = 0;
                diag.severity = "warning";
                diag.code = "analysis-timeout";
                diag.source = "opencdc";
                diag.message = "Analysis timed out after " + std::to_string(analysis_timeout_sec_) + "s";
                diagnostics.push_back(diag);
                return true;
            }
        }
        return false;
    };
    
    if (top_module_.empty()) {
        return diagnostics;
    }
    
    std::string file_path = uri;
    if (uri.find("file://") == 0) {
        file_path = uri.substr(7);
    }
    
    std::string analysis_content = content;
    bool use_content = !content.empty();
    if (allow_remote_) {
        // Never read a client-supplied URI on remote connections. Empty text is valid.
        use_content = true;
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = open_documents_.find(uri);
        if (it != open_documents_.end()) analysis_content = it->second.text;
        file_path.clear();
    }
    
    temp_file_guard temp_guard;
    if (use_content) {
        // Prefer a user-private temp directory to avoid world-readable exposure.
        // XDG_RUNTIME_DIR is mode 0700 on modern Linux systems.
        const char* xdg = std::getenv("XDG_RUNTIME_DIR");
        std::string tmpdir = (xdg && *xdg) ? std::string(xdg) : "/tmp";
        std::string tmpl_str = tmpdir + "/opencdc_lsp_XXXXXX.sv";

        // Restrict umask around mkstemps so the file is never world-readable,
        // even in the window between creation and fchmod.
        mode_t old_mask = umask(0077);
        std::vector<char> tmpl(tmpl_str.begin(), tmpl_str.end());
        tmpl.push_back('\0');
        int fd = mkstemps(tmpl.data(), 3);
        umask(old_mask);

        if (fd >= 0) {
            // Set temp_guard.path IMMEDIATELY so the file is cleaned up
            // even if fdopen later fails.
            temp_guard.path = tmpl.data();

            FILE* fp = fdopen(fd, "w");
            if (fp) {
                fputs(analysis_content.c_str(), fp);
                fclose(fp);
                file_path = temp_guard.path;
            } else {
                close(fd);
                // temp_guard will delete the file on scope exit
                Diagnostic diag;
                diag.range.start.line = 0;
                diag.range.start.character = 0;
                diag.range.end.line = 0;
                diag.range.end.character = 0;
                diag.severity = "error";
                diag.code = "temporary-file-error";
                diag.source = "opencdc";
                diag.message = "Unable to write temporary document";
                diagnostics.push_back(diag);
                return diagnostics;
            }
        } else {
            Diagnostic diag;
            diag.range.start.line = 0;
            diag.range.start.character = 0;
            diag.range.end.line = 0;
            diag.range.end.character = 0;
            diag.severity = "error";
            diag.code = "temporary-file-error";
            diag.source = "opencdc";
            diag.message = "Unable to create temporary document";
            diagnostics.push_back(diag);
            return diagnostics;
        }
    }

    analysis::AnalysisRequest request;
    request.input_files = {file_path};
    request.top_module = top_module_;
    request.config_path = config_path_;
    request.waiver_path = waiver_path_;
    request.constraints_path = constraints_path_;

    analysis::Analyzer analyzer;
    analysis::AnalysisResult result = analyzer.run(request);

    // D3+D4: Check after frontend elaboration (slow step)
    if (check_timeout()) return diagnostics;

    if (!result.ok) {
        Diagnostic diag;
        diag.range.start.line = 0;
        diag.range.start.character = 0;
        diag.range.end.line = 0;
        diag.range.end.character = 0;
        diag.severity = "error";
        diag.code = "parse-error";
        diag.source = "opencdc";
        for (const auto& err : result.errors) {
            diag.message += err + "\n";
        }
        diagnostics.push_back(diag);
        return diagnostics;
    }

    // D3+D4: Check after analysis
    if (check_timeout()) return diagnostics;

    for (const auto& f : result.findings) {
        if (f.waived) continue;

        Diagnostic diag;
        diag.range.start.line = f.source_loc.line > 0 ? f.source_loc.line - 1 : 0;
        diag.range.start.character = f.source_loc.col > 0 ? f.source_loc.col - 1 : 0;
        diag.range.end.line = diag.range.start.line;
        diag.range.end.character = diag.range.start.character + 10;

        diag.severity = f.severity;
        diag.code = f.rule_id;
        diag.source = "opencdc";
        diag.message = f.reason;

        diagnostics.push_back(diag);
    }

    return diagnostics;
}

void LspServer::server_loop() {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        running_ = false;
        startup_cv_.notify_all();
        return;
    }
    {
        std::lock_guard<std::mutex> lock(socket_mutex_);
        socket_fd_ = listen_fd;
    }
    
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    // Security: bind to loopback by default; explicit opt-in for remote
    if (allow_remote_) {
        address.sin_addr.s_addr = INADDR_ANY;
    } else {
        address.sin_addr.s_addr = inet_addr(bind_address_.c_str());
    }
    address.sin_port = htons(port_);
    
    if (bind(listen_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        close(listen_fd);
        std::lock_guard<std::mutex> lock(socket_mutex_);
        socket_fd_ = -1;
        running_ = false;
        { std::lock_guard<std::mutex> lock(startup_mutex_); }
        startup_cv_.notify_all();
        return;
    }
    
    // Discover actual port when port 0 was requested
    if (port_ == 0) {
        socklen_t addr_len = sizeof(address);
        if (getsockname(listen_fd, (struct sockaddr*)&address, &addr_len) == 0) {
            port_ = ntohs(address.sin_port);
        }
    }
    bound_port_ = port_;
    { std::lock_guard<std::mutex> lock(startup_mutex_); }
    startup_cv_.notify_all();
    
    if (listen(listen_fd, 5) < 0) {
        close(listen_fd);
        {
            std::lock_guard<std::mutex> lock(socket_mutex_);
            socket_fd_ = -1;
        }
        running_ = false;
        startup_cv_.notify_all();
        return;
    }
    
    while (running_) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(listen_fd, &read_fds);
        
        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        int activity = select(listen_fd + 1, &read_fds, nullptr, nullptr, &timeout);
        
        if (activity < 0 || !running_) break;
        
        if (FD_ISSET(listen_fd, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
            
            if (client_fd >= 0) {
                // Security: reject non-loopback clients unless explicitly allowed
                if (!allow_remote_) {
                    char client_ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
                    std::string ip_str(client_ip);
                    if (ip_str != "127.0.0.1" && ip_str != "::1") {
                        close(client_fd);
                        continue;
                    }
                }
                
                // Limit to single concurrent client
                if (client_fd_ >= 0) {
                    close(client_fd);
                    continue;
                }
                
                {
                    std::lock_guard<std::mutex> lock(socket_mutex_);
                    client_fd_ = client_fd;
                }
                
                // Set read timeout (30 seconds)
                struct timeval read_timeout;
                read_timeout.tv_sec = 30;
                read_timeout.tv_usec = 0;
                setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &read_timeout, sizeof(read_timeout));
                
                char buffer[4096];
                std::string message;
                
                while (running_) {
                    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
                    if (bytes_read <= 0) break;
                    
                    buffer[bytes_read] = '\0';
                    message += buffer;
                    
                    if (message.size() > 10 * 1024 * 1024) {
                        break;
                    }
                    
                    while (true) {
                        size_t header_end = message.find("\r\n\r\n");
                        if (header_end == std::string::npos) break;

                        size_t content_start = header_end + 4;
                        std::string header = message.substr(0, header_end);
                        size_t content_length = 0;
                        if (!parse_content_length(header, &content_length) ||
                            content_length > 10 * 1024 * 1024) {
                            running_ = false;
                            break;
                        }
                        if (content_length > message.size() - content_start)
                            break;

                        std::string content = message.substr(content_start, content_length);
                        message.erase(0, content_start + static_cast<size_t>(content_length));
                        std::string response;
                        try {
                            response = process_message(content);
                        } catch (...) {
                            // Use "null" id since we can't recover the real id here.
                            // This only fires for truly unexpected exceptions (not
                            // json::parse_error which is caught inside process_message).
                            response = serialize_error("null", -32603, "Internal error");
                        }

                        if (!response.empty()) {
                            std::string response_header =
                                "Content-Length: " + std::to_string(response.size()) + "\r\n\r\n";
                            std::lock_guard<std::mutex> lock(socket_mutex_);
                            if (!write_all_fd(client_fd, response_header.c_str(), response_header.size()) ||
                                !write_all_fd(client_fd, response.c_str(), response.size())) {
                                running_ = false;
                                break;
                            }
                        }
                    }
                }
                
                close(client_fd);
                {
                    std::lock_guard<std::mutex> lock(socket_mutex_);
                    if (client_fd_ == client_fd) client_fd_ = -1;
                }
            }
        }
    }
    
    close(listen_fd);
    {
        std::lock_guard<std::mutex> lock(socket_mutex_);
        if (socket_fd_ == listen_fd) socket_fd_ = -1;
    }
}

std::string LspServer::process_message(const std::string& message) {
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(message);
    } catch (const nlohmann::json::parse_error&) {
        return serialize_error("null", -32700, "Parse error");
    }

    bool is_notification = !j.contains("id");
    std::string id = is_notification ? "null" : j["id"].dump();

    if (!j.contains("method") || !j["method"].is_string()) {
        return serialize_error(id, -32600, "Invalid request");
    }

    std::string method = j["method"].get<std::string>();

    if (method == "initialize") {
        nlohmann::json result = {
            {"capabilities", {
                {"textDocumentSync", 1},
                {"diagnosticProvider", {
                    {"interFileDependencies", false},
                    {"workspaceDiagnostics", false}
                }}
            }}
        };
        return serialize_response(id, result.dump());
    } else if (method == "textDocument/didOpen") {
        if (!j.contains("params"))
            return is_notification ? "" : serialize_error(id, -32602, "Missing params");
        auto& params = j["params"];
        if (!params.contains("textDocument") || !params["textDocument"].is_object())
            return is_notification ? "" : serialize_error(id, -32602, "Missing textDocument");
        auto& document = params["textDocument"];
        std::string uri = document.value("uri", std::string());
        std::string text = document.value("text", std::string());
        if (uri.empty()) return is_notification ? "" : serialize_error(id, -32602, "Missing document URI");
        
        TextDocument doc;
        doc.uri = uri;
        doc.text = text;
        did_open(doc);
        
        return is_notification ? "" : serialize_response(id, "{}");
    } else if (method == "textDocument/didChange") {
        if (!j.contains("params"))
            return is_notification ? "" : serialize_error(id, -32602, "Missing params");
        auto& params = j["params"];
        if (!params.contains("textDocument") || !params["textDocument"].is_object() ||
            !params.contains("contentChanges") || !params["contentChanges"].is_array())
            return is_notification ? "" : serialize_error(id, -32602, "Invalid change params");
        std::string uri = params["textDocument"].value("uri", std::string());
        if (uri.empty() || params["contentChanges"].empty())
            return is_notification ? "" : serialize_error(id, -32602, "Missing document URI or content");
        auto& change = params["contentChanges"].back();
        if (!change.is_object() || !change.contains("text"))
            return serialize_error(id, -32602, "Missing change text");
        std::string text = change.value("text", std::string());
        
        TextDocument doc;
        doc.uri = uri;
        doc.text = text;
        did_change(doc);
        
        return is_notification ? "" : serialize_response(id, "{}");
    } else if (method == "textDocument/didClose") {
        if (!j.contains("params"))
            return serialize_error(id, -32602, "Missing params");
        auto& params = j["params"];
        if (!params.contains("textDocument") || !params["textDocument"].is_object())
            return serialize_error(id, -32602, "Missing textDocument");
        std::string uri = params["textDocument"].value("uri", std::string());
        if (uri.empty()) return serialize_error(id, -32602, "Missing document URI");
        did_close(uri);
        
        return is_notification ? "" : serialize_response(id, "{}");
    } else if (method == "textDocument/didSave") {
        if (!j.contains("params"))
            return serialize_error(id, -32602, "Missing params");
        auto& params = j["params"];
        if (!params.contains("textDocument") || !params["textDocument"].is_object())
            return serialize_error(id, -32602, "Missing textDocument");
        auto& document = params["textDocument"];
        std::string uri = document.value("uri", std::string());
        if (uri.empty()) return serialize_error(id, -32602, "Missing document URI");
        TextDocument doc;
        doc.uri = uri;
        doc.text = params.value("text", std::string());
        if (doc.text.empty()) {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = open_documents_.find(uri);
            if (it != open_documents_.end()) doc.text = it->second.text;
        }
        did_save(doc);
        return is_notification ? "" : serialize_response(id, "{}");
    } else if (method == "shutdown") {
        return serialize_response(id, "null");
    } else if (method == "exit") {
        running_ = false;
        return "";
    } else if (method == "$/cancelRequest") {
        // D3: Cancel all in-progress analyses (global cancel)
        {
            std::lock_guard<std::mutex> lock(cancel_mutex_);
            for (auto& [uri, flag] : cancel_flags_) {
                flag->store(true);
            }
        }
        return "";  // notification, no response
    }
    
    if (is_notification) return "";
    return serialize_error(id, -32601, "Method not found");
}

std::string LspServer::serialize_response(const std::string& id, const std::string& result) {
    nlohmann::json resp;
    resp["jsonrpc"] = "2.0";
    resp["id"] = nlohmann::json::parse(id);
    resp["result"] = nlohmann::json::parse(result);
    return resp.dump();
}

std::string LspServer::serialize_error(const std::string& id, int code, const std::string& message) {
    nlohmann::json resp;
    resp["jsonrpc"] = "2.0";
    resp["id"] = nlohmann::json::parse(id);
    resp["error"]["code"] = code;
    resp["error"]["message"] = message;
    return resp.dump();
}

std::string LspServer::serialize_diagnostics(const PublishDiagnosticsParams& params) {
    nlohmann::json result;
    result["uri"] = params.uri;
    nlohmann::json diags = nlohmann::json::array();
    
    for (const auto& diag : params.diagnostics) {
        nlohmann::json d;
        d["range"]["start"]["line"] = diag.range.start.line;
        d["range"]["start"]["character"] = diag.range.start.character;
        d["range"]["end"]["line"] = diag.range.end.line;
        d["range"]["end"]["character"] = diag.range.end.character;
        
        int severity_num = 2;
        if (diag.severity == "error") severity_num = 1;
        else if (diag.severity == "warning") severity_num = 2;
        else if (diag.severity == "info") severity_num = 3;
        
        d["severity"] = severity_num;
        d["code"] = diag.code;
        d["source"] = diag.source;
        d["message"] = diag.message;
        diags.push_back(d);
    }
    
    result["diagnostics"] = diags;
    return result.dump();
}

bool LspClient::write_all(int fd, const char* data, size_t len) {
    size_t written = 0;
    while (written < len) {
        ssize_t n = ::write(fd, data + written, len - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        written += static_cast<size_t>(n);
    }
    return true;
}

bool LspClient::connect(const std::string& host, int port) {
    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) return false;
    
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr);
    
    if (::connect(socket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
    
    return true;
}

void LspClient::disconnect() {
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
}

void LspClient::send_notification(const std::string& method, const std::string& params) {
    if (socket_fd_ < 0) return;
    
    nlohmann::json content;
    content["jsonrpc"] = "2.0";
    content["method"] = method;
    content["params"] = nlohmann::json::parse(params);
    std::string content_str = content.dump();
    std::string header = "Content-Length: " + std::to_string(content_str.size()) + "\r\n\r\n";
    
    write_all(socket_fd_, header.c_str(), header.size());
    write_all(socket_fd_, content_str.c_str(), content_str.size());
}

std::string LspClient::send_request(const std::string& method, const std::string& params) {
    if (socket_fd_ < 0) return "";
    
    static std::atomic<int> request_id{0};
    int id_val = request_id.fetch_add(1) + 1;
    
    nlohmann::json content;
    content["jsonrpc"] = "2.0";
    content["id"] = id_val;
    content["method"] = method;
    content["params"] = nlohmann::json::parse(params);
    std::string content_str = content.dump();
    std::string header = "Content-Length: " + std::to_string(content_str.size()) + "\r\n\r\n";
    
    write_all(socket_fd_, header.c_str(), header.size());
    write_all(socket_fd_, content_str.c_str(), content_str.size());
    
    char buffer[4096];
    std::string response;
    
    while (true) {
        ssize_t bytes_read = read(socket_fd_, buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0) break;
        
        buffer[bytes_read] = '\0';
        response += buffer;
        
        size_t header_end = response.find("\r\n\r\n");
        if (header_end != std::string::npos) {
            size_t content_start = header_end + 4;
            std::string hdr = response.substr(0, header_end);
            
            size_t content_length = 0;
            if (!parse_content_length(hdr, &content_length) ||
                content_length > 10 * 1024 * 1024 ||
                content_length > response.size() - content_start) {
                continue;
            }
            std::string message = response.substr(content_start, content_length);
            response.erase(0, content_start + content_length);
            try {
                nlohmann::json parsed = nlohmann::json::parse(message);
                if (parsed.contains("method") && !parsed.contains("id")) continue;
            } catch (const nlohmann::json::parse_error&) {
                return "";
            }
            return message;
        }
    }
    
    return "";
}

} // namespace lsp
} // namespace opencdc
