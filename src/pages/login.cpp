// Модуль «Авторизация».
#include <cstdlib>
#include <map>
#include <string>

#include "config.hpp"
#include "db.hpp"
#include "html.hpp"
#include "http.hpp"
#include "session.hpp"
#include "sha256.hpp"
#include "validate.hpp"

using namespace apteka;

namespace {

void render_form(const Request& req,
                 const std::map<std::string, std::string>& errors,
                 const std::string& top_message) {
    Response resp;
    std::string& b = resp.body;
    b += page_begin("Вход в систему", nullptr);
    if (!top_message.empty()) {
        b += "<div class=\"flash flash-ok\">" + esc(top_message) + "</div>\n";
    }
    b += field_error(errors, "form"); // общая ошибка «неверные данные»
    b += "<form class=\"form\" method=\"post\" action=\"login.cgi\">\n";
    b += "<div class=\"form-row\"><label for=\"email\">E-mail</label>"
         "<input id=\"email\" name=\"email\" type=\"text\" value=\"" +
         esc(req.post_param("email")) + "\">" + field_error(errors, "email") + "</div>\n";
    b += "<div class=\"form-row\"><label for=\"password\">Пароль</label>"
         "<input id=\"password\" name=\"password\" type=\"password\" value=\"\">" +
         field_error(errors, "password") + "</div>\n";
    b += "<div class=\"form-actions\">"
         "<button type=\"submit\" class=\"btn btn-primary\">Войти</button>"
         "<a class=\"btn\" href=\"register.cgi\">Регистрация</a></div>\n";
    b += "</form>\n";
    b += page_end();
    resp.send();
}

} // namespace

int main() {
    try {
        Config cfg = load_config();
        Request req = Request::parse(64 * 1024);
        Db db(cfg);

        // Уже вошедшего пользователя сразу отправляем на главную.
        if (current_user(db, req)) {
            redirect("index.cgi").send();
            return 0;
        }

        if (req.method != "POST") {
            std::string msg;
            if (req.get_param("registered") == "1") {
                msg = "Регистрация завершена. Войдите, используя свой e-mail и пароль.";
            }
            render_form(req, {}, msg);
            return 0;
        }

        std::map<std::string, std::string> errors;
        std::string email = trim(req.post_param("email"));
        std::string password = req.post_param("password");

        if (email.empty()) {
            errors["email"] = "Заполните поле «E-mail»";
        } else if (!valid_email(email)) {
            errors["email"] = "Некорректный адрес электронной почты";
        }
        if (password.empty()) errors["password"] = "Заполните поле «Пароль»";

        if (errors.empty()) {
            auto rows = db.query(
                "SELECT user_id, password_hash, salt FROM users WHERE email = ?",
                {email});
            if (rows.empty() ||
                sha256_hex(rows[0]["salt"] + password) != rows[0]["password_hash"]) {
                errors["form"] = "Неверный e-mail или пароль";
            } else {
                Response resp = redirect("index.cgi");
                unsigned long long uid =
                    std::strtoull(rows[0]["user_id"].c_str(), nullptr, 10);
                start_session(db, uid, cfg.session_minutes, resp);
                resp.send();
                return 0;
            }
        }
        render_form(req, errors, "");
    } catch (const std::exception& e) {
        send_error_page(e.what());
    }
    return 0;
}
