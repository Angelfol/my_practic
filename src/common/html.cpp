#include "html.hpp"

#include "http.hpp"

namespace apteka {

std::string esc(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c;
        }
    }
    return out;
}

std::string page_begin(const std::string& title, const AuthUser* user) {
    std::string h;
    h += "<!DOCTYPE html>\n<html lang=\"ru\">\n<head>\n";
    h += "<meta charset=\"utf-8\">\n";
    h += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
    h += "<title>" + esc(title) + " — Аптека «Целебная»</title>\n";
    h += "<link rel=\"stylesheet\" href=\"public/style.css\">\n";
    h += "</head>\n<body>\n";
    h += "<header class=\"site-header\"><div class=\"wrap header-inner\">\n";
    h += "<a class=\"brand\" href=\"index.cgi\">Аптека «Целебная»</a>\n";
    h += "<nav class=\"nav\">\n";
    if (user) {
        h += "<a href=\"index.cgi\">Главная</a>\n";
        h += "<a href=\"medicines.cgi\">Лекарства</a>\n";
        h += "<a href=\"orders.cgi\">Заказы</a>\n";
        h += "<a href=\"search.cgi\">Поиск</a>\n";
        h += "<a href=\"upload.cgi\">Изображения</a>\n";
        h += "<span class=\"nav-user\">" + esc(user->first_name + " " + user->last_name) + "</span>\n";
        h += "<a class=\"nav-exit\" href=\"logout.cgi\">Выйти</a>\n";
    } else {
        h += "<a href=\"login.cgi\">Вход</a>\n";
        h += "<a href=\"register.cgi\">Регистрация</a>\n";
    }
    h += "</nav></div></header>\n<main class=\"wrap\">\n";
    h += "<h1>" + esc(title) + "</h1>\n";
    return h;
}

std::string page_end() {
    return "</main>\n<footer class=\"site-footer\"><div class=\"wrap\">"
           "Информационная система аптеки · учебно-технологическая практика, 2026"
           "</div></footer>\n</body>\n</html>\n";
}

std::string field_error(const std::map<std::string, std::string>& errors,
                        const std::string& field) {
    auto it = errors.find(field);
    if (it == errors.end()) return "";
    return "<div class=\"field-error\">" + esc(it->second) + "</div>";
}

void send_error_page(const std::string& message) {
    Response resp;
    resp.status = 500;
    resp.body += "<!DOCTYPE html>\n<html lang=\"ru\"><head><meta charset=\"utf-8\">"
                 "<title>Ошибка сервера</title>"
                 "<link rel=\"stylesheet\" href=\"public/style.css\"></head><body>"
                 "<main class=\"wrap\"><h1>Ошибка сервера</h1><p class=\"form-error\">" +
                 esc(message) +
                 "</p><p><a href=\"index.cgi\">На главную</a></p></main></body></html>\n";
    resp.send();
}

} // namespace apteka
