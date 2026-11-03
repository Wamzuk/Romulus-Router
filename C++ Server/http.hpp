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

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

namespace http {

#ifdef _WIN32
using Socket = SOCKET;
#else
using Socket = int;
#endif

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

    ~Server() {
#ifdef _WIN32
        if (winsockInitialized_) WSACleanup();
#endif
    }

    void route(const std::string& method, const std::string& path, Handler h) {
        handlers_.push_back({method, path, std::move(h)});
    }

    // Fallback used when no route matches (e.g. to serve a static file).
    void setDefault(Handler h) { defaultHandler_ = std::move(h); }

    bool listen(const std::string& host, int port) {
#ifdef _WIN32
        WSADATA wsaData{};
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "WSAStartup failed\n";
            return false;
        }
        winsockInitialized_ = true;
#endif
        serverFd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (socketFailed(serverFd_)) {
            reportSocketError("socket");
            cleanupSockets();
            return false;
        }

#ifdef _WIN32
        char opt = 1;
#else
        int opt = 1;
#endif
        setsockopt(serverFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(port));
        if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
            addr.sin_addr.s_addr = INADDR_ANY;
        }

        if (bind(serverFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            reportSocketError("bind");
            cleanupSockets();
            return false;
        }
        if (::listen(serverFd_, 64) < 0) {
            reportSocketError("listen");
            cleanupSockets();
            return false;
        }
        return true;
    }

    // Accept loop. Blocks forever.
    void run() {
        while (true) {
            sockaddr_in client{};
#ifdef _WIN32
            int len = sizeof(client);
#else
            socklen_t len = sizeof(client);
#endif
            Socket fd = accept(serverFd_, reinterpret_cast<sockaddr*>(&client), &len);
            if (socketFailed(fd)) continue;
            std::thread(&Server::handleConnection, this, fd).detach();
        }
    }

private:
    struct RouteEntry { std::string method; std::string path; Handler handler; };
    std::vector<RouteEntry> handlers_;
    Handler defaultHandler_;
#ifdef _WIN32
    SOCKET serverFd_ = INVALID_SOCKET;
    bool winsockInitialized_ = false;
#else
    int serverFd_ = -1;
#endif

#ifdef _WIN32
    static bool socketFailed(Socket fd) { return fd == INVALID_SOCKET; }
    static void reportSocketError(const char* operation) {
        std::cerr << operation << " failed: " << WSAGetLastError() << "\n";
    }
    static void closeSocket(Socket fd) { closesocket(fd); }
#else
    static bool socketFailed(Socket fd) { return fd < 0; }
    static void reportSocketError(const char* operation) { perror(operation); }
    static void closeSocket(Socket fd) { close(fd); }
#endif

    void cleanupSockets() {
        if (!socketFailed(serverFd_)) {
            closeSocket(serverFd_);
#ifdef _WIN32
            serverFd_ = INVALID_SOCKET;
#else
            serverFd_ = -1;
#endif
        }
#ifdef _WIN32
        if (winsockInitialized_) {
            WSACleanup();
            winsockInitialized_ = false;
        }
#endif
    }

    static std::string toLower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        return s;
    }

    // Read exactly `n` more bytes into `buf` beyond what is already there.
    static bool readAtLeast(
        Socket fd,
        std::string& buf, size_t target) {
        char tmp[65536];
        while (buf.size() < target) {
#ifdef _WIN32
            int r = recv(fd, tmp, sizeof(tmp), 0);
#else
            ssize_t r = recv(fd, tmp, sizeof(tmp), 0);
#endif
            if (r <= 0) return false;
            buf.append(tmp, r);
        }
        return true;
    }

    void handleConnection(Socket fd) {
        std::string buf;
        char tmp[65536];

        // Read until we have the full header block.
        size_t headerEnd = std::string::npos;
        while (true) {
            headerEnd = buf.find("\r\n\r\n");
            if (headerEnd != std::string::npos) break;
#ifdef _WIN32
            int r = recv(fd, tmp, sizeof(tmp), 0);
#else
            ssize_t r = recv(fd, tmp, sizeof(tmp), 0);
#endif
            if (r <= 0) { closeSocket(fd); return; }
            buf.append(tmp, static_cast<size_t>(r));
            if (buf.size() > 512 * 1024 && headerEnd == std::string::npos) {
                sendResponse(fd, Response::make(413, "text/plain", "Headers too large"));
                closeSocket(fd);
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
            closeSocket(fd);
            return;
        }

        if (!readAtLeast(fd, buf, bodyStart + contentLength)) { closeSocket(fd); return; }
        req.body = buf.substr(bodyStart, contentLength);

        Response res = dispatch(req);
        sendResponse(fd, res);
        closeSocket(fd);
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

    void sendResponse(
        Socket fd,
        const Response& res) {
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

    static void sendAll(
        Socket fd,
        const std::string& data) {
        size_t sent = 0;
        while (sent < data.size()) {
#ifdef _WIN32
            int w = send(fd, data.data() + sent, static_cast<int>(data.size() - sent), 0);
#else
            ssize_t w = send(fd, data.data() + sent, data.size() - sent, 0);
#endif
            if (w <= 0) return;
            sent += w;
        }
    }
};

} // namespace http
