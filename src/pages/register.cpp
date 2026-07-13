// Модуль «Регистрация».
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

// Поле формы с подписью, значением и текстом ошибки.
std::string input_row(const std::string& name, const std::string& label,
                      const std::string& type, const std::string& value,
                      const std::map<std::string, std::string>& errors) {
    std::string h = "<div class=\"form-row\">";
    h += "<label for=\"" + name + "\">" + esc(label) + "</label>";
    h += "<input id=\"" + name + "\" name=\"" + name + "\" type=\"" + type +
         "\" value=\"" + esc(value) + "\">";
    h += field_error(errors, name);
    h += "</div>\n";
    return h;
}

void render_form(const Request& req,
                 const std::map<std::string, std::string>& errors,
                 const AuthUser* user) {
    Response resp;
    std::string& b = resp.body;
    b += page_begin("Регистрация", user);
    b += "<form class=\"form\" method=\"post\" action=\"register.cgi\">\n";
    b += input_row("last_name", "Фамилия", "text", req.post_param("last_name"), errors);
    b += input_row("first_name", "Имя", "text", req.post_param("first_name"), errors);
    b += input_row("patronymic", "Отчество", "text", req.post_param("patronymic"), errors);
    b += input_row("birth_date", "Дата рождения", "date", req.post_param("birth_date"), errors);
    b += input_row("email", "E-mail", "text", req.post_param("email"), errors);
    // Значение пароля сохраняется по требованию задания: введённые значения не теряются.
    b += input_row("password", "Пароль (не менее 3 символов)", "password",
                   req.post_param("password"), errors);
    b += "<div class=\"form-actions\">"
         "<button type=\"submit\" class=\"btn btn-primary\">Зарегистрироваться</button>"
         "<a class=\"btn\" href=\"login.cgi\">Уже есть аккаунт? Войти</a></div>\n";
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
        auto user = current_user(db, req);
        const AuthUser* user_ptr = user ? &*user : nullptr;

        if (req.method != "POST") {
            render_form(req, {}, user_ptr);
            return 0;
        }

        std::map<std::string, std::string> errors;
        std::string last_name = trim(req.post_param("last_name"));
        std::string first_name = trim(req.post_param("first_name"));
        std::string patronymic = trim(req.post_param("patronymic"));
        std::string birth_date = trim(req.post_param("birth_date"));
        std::string email = trim(req.post_param("email"));
        std::string password = req.post_param("password");

        if (last_name.empty()) errors["last_name"] = "Заполните поле «Фамилия»";
        if (first_name.empty()) errors["first_name"] = "Заполните поле «Имя»";
        if (patronymic.empty()) errors["patronymic"] = "Заполните поле «Отчество»";

        if (birth_date.empty()) {
            errors["birth_date"] = "Заполните поле «Дата рождения»";
        } else if (!valid_date(birth_date)) {
            errors["birth_date"] = "Укажите дату в формате ГГГГ-ММ-ДД";
        }

        if (email.empty()) {
            errors["email"] = "Заполните поле «E-mail»";
        } else if (!valid_email(email)) {
            errors["email"] = "Некорректный адрес электронной почты";
        } else if (!db.query("SELECT user_id FROM users WHERE email = ?", {email}).empty()) {
            errors["email"] = "Пользователь с таким e-mail уже зарегистрирован";
        }

        if (password.empty()) {
            errors["password"] = "Заполните поле «Пароль»";
        } else if (password.size() < 3) {
            errors["password"] = "Пароль должен содержать не менее 3 символов";
        }

        if (!errors.empty()) {
            render_form(req, errors, user_ptr);
            return 0;
        }

        std::string salt = random_hex(16);
        std::string hash = sha256_hex(salt + password);
        db.exec(
            "INSERT INTO users (first_name, last_name, patronymic, email, "
            "password_hash, salt, birth_date) VALUES (?, ?, ?, ?, ?, ?, ?)",
            {first_name, last_name, patronymic, email, hash, salt, birth_date});

        // Успешная регистрация — переход на экран входа.
        redirect("login.cgi?registered=1").send();
    } catch (const std::exception& e) {
        send_error_page(e.what());
    }
    return 0;
}
