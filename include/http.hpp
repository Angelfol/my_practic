// CGI: разбор запроса (query string, тело POST, cookies) и формирование ответа.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace apteka {

std::string url_decode(const std::string& s);
std::string url_encode(const std::string& s);

// Разбирает строку вида a=1&b=2 в отображение (с URL-декодированием).
std::map<std::string, std::string> parse_urlencoded(const std::string& s);

struct Request {
    std::string method;       // GET / POST
    std::string content_type; // как прислал клиент
    std::map<std::string, std::string> get;     // параметры query string
    std::map<std::string, std::string> post;    // поля application/x-www-form-urlencoded
    std::map<std::string, std::string> cookies;
    std::string raw_body;     // сырое тело (для multipart/form-data)
    bool body_too_large = false; // тело превысило лимит и было отброшено

    // Читает окружение CGI и stdin. Переводит потоки в бинарный режим (Windows).
    static Request parse(size_t max_body_bytes);

    std::string get_param(const std::string& name) const;
    std::string post_param(const std::string& name) const;
    std::string cookie(const std::string& name) const;
};

struct Response {
    int status = 200;
    std::string content_type = "text/html; charset=utf-8";
    std::vector<std::string> extra_headers; // например, Set-Cookie
    std::string body;

    void add_cookie(const std::string& name, const std::string& value,
                    int max_age_seconds, bool http_only = true);
    void send() const; // печатает заголовки CGI и тело в stdout
};

// Ответ-перенаправление (302) на относительный или абсолютный URL.
Response redirect(const std::string& location);

} // namespace apteka
