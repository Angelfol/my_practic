#include "http.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iostream>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "validate.hpp" // trim

namespace apteka {

namespace {

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? v : "";
}

int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void binary_stdio() {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
}

} // namespace

std::string url_decode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+') {
            out += ' ';
        } else if (s[i] == '%' && i + 2 < s.size()) {
            int hi = hex_val(s[i + 1]), lo = hex_val(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>(hi * 16 + lo);
                i += 2;
            } else {
                out += s[i];
            }
        } else {
            out += s[i];
        }
    }
    return out;
}

std::string url_encode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0x0f];
        }
    }
    return out;
}

std::map<std::string, std::string> parse_urlencoded(const std::string& s) {
    std::map<std::string, std::string> out;
    size_t pos = 0;
    while (pos <= s.size()) {
        size_t amp = s.find('&', pos);
        std::string pair = s.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
        if (!pair.empty()) {
            auto eq = pair.find('=');
            if (eq == std::string::npos) {
                out[url_decode(pair)] = "";
            } else {
                out[url_decode(pair.substr(0, eq))] = url_decode(pair.substr(eq + 1));
            }
        }
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return out;
}

Request Request::parse(size_t max_body_bytes) {
    binary_stdio();

    Request req;
    req.method = env("REQUEST_METHOD");
    if (req.method.empty()) req.method = "GET";
    req.content_type = env("CONTENT_TYPE");
    req.get = parse_urlencoded(env("QUERY_STRING"));

    // Cookies: "a=1; b=2"
    std::string cookie_hdr = env("HTTP_COOKIE");
    size_t pos = 0;
    while (pos < cookie_hdr.size()) {
        size_t semi = cookie_hdr.find(';', pos);
        std::string item = cookie_hdr.substr(
            pos, semi == std::string::npos ? std::string::npos : semi - pos);
        item = trim(item);
        auto eq = item.find('=');
        if (eq != std::string::npos && eq > 0) {
            req.cookies[item.substr(0, eq)] = item.substr(eq + 1);
        }
        if (semi == std::string::npos) break;
        pos = semi + 1;
    }

    if (req.method == "POST") {
        unsigned long long len = std::strtoull(env("CONTENT_LENGTH").c_str(), nullptr, 10);
        if (len > max_body_bytes) {
            // Тело не читаем целиком: помечаем и аккуратно поглощаем ввод.
            char sink[8192];
            unsigned long long left = len;
            while (left > 0 && std::cin.read(sink, static_cast<std::streamsize>(
                                                       left > sizeof(sink) ? sizeof(sink) : left))) {
                left -= static_cast<unsigned long long>(std::cin.gcount());
            }
            req.body_too_large = true;
        } else if (len > 0) {
            req.raw_body.resize(static_cast<size_t>(len));
            std::cin.read(&req.raw_body[0], static_cast<std::streamsize>(len));
            req.raw_body.resize(static_cast<size_t>(std::cin.gcount()));
            if (req.content_type.find("application/x-www-form-urlencoded") != std::string::npos) {
                req.post = parse_urlencoded(req.raw_body);
            }
        }
    }
    return req;
}

std::string Request::get_param(const std::string& name) const {
    auto it = get.find(name);
    return it == get.end() ? "" : it->second;
}

std::string Request::post_param(const std::string& name) const {
    auto it = post.find(name);
    return it == post.end() ? "" : it->second;
}

std::string Request::cookie(const std::string& name) const {
    auto it = cookies.find(name);
    return it == cookies.end() ? "" : it->second;
}

void Response::add_cookie(const std::string& name, const std::string& value,
                          int max_age_seconds, bool http_only) {
    std::string h = "Set-Cookie: " + name + "=" + value + "; Path=/; Max-Age=" +
                    std::to_string(max_age_seconds);
    if (http_only) h += "; HttpOnly";
    extra_headers.push_back(h);
}

void Response::send() const {
    binary_stdio();
    std::string head;
    head += "Status: " + std::to_string(status) + "\r\n";
    head += "Content-Type: " + content_type + "\r\n";
    for (const auto& h : extra_headers) head += h + "\r\n";
    head += "\r\n";
    std::cout << head << body;
    std::cout.flush();
}

Response redirect(const std::string& location) {
    Response r;
    r.status = 302;
    r.extra_headers.push_back("Location: " + location);
    r.body = "";
    return r;
}

} // namespace apteka
