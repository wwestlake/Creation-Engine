#include "HttpTransport.h"

#include <httplib.h>

#include <algorithm>
#include <thread>

namespace ce::developer
{
namespace
{
bool TokenMatches(std::string_view provided, std::string_view configured)
{
    if (provided.size() != configured.size()) return false;
    unsigned difference = 0;
    for (std::size_t i = 0; i < configured.size(); ++i)
        difference |= static_cast<unsigned char>(provided[i]) ^ static_cast<unsigned char>(configured[i]);
    return difference == 0;
}

bool BoundedJson(std::string_view json)
{
    // Bound recursion before handing local, untrusted input to the JSON parser.
    json.remove_prefix(std::min(json.find_first_not_of(" \t\r\n"), json.size()));
    if (json.empty() || json.front() != '{') return false;
    unsigned depth = 0;
    bool quoted = false;
    bool escaped = false;
    bool finished = false;
    for (const char c : json) {
        if (finished) {
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return false;
            continue;
        }
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') {
            quoted = true;
        } else if (c == '{' || c == '[') {
            if (++depth > 24) return false;
        } else if (c == '}' || c == ']') {
            if (depth == 0) return false;
            finished = --depth == 0;
        }
    }
    return finished && !quoted;
}
} // namespace

struct HttpTransport::Impl {
    Impl(DeveloperApi& api, const Settings& settings)
        : api(api), read(settings.readToken.toStdString()), control(settings.controlToken.toStdString())
    {
        using Result = httplib::Server::HandlerResponse;
        server.new_task_queue = [] { return new httplib::ThreadPool(4, 4, 16); };
        server.set_payload_max_length(static_cast<std::size_t>(settings.limits.requestBytes));
        server.set_read_timeout(2, 0);
        server.set_write_timeout(2, 0);
        server.set_keep_alive_max_count(1);
        server.set_default_headers({{"Cache-Control", "no-store"}, {"X-Content-Type-Options", "nosniff"}});
        server.set_pre_routing_handler([this](const httplib::Request& req, httplib::Response& res) {
            const auto host = req.get_header_value("Host");
            if (req.remote_addr != "127.0.0.1" || req.get_header_value_count("Host") != 1 ||
                (host != "127.0.0.1:" + portText && host != "localhost:" + portText) ||
                req.has_header("Origin") || req.has_header("Upgrade")) {
                WriteError(res, 403, "forbidden", "This request is outside the loopback API policy.");
                return Result::Handled;
            }
            if (!Authenticate(req)) {
                res.set_header("WWW-Authenticate", "Bearer");
                WriteError(res, 401, "unauthenticated", "A valid Developer API bearer credential is required.");
                return Result::Handled;
            }
            return Result::Unhandled;
        });
        server.Get("/v1/discovery", [this](const httplib::Request& req, httplib::Response& res) {
            Write(res, {200, this->api.Discovery(*Authenticate(req))});
        });
        server.Get("/v1/openapi", [this](const httplib::Request&, httplib::Response& res) {
            Write(res, {200, this->api.OpenApi()});
        });
        server.Post(R"(/v1/commands/([a-zA-Z0-9_.-]{1,80}))", [this](const httplib::Request& req, httplib::Response& res) {
            const auto type = req.get_header_value("Content-Type");
            if (req.get_header_value_count("Content-Type") != 1 ||
                (type != "application/json" && type != "application/json; charset=utf-8")) {
                WriteError(res, 400, "invalid_input", "Expected application/json.");
                return;
            }
            Value body;
            if (!BoundedJson(req.body) || !juce::CharPointer_UTF8::isValidString(req.body.data(), static_cast<int>(req.body.size())) ||
                req.body.find('\0') != std::string::npos || juce::JSON::parse(Utf8(req.body), body).failed()) {
                WriteError(res, 400, "invalid_input", "Invalid JSON request.");
                return;
            }
            Write(res, this->api.Submit(*Authenticate(req), req.matches[1].str(), body));
        });
        const auto jobHandler = [this](const httplib::Request& req, httplib::Response& res) {
            Write(res, this->api.Job(*Authenticate(req), req.matches[1].str(), req.method == "DELETE"));
        };
        server.Get(R"(/v1/jobs/([a-zA-Z0-9_-]{1,80}))", jobHandler);
        server.Delete(R"(/v1/jobs/([a-zA-Z0-9_-]{1,80}))", jobHandler);
        server.set_error_handler([this](const httplib::Request&, httplib::Response& res) {
            if (!res.body.empty()) return;
            if (res.status == 413) WriteError(res, 413, "resource_limit", "The request exceeds the size limit.");
            else if (res.status == 404 || res.status == 405) WriteError(res, res.status, "unsupported", "Unknown API endpoint or method.");
            else if (res.status >= 500) WriteError(res, res.status, "internal", "The request could not be completed.");
            else WriteError(res, res.status, "invalid_input", "Invalid HTTP request.");
        });
        server.set_exception_handler([this](const httplib::Request&, httplib::Response& res, std::exception_ptr) {
            WriteError(res, 500, "internal", "The request could not be completed.");
        });
    }

    std::optional<Access> Authenticate(const httplib::Request& req) const
    {
        if (req.get_header_value_count("Authorization") != 1) return {};
        const auto header = req.get_header_value("Authorization");
        constexpr std::string_view prefix = "Bearer ";
        if (!header.starts_with(prefix)) return {};
        const auto token = std::string_view(header).substr(prefix.size());
        if (TokenMatches(token, control)) return Access::control;
        if (TokenMatches(token, read)) return Access::read;
        return {};
    }

    void WriteError(httplib::Response& response, int status, const char* code, const char* message)
    {
        Write(response, {status, Object({{"error", Error({code, message, status, {}})}})});
    }

    void Write(httplib::Response& response, const Response& value)
    {
        auto json = juce::JSON::toString(value.body, true);
        response.status = value.status;
        if (json.getNumBytesAsUTF8() > static_cast<std::size_t>(api.GetLimits().responseBytes)) {
            json = R"({"error":{"code":"resource_limit","message":"The response exceeds the size limit."}})";
            response.status = 429;
        }
        response.set_content(json.toStdString(), "application/json");
    }

    DeveloperApi& api;
    std::string read, control, portText;
    httplib::Server server;
    std::thread listener;
};

HttpTransport::HttpTransport(DeveloperApi& api, const Settings& settings)
    : impl_(std::make_unique<Impl>(api, settings)) {}

HttpTransport::~HttpTransport() { Stop(); }

int HttpTransport::Start(int port)
{
    if (port == 0)
        port = impl_->server.bind_to_any_port("127.0.0.1");
    else if (!impl_->server.bind_to_port("127.0.0.1", port))
        port = -1;
    if (port <= 0) Reject("operation_failed", "The Developer API could not bind the loopback port.", 500);
    impl_->portText = std::to_string(port);
    impl_->listener = std::thread([this] { impl_->server.listen_after_bind(); });
    impl_->server.wait_until_ready();
    if (!impl_->server.is_running()) Reject("operation_failed", "The Developer API listener failed to start.", 500);
    return port;
}

void HttpTransport::Stop()
{
    if (!impl_->listener.joinable()) return;
    impl_->server.stop();
    impl_->listener.join();
}
} // namespace ce::developer
