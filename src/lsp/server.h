#ifndef OPENCDC_LSP_SERVER_H
#define OPENCDC_LSP_SERVER_H

#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_map>
#include <fstream>
#include <atomic>
#include <chrono>

namespace opencdc {
namespace lsp {

struct Position {
    int line;
    int character;
};

struct Range {
    Position start;
    Position end;
};

struct Location {
    std::string uri;
    Range range;
};

struct Diagnostic {
    Range range;
    std::string severity;
    std::string code;
    std::string source;
    std::string message;
    std::vector<Location> relatedInformation;
};

struct TextDocument {
    std::string uri;
    std::string languageId;
    int version;
    std::string text;
};

struct PublishDiagnosticsParams {
    std::string uri;
    std::vector<Diagnostic> diagnostics;
};

class LspServer {
public:
    using PublishDiagnosticsCallback = std::function<void(const PublishDiagnosticsParams&)>;
    
    LspServer();
    ~LspServer();
    
    void start(int port = 0);
    void stop();
    
    void set_publish_diagnostics_callback(PublishDiagnosticsCallback callback);
    
    void did_open(const TextDocument& document);
    void did_change(const TextDocument& document);
    void did_close(const std::string& uri);
    void did_save(const TextDocument& document);
    
    std::vector<Diagnostic> analyze_document(const std::string& uri, const std::string& content);
    
    void set_top_module(const std::string& module) { top_module_ = module; }
    void set_config_path(const std::string& path) { config_path_ = path; }
    void set_waiver_path(const std::string& path) { waiver_path_ = path; }
    void set_constraints_path(const std::string& path) { constraints_path_ = path; }
    void set_allow_remote(bool allow) { allow_remote_ = allow; }
    void set_bind_address(const std::string& addr) { bind_address_ = addr; }
    void set_analysis_timeout(int seconds) { analysis_timeout_sec_ = seconds; }

    int bound_port() const { return bound_port_.load(); }
    bool is_running() const { return running_; }
    
private:
    void server_loop();
    void handle_request(const std::string& request);
    std::string process_message(const std::string& message);
    void send_diagnostics_notification(const PublishDiagnosticsParams& params);
    
    std::string serialize_diagnostics(const PublishDiagnosticsParams& params);
    std::string serialize_response(const std::string& id, const std::string& result);
    std::string serialize_error(const std::string& id, int code, const std::string& message);
    
    std::thread server_thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<std::string> message_queue_;
    std::atomic<bool> running_{false};
    
    std::unordered_map<std::string, TextDocument> open_documents_;
    std::unordered_map<std::string, std::vector<Diagnostic>> diagnostics_cache_;
    
    PublishDiagnosticsCallback publish_callback_;
    
    std::string top_module_;
    std::string config_path_;
    std::string waiver_path_;
    std::string constraints_path_;
    
    std::string bind_address_ = "127.0.0.1";
    bool allow_remote_ = false;
    int port_ = 0;
    std::atomic<int> bound_port_{0};
    std::mutex startup_mutex_;
    std::condition_variable startup_cv_;
    int socket_fd_ = -1;
    int client_fd_ = -1;
    std::mutex socket_mutex_;

    // D3: Per-document cancellation (shared_ptr to prevent iterator invalidation)
    std::mutex cancel_mutex_;
    std::unordered_map<std::string, std::shared_ptr<std::atomic<bool>>> cancel_flags_;
    // D4: Analysis timeout (seconds), 0 = no timeout
    int analysis_timeout_sec_ = 10;
};

class LspClient {
public:
    bool connect(const std::string& host, int port);
    void disconnect();
    
    void send_notification(const std::string& method, const std::string& params);
    std::string send_request(const std::string& method, const std::string& params);
    
private:
    bool write_all(int fd, const char* data, size_t len);
    int socket_fd_ = -1;
};

} // namespace lsp
} // namespace opencdc

#endif // OPENCDC_LSP_SERVER_H
