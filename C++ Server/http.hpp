// http.hpp - a minimal, blocking, thread-per-connection HTTP/1.1 server built
// on POSIX sockets. It is deliberately tiny: just enough to serve one static
// file and a handful of JSON POST endpoints on localhost.
#pragma once

#include <string>
#include <map>
#include <functional>
#include <thread>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <iostream>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

namespace http {

struct Request {
    std::string method;
    std::string path;
    std::map<std::string, std::string> headers; // lower-cased keys
    std::string body;
};

struct Response {
    int status = 200;
    std::string contentType = "application/json";
    std::string body;
    std::map<std::string, std::string> extraHeaders;

    // Convenience factory so simple responses don't need aggregate init.
    static Response make(int status, const std::string& contentType, const std::string& body) {
        Response r;
        r.status = status;
        r.contentType = contentType;
        r.body = body;
        return r;
    }
};

inline std::string statusText(int code) {
    switch (code) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 500: return "Internal Server Error";
        default: return "OK";
    }
}

class Server {
public:
    using Handler = std::function<Response(const Request&)>;

    void route(const std::string& method, const std::string& path, Handler h) {
        handlers_.push_back({method, path, std::move(h)});
    }

    // Fallback used when no route matches (e.g. to serve a static file).
    void setDefault(Handler h) { defaultHandler_ = std::move(h); }

    bool listen(const std::string& host, int port) {
        serverFd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (serverFd_ < 0) { perror("socket"); return false; }

        int opt = 1;
        setsockopt(serverFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(port));
        if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
            addr.sin_addr.s_addr = INADDR_ANY;
        }

        if (bind(serverFd_, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return false; }
        if (::listen(serverFd_, 64) < 0) { perror("listen"); return false; }
        return true;
    }

    // Accept loop. Blocks forever.
    void run() {
        while (true) {
            sockaddr_in client{};
            socklen_t len = sizeof(client);
            int fd = accept(serverFd_, (sockaddr*)&client, &len);
            if (fd < 0) continue;
            std::thread(&Server::handleConnection, this, fd).detach();
        }
    }

private:
    struct RouteEntry { std::string method; std::string path; Handler handler; };
    std::vector<RouteEntry> handlers_;
    Handler defaultHandler_;
    int serverFd_ = -1;

    static std::string toLower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        return s;
    }

    // Read exactly `n` more bytes into `buf` beyond what is already there.
    static bool readAtLeast(int fd, std::string& buf, size_t target) {
        char tmp[65536];
        while (buf.size() < target) {
            ssize_t r = recv(fd, tmp, sizeof(tmp), 0);
            if (r <= 0) return false;
            buf.append(tmp, r);
        }
        return true;
    }

    void handleConnection(int fd) {
        std::string buf;
        char tmp[65536];

        // Read until we have the full header block.
        size_t headerEnd = std::string::npos;
        while (true) {
            headerEnd = buf.find("\r\n\r\n");
            if (headerEnd != std::string::npos) break;
            ssize_t r = recv(fd, tmp, sizeof(tmp), 0);
            if (r <= 0) { close(fd); return; }
            buf.append(tmp, r);
            if (buf.size() > 512 * 1024 && headerEnd == std::string::npos) {
                sendResponse(fd, Response::make(413, "text/plain", "Headers too large"));
                close(fd);
                return;
            }
        }

        Request req;
        std::string headerBlock = buf.substr(0, headerEnd);
        parseHeaders(headerBlock, req);

        // Read the body if Content-Length is present.
        size_t bodyStart = headerEnd + 4;
        size_t contentLength = 0;
        auto it = req.headers.find("content-length");
        if (it != req.headers.end()) contentLength = std::strtoul(it->second.c_str(), nullptr, 10);

        if (contentLength > 64 * 1024 * 1024) { // 64 MB cap
            sendResponse(fd, Response::make(413, "text/plain", "Payload too large"));
            close(fd);
            return;
        }

        if (!readAtLeast(fd, buf, bodyStart + contentLength)) { close(fd); return; }
        req.body = buf.substr(bodyStart, contentLength);

        Response res = dispatch(req);
        sendResponse(fd, res);
        close(fd);
    }

    void parseHeaders(const std::string& block, Request& req) {
        std::istringstream iss(block);
        std::string line;
        if (!std::getline(iss, line)) return;
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // Request line: METHOD PATH VERSION
        std::istringstream rl(line);
        std::string version;
        rl >> req.method >> req.path >> version;

        // Strip any query string for routing purposes.
        auto q = req.path.find('?');
        if (q != std::string::npos) req.path = req.path.substr(0, q);

        while (std::getline(iss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            auto colon = line.find(':');
            if (colon == std::string::npos) continue;
            std::string key = toLower(line.substr(0, colon));
            std::string val = line.substr(colon + 1);
            size_t s = val.find_first_not_of(" \t");
            if (s != std::string::npos) val = val.substr(s);
            req.headers[key] = val;
        }
    }

    Response dispatch(const Request& req) {
        for (auto& h : handlers_) {
            if (h.path == req.path) {
                if (h.method == req.method) {
                    try {
                        return h.handler(req);
                    } catch (const std::exception& e) {
                        Response r;
                        r.status = 500;
                        r.body = std::string("{\"error\":\"") + jsonEscape(e.what()) + "\"}";
                        return r;
                    }
                }
            }
        }
        // Path exists but wrong method?
        for (auto& h : handlers_) {
            if (h.path == req.path) return Response::make(405, "text/plain", "Method Not Allowed");
        }
        if (defaultHandler_) return defaultHandler_(req);
        return Response::make(404, "text/plain", "Not Found");
    }

    static std::string jsonEscape(const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '"' || c == '\\') { out += '\\'; out += c; }
            else if (c == '\n') out += "\\n";
            else out += c;
        }
        return out;
    }

    void sendResponse(int fd, const Response& res) {
        std::ostringstream out;
        out << "HTTP/1.1 " << res.status << " " << statusText(res.status) << "\r\n";
        out << "Content-Type: " << res.contentType << "\r\n";
        out << "Content-Length: " << res.body.size() << "\r\n";
        out << "Access-Control-Allow-Origin: *\r\n";
        out << "Connection: close\r\n";
        for (auto& kv : res.extraHeaders) out << kv.first << ": " << kv.second << "\r\n";
        out << "\r\n";
        std::string header = out.str();
        sendAll(fd, header);
        sendAll(fd, res.body);
    }

    static void sendAll(int fd, const std::string& data) {
        size_t sent = 0;
        while (sent < data.size()) {
            ssize_t w = send(fd, data.data() + sent, data.size() - sent, 0);
            if (w <= 0) return;
            sent += w;
        }
    }
};

} // namespace http
