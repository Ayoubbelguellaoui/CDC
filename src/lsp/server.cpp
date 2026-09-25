#include "lsp/server.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <sstream>

#include "analysis/analyzer.h"
#include "lsp/socket_compat.h"
#include "util/temp_file.h"

namespace opencdc {
namespace lsp {

namespace {
// Removes a temporary analysis file on scope exit, including early returns.
struct temp_file_guard {
    std::string path;
    ~temp_file_guard() {
        if (!path.empty())
            std::remove(path.c_str());
    }
};

bool is_loopback_ipv4(const std::string& ip) {
    // 127.0.0.0/8 (covers 127.0.0.1, 127.0.0.2, ...).
    if (ip.size() < 8 || ip.compare(0, 4, "127.") != 0)
        return false;
    std::istringstream ss(ip.substr(4));
    std::string part;
    while (std::getline(ss, part, '.')) {
        if (part.empty() || part.size() > 3)
            return false;
        for (char c : part)
            if (!std::isdigit((unsigned char)c))
                return false;
    }
    return true;
}
}  // namespace

static bool write_all_fd(compat::socket_t fd, const char* data, size_t len) {
    size_t written = 0;
    while (written < len) {
        compat::ssize_t n = compat::socket_send(fd, data + written, len - written, MSG_NOSIGNAL);
        if (n < 0) {
            if (compat::last_interrupted())
                continue;
            return false;
        }
        if (n == 0)
            return false;
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
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        size_t colon = line.find(':');
        if (colon == std::string::npos)
            continue;
        std::string name = line.substr(0, colon);
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (name != "content-length")
            continue;
        if (found)
            return false;
        found = true;
        size_t pos = colon + 1;
        while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos])))
            ++pos;
        if (pos == line.size())
            return false;
        size_t parsed = 0;
        for (; pos < line.size(); ++pos) {
            unsigned char c = static_cast<unsigned char>(line[pos]);
            if (std::isspace(c)) {
                while (++pos < line.size()) {
                    if (!std::isspace(static_cast<unsigned char>(line[pos])))
                        return false;
                }
                break;
            }
            if (!std::isdigit(c))
                return false;
            size_t digit = static_cast<size_t>(c - '0');
            if (parsed > (std::numeric_limits<size_t>::max() - digit) / 10)
                return false;
            parsed = parsed * 10 + digit;
        }
        value = parsed;
    }
    if (!found)
        return false;
    *length = value;
    return true;
}

LspServer::LspServer() {}

LspServer::~LspServer() {
    stop();
}

void LspServer::start(int port) {
    if (running_.load()) {
        // Already running: refuse instead of joining a thread that never exits.
        return;
    }
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
        startup_cv_.wait_for(lock, std::chrono::seconds(5),
                             [this] { return bound_port_.load() != 0 || !running_.load(); });
    }
}

void LspServer::stop() {
    running_ = false;
    cv_.notify_all();
    // Shutdown to unblock blocking reads/joins; server_loop owns FD cleanup.
    // Use socket_mutex_ to serialize with concurrent writes.
    {
        std::lock_guard<std::mutex> lock(socket_mutex_);
        if (client_fd_ != compat::kInvalidSocket) {
            compat::shutdown_socket(client_fd_);
            client_fd_ = compat::kInvalidSocket;
        }
        if (socket_fd_ != compat::kInvalidSocket) {
            compat::shutdown_socket(socket_fd_);
            socket_fd_ = compat::kInvalidSocket;
        }
    }
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
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

    PublishDiagnosticsCallback callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callback = publish_callback_;
    }
    if (callback) {
        PublishDiagnosticsParams params;
        params.uri = document.uri;
        params.diagnostics = diagnostics;
        callback(params);
    } else {
        PublishDiagnosticsParams params{document.uri, diagnostics};
        send_diagnostics_notification(params);
    }
}

void LspServer::did_change(const TextDocument& document) {
    {
        // Signal any in-flight analysis; do NOT install a fresh flag here —
        // analyze_document() owns flag creation. Installing one here would be
        // immediately overwritten there, orphaning a cancel.
        std::lock_guard<std::mutex> lock(cancel_mutex_);
        auto it = cancel_flags_.find(document.uri);
        if (it != cancel_flags_.end()) {
            it->second->store(true);
        }
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

    PublishDiagnosticsCallback callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callback = publish_callback_;
    }
    if (callback) {
        PublishDiagnosticsParams params;
        params.uri = document.uri;
        params.diagnostics = diagnostics;
        callback(params);
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
    std::string content =
        nlohmann::json{{"jsonrpc", "2.0"},
                       {"method", "textDocument/publishDiagnostics"},
                       {"params", nlohmann::json::parse(serialize_diagnostics(params))}}
            .dump();
    std::string header = "Content-Length: " + std::to_string(content.size()) + "\r\n\r\n";

    std::lock_guard<std::mutex> lock(socket_mutex_);
    if (client_fd_ == compat::kInvalidSocket)
        return;
    if (!write_all_fd(client_fd_, header.c_str(), header.size()) ||
        !write_all_fd(client_fd_, content.c_str(), content.size())) {
        // One dead peer must not stop the server for everyone: drop this
        // client and keep accepting.
        compat::close_socket(client_fd_);
        client_fd_ = compat::kInvalidSocket;
    }
}

std::vector<Diagnostic> LspServer::analyze_document(const std::string& uri,
                                                    const std::string& content) {
    std::vector<Diagnostic> diagnostics;
    auto make_diag = [](const std::string& severity, const std::string& code,
                        const std::string& msg) {
        Diagnostic d;
        d.range.start.line = 0;
        d.range.start.character = 0;
        d.range.end.line = 0;
        d.range.end.character = 0;
        d.severity = severity;
        d.code = code;
        d.source = "opencdc";
        d.message = msg;
        return d;
    };

    // Reuse an existing non-cancelled flag if present so a $/cancelRequest
    // racing did_change is not lost by overwriting. Only create when absent.
    std::shared_ptr<std::atomic<bool>> cancel_flag;
    {
        std::lock_guard<std::mutex> lock(cancel_mutex_);
        auto it = cancel_flags_.find(uri);
        if (it != cancel_flags_.end() && !it->second->load())
            cancel_flag = it->second;
        else {
            cancel_flag = std::make_shared<std::atomic<bool>>(false);
            cancel_flags_[uri] = cancel_flag;
        }
    }
    auto start_time = std::chrono::steady_clock::now();
    // Snapshot timeout once (setter has no lock; avoid data race).
    int timeout_snapshot;
    {
        std::lock_guard<std::mutex> lock(cancel_mutex_);
        timeout_snapshot = analysis_timeout_sec_;
    }

    // D4: Timeout/cancel check lambda
    auto check_timeout = [&]() -> bool {
        if (cancel_flag->load())
            return true;
        if (timeout_snapshot > 0) {
            auto elapsed = std::chrono::steady_clock::now() - start_time;
            if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() >=
                timeout_snapshot) {
                Diagnostic diag;
                diag.range.start.line = 0;
                diag.range.start.character = 0;
                diag.range.end.line = 0;
                diag.range.end.character = 0;
                diag.severity = "warning";
                diag.code = "analysis-timeout";
                diag.source = "opencdc";
                diag.message = "Analysis timed out after " + std::to_string(timeout_snapshot) + "s";
                diagnostics.push_back(diag);
                return true;
            }
        }
        return false;
    };

    std::string top_module, config_path, waiver_path, constraints_path;
    {
        // Setters may be called from another thread; snapshot under lock.
        std::lock_guard<std::mutex> lock(mutex_);
        top_module = top_module_;
        config_path = config_path_;
        waiver_path = waiver_path_;
        constraints_path = constraints_path_;
    }
    if (top_module.empty()) {
        return diagnostics;
    }

    std::string file_path;
    std::string analysis_content = content;
    bool use_content = !content.empty();
    // Resolve the effective document text: explicit content wins, otherwise
    // fall back to the open-document snapshot (didOpen/didChange).
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = open_documents_.find(uri);
        if (it != open_documents_.end() && analysis_content.empty())
            analysis_content = it->second.text;
    }
    if (analysis_content.empty()) {
        // Secure default: never read a client-supplied file:// URI from disk
        // (/etc/passwd etc.). Refuse instead of opening arbitrary paths —
        // same behavior for local and remote connections.
        diagnostics.push_back(make_diag("error", "empty-document",
                                        "No document text provided; refusing to read "
                                        "client-supplied path from disk"));
        return diagnostics;
    }
    use_content = true;

    temp_file_guard temp_guard;
    if (use_content) {
        // Check cancel/timeout before the slow analysis step.
        if (check_timeout())
            return diagnostics;
        // Private temp file (0600 equivalent on all platforms).
        std::string tmpl_str =
            util::temp_directory() + util::path_separator() + "opencdc_lsp_XXXXXX.sv";
        std::string tmp_path;
        int fd = util::create_private_temp_file(tmpl_str, 3, tmp_path);

        if (fd >= 0) {
            // Set temp_guard.path IMMEDIATELY so the file is cleaned up
            // even if fdopen later fails.
            temp_guard.path = tmp_path;

            FILE* fp = util::fdopen_temp(fd, "w");
            if (fp) {
                if (fputs(analysis_content.c_str(), fp) == EOF) {
                    fclose(fp);
                    Diagnostic diag;
                    diag.range.start.line = 0;
                    diag.range.start.character = 0;
                    diag.range.end.line = 0;
                    diag.range.end.character = 0;
                    diag.severity = "error";
                    diag.code = "temporary-file-error";
                    diag.source = "opencdc";
                    diag.message = "Failed to write temporary document";
                    diagnostics.push_back(diag);
                    return diagnostics;
                }
                fclose(fp);
                file_path = temp_guard.path;
            } else {
                util::close_temp_fd(fd);
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
    request.top_module = top_module;
    request.config_path = config_path;
    request.waiver_path = waiver_path;
    request.constraints_path = constraints_path;

    analysis::Analyzer analyzer;
    analysis::AnalysisResult result = analyzer.run(request);

    // D3+D4: Check after frontend elaboration (slow step)
    if (check_timeout())
        return diagnostics;

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
    if (check_timeout())
        return diagnostics;

    for (const auto& f : result.findings) {
        if (f.waived)
            continue;

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
    // Snapshot settings under lock: setters may race with a running server.
    // (Prefer setting all options before start().)
    std::string bind_address;
    bool allow_remote;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        bind_address = bind_address_;
        allow_remote = allow_remote_;
    }
    if (!compat::ensure_winsock()) {
        running_ = false;
        startup_cv_.notify_all();
        return;
    }
    compat::socket_t listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == compat::kInvalidSocket) {
        running_ = false;
        startup_cv_.notify_all();
        return;
    }
    {
        std::lock_guard<std::mutex> lock(socket_mutex_);
        socket_fd_ = listen_fd;
    }

    int opt = 1;
    // Winsock takes const char* here (POSIX takes int*); the cast is valid
    // on both.
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt),
               sizeof(opt));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    // Security: bind to loopback by default; explicit opt-in for remote
    if (allow_remote) {
        address.sin_addr.s_addr = INADDR_ANY;
    } else {
        address.sin_addr.s_addr = inet_addr(bind_address.c_str());
    }
    address.sin_port = htons(port_);

    if (bind(listen_fd, (struct sockaddr*)&address, sizeof(address)) != 0) {
        compat::close_socket(listen_fd);
        std::lock_guard<std::mutex> lock(socket_mutex_);
        socket_fd_ = compat::kInvalidSocket;
        running_ = false;
        { std::lock_guard<std::mutex> lock(startup_mutex_); }
        startup_cv_.notify_all();
        return;
    }

    // Discover actual port when port 0 was requested
    if (port_ == 0) {
        compat::socklen_t addr_len = sizeof(address);
        if (getsockname(listen_fd, (struct sockaddr*)&address, &addr_len) == 0) {
            port_ = ntohs(address.sin_port);
        }
    }

    if (listen(listen_fd, 16) != 0) {
        compat::close_socket(listen_fd);
        {
            std::lock_guard<std::mutex> lock(socket_mutex_);
            socket_fd_ = compat::kInvalidSocket;
        }
        running_ = false;
        startup_cv_.notify_all();
        return;
    }

    // Signal ready only after listen() succeeds — clients connecting on the
    // notified port are otherwise refused intermittently.
    bound_port_ = port_;
    { std::lock_guard<std::mutex> lock(startup_mutex_); }
    startup_cv_.notify_all();

    while (running_) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(listen_fd, &read_fds);

        struct timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int activity = select(listen_fd + 1, &read_fds, nullptr, nullptr, &timeout);

        if (activity < 0 || !running_)
            break;

        if (FD_ISSET(listen_fd, &read_fds)) {
            struct sockaddr_in client_addr;
            compat::socklen_t client_len = sizeof(client_addr);
            compat::socket_t client_fd =
                accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);

            if (client_fd != compat::kInvalidSocket) {
                // Security: reject non-loopback clients unless explicitly allowed
                // (allow_remote snapshotted under lock at loop start).
                if (!allow_remote) {
                    char client_ip[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
                    std::string ip_str(client_ip);
                    // AF_INET only yields IPv4: accept full 127.0.0.0/8.
                    // (Previous "::1" comparison was dead code on AF_INET.)
                    if (!is_loopback_ipv4(ip_str)) {
                        compat::close_socket(client_fd);
                        continue;
                    }
                }

                // Limit to single concurrent client (check-and-set under lock
                // so stop() cannot interleave a second accept).
                {
                    std::lock_guard<std::mutex> lock(socket_mutex_);
                    if (client_fd_ != compat::kInvalidSocket) {
                        compat::close_socket(client_fd);
                        continue;
                    }
                    client_fd_ = client_fd;
                }

                // Idle read timeout (30 seconds); timeouts keep the
                // connection, only real errors drop it.
                compat::set_recv_timeout(client_fd, 30);

                char buffer[4096];
                std::string message;

                while (running_) {
                    compat::ssize_t bytes_read =
                        compat::socket_recv(client_fd, buffer, sizeof(buffer) - 1);
                    if (bytes_read == 0)
                        break;  // peer closed the connection
                    if (bytes_read < 0) {
                        if (compat::last_interrupted())
                            continue;
                        if (compat::last_would_block()) {
                            // 30s read timeout elapsed with no data: idle
                            // client, not a dead one. Keep waiting while the
                            // server runs instead of dropping the connection.
                            if (!running_)
                                break;
                            continue;
                        }
                        break;  // real socket error
                    }

                    buffer[bytes_read] = '\0';
                    message += buffer;

                    if (message.size() > 10 * 1024 * 1024) {
                        std::string err_resp = serialize_error("null", -32600, "Request too large");
                        std::string err_hdr =
                            "Content-Length: " + std::to_string(err_resp.size()) + "\r\n\r\n";
                        write_all_fd(client_fd, err_hdr.c_str(), err_hdr.size());
                        write_all_fd(client_fd, err_resp.c_str(), err_resp.size());
                        break;
                    }

                    while (true) {
                        size_t header_end = message.find("\r\n\r\n");
                        if (header_end == std::string::npos)
                            break;

                        size_t content_start = header_end + 4;
                        std::string header = message.substr(0, header_end);
                        size_t content_length = 0;
                        if (!parse_content_length(header, &content_length) ||
                            content_length > 10 * 1024 * 1024) {
                            std::string err_resp =
                                serialize_error("null", -32600, "Invalid or oversized message");
                            std::string err_hdr =
                                "Content-Length: " + std::to_string(err_resp.size()) + "\r\n\r\n";
                            write_all_fd(client_fd, err_hdr.c_str(), err_hdr.size());
                            write_all_fd(client_fd, err_resp.c_str(), err_resp.size());
                            message.clear();
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
                            bool write_ok;
                            {
                                std::lock_guard<std::mutex> lock(socket_mutex_);
                                write_ok =
                                    write_all_fd(client_fd, response_header.c_str(),
                                                 response_header.size()) &&
                                    write_all_fd(client_fd, response.c_str(), response.size());
                            }
                            if (!write_ok) {
                                // Dead peer: drop this client, keep serving.
                                break;
                            }
                        }
                    }
                }

                compat::close_socket(client_fd);
                {
                    std::lock_guard<std::mutex> lock(socket_mutex_);
                    if (client_fd_ == client_fd)
                        client_fd_ = compat::kInvalidSocket;
                }
            }
        }
    }

    compat::close_socket(listen_fd);
    {
        std::lock_guard<std::mutex> lock(socket_mutex_);
        if (socket_fd_ == listen_fd)
            socket_fd_ = compat::kInvalidSocket;
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
            {"capabilities",
             {{"textDocumentSync", 1},
              {"diagnosticProvider",
               {{"interFileDependencies", false}, {"workspaceDiagnostics", false}}}}}};
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
        if (uri.empty())
            return is_notification ? "" : serialize_error(id, -32602, "Missing document URI");

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
            return is_notification ? ""
                                   : serialize_error(id, -32602, "Missing document URI or content");
        auto& change = params["contentChanges"].back();
        if (!change.is_object() || !change.contains("text"))
            return is_notification ? "" : serialize_error(id, -32602, "Missing change text");
        std::string text = change.value("text", std::string());

        TextDocument doc;
        doc.uri = uri;
        doc.text = text;
        did_change(doc);

        return is_notification ? "" : serialize_response(id, "{}");
    } else if (method == "textDocument/didClose") {
        if (!j.contains("params"))
            return is_notification ? "" : serialize_error(id, -32602, "Missing params");
        auto& params = j["params"];
        if (!params.contains("textDocument") || !params["textDocument"].is_object())
            return is_notification ? "" : serialize_error(id, -32602, "Missing textDocument");
        std::string uri = params["textDocument"].value("uri", std::string());
        if (uri.empty())
            return is_notification ? "" : serialize_error(id, -32602, "Missing document URI");
        did_close(uri);

        return is_notification ? "" : serialize_response(id, "{}");
    } else if (method == "textDocument/didSave") {
        if (!j.contains("params"))
            return is_notification ? "" : serialize_error(id, -32602, "Missing params");
        auto& params = j["params"];
        if (!params.contains("textDocument") || !params["textDocument"].is_object())
            return is_notification ? "" : serialize_error(id, -32602, "Missing textDocument");
        auto& document = params["textDocument"];
        std::string uri = document.value("uri", std::string());
        if (uri.empty())
            return is_notification ? "" : serialize_error(id, -32602, "Missing document URI");
        TextDocument doc;
        doc.uri = uri;
        // LSP didSave: text is in params.text (string) or params.textDocument.text.
        if (params.contains("text") && params["text"].is_string())
            doc.text = params["text"].get<std::string>();
        else if (document.contains("text") && document["text"].is_string())
            doc.text = document["text"].get<std::string>();
        if (doc.text.empty()) {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = open_documents_.find(uri);
            if (it != open_documents_.end())
                doc.text = it->second.text;
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

    if (is_notification)
        return "";
    return serialize_error(id, -32601, "Method not found");
}

std::string LspServer::serialize_response(const std::string& id, const std::string& result) {
    nlohmann::json resp;
    resp["jsonrpc"] = "2.0";
    resp["id"] = nlohmann::json::parse(id);
    resp["result"] = nlohmann::json::parse(result);
    return resp.dump();
}

std::string LspServer::serialize_error(const std::string& id, int code,
                                       const std::string& message) {
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
        if (diag.severity == "error")
            severity_num = 1;
        else if (diag.severity == "warning")
            severity_num = 2;
        else if (diag.severity == "info")
            severity_num = 3;

        d["severity"] = severity_num;
        d["code"] = diag.code;
        d["source"] = diag.source;
        d["message"] = diag.message;
        diags.push_back(d);
    }

    result["diagnostics"] = diags;
    return result.dump();
}

bool LspClient::write_all(compat::socket_t fd, const char* data, size_t len) {
    size_t written = 0;
    while (written < len) {
        // MSG_NOSIGNAL: a closed peer must surface as an error, never as a
        // SIGPIPE death of the caller.
        compat::ssize_t n = compat::socket_send(fd, data + written, len - written, MSG_NOSIGNAL);
        if (n < 0) {
            if (compat::last_interrupted())
                continue;
            return false;
        }
        if (n == 0)
            return false;
        written += static_cast<size_t>(n);
    }
    return true;
}

LspClient::~LspClient() {
    disconnect();
}

bool LspClient::connect(const std::string& host, int port) {
    disconnect();

    if (!compat::ensure_winsock())
        return false;
    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ == compat::kInvalidSocket)
        return false;

    struct sockaddr_in server_addr {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) != 1) {
        compat::close_socket(socket_fd_);
        socket_fd_ = compat::kInvalidSocket;
        return false;
    }

    // Connection + read/write timeouts (5s connect, 30s I/O).
    compat::set_send_timeout(socket_fd_, 5);

    if (::connect(socket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) != 0) {
        compat::close_socket(socket_fd_);
        socket_fd_ = compat::kInvalidSocket;
        return false;
    }

    compat::set_recv_timeout(socket_fd_, 30);
    compat::set_send_timeout(socket_fd_, 30);

    return true;
}

void LspClient::disconnect() {
    if (socket_fd_ != compat::kInvalidSocket) {
        compat::close_socket(socket_fd_);
        socket_fd_ = compat::kInvalidSocket;
    }
}

void LspClient::send_notification(const std::string& method, const std::string& params) {
    if (socket_fd_ == compat::kInvalidSocket)
        return;

    nlohmann::json content;
    content["jsonrpc"] = "2.0";
    content["method"] = method;
    try {
        content["params"] = nlohmann::json::parse(params);
    } catch (const nlohmann::json::parse_error&) {
        content["params"] = nlohmann::json::object();
    }
    std::string content_str = content.dump();
    std::string header = "Content-Length: " + std::to_string(content_str.size()) + "\r\n\r\n";

    write_all(socket_fd_, header.c_str(), header.size());
    write_all(socket_fd_, content_str.c_str(), content_str.size());
}

std::string LspClient::send_request(const std::string& method, const std::string& params) {
    if (socket_fd_ == compat::kInvalidSocket)
        return "";

    static std::atomic<int> request_id{0};
    int id_val = request_id.fetch_add(1) + 1;

    nlohmann::json content;
    content["jsonrpc"] = "2.0";
    content["id"] = id_val;
    content["method"] = method;
    try {
        content["params"] = nlohmann::json::parse(params);
    } catch (const nlohmann::json::parse_error&) {
        content["params"] = nlohmann::json::object();
    }
    std::string content_str = content.dump();
    std::string header = "Content-Length: " + std::to_string(content_str.size()) + "\r\n\r\n";

    write_all(socket_fd_, header.c_str(), header.size());
    write_all(socket_fd_, content_str.c_str(), content_str.size());

    char buffer[4096];
    std::string response;

    while (true) {
        // Drain already-buffered data first: a notification and its response
        // often arrive coalesced in one TCP segment. Re-scan the buffer
        // after skipping a notification instead of blocking in read() while
        // a complete message sits unprocessed.
        while (true) {
            size_t header_end = response.find("\r\n\r\n");
            if (header_end == std::string::npos)
                break;
            size_t content_start = header_end + 4;
            std::string hdr = response.substr(0, header_end);

            size_t content_length = 0;
            if (!parse_content_length(hdr, &content_length) || content_length > 10 * 1024 * 1024) {
                return "";
            }
            if (content_length > response.size() - content_start)
                break;  // incomplete body: read more
            std::string message = response.substr(content_start, content_length);
            response.erase(0, content_start + content_length);
            try {
                nlohmann::json parsed = nlohmann::json::parse(message);
                if (parsed.contains("method") && !parsed.contains("id"))
                    continue;  // notification: re-scan buffer for the response
            } catch (const nlohmann::json::parse_error&) {
                return "";
            }
            return message;
        }

        compat::ssize_t bytes_read = compat::socket_read(socket_fd_, buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0)
            break;

        buffer[bytes_read] = '\0';
        response += buffer;
    }

    return "";
}

}  // namespace lsp
}  // namespace opencdc
