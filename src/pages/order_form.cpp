// Страницы «Создание записи» и «Редактирование записи» (заказ).
#include <map>
#include <string>
#include <vector>

#include "config.hpp"
#include "db.hpp"
#include "html.hpp"
#include "http.hpp"
#include "session.hpp"
#include "validate.hpp"

using namespace apteka;

namespace {

struct FormData {
    std::string id;          // пусто — создание новой записи
    std::string order_number;
    std::string medicine_id;
    std::string order_date;
    std::string quantity;
};

void render_form(const AuthUser& user, Db& db, const FormData& f,
                 const std::map<std::string, std::string>& errors) {
    auto meds = db.query(
        "SELECT medicine_id, medicine_name FROM medicines ORDER BY medicine_name");

    Response resp;
    std::string& b = resp.body;
    bool editing = !f.id.empty();
    b += page_begin(editing ? "Редактирование записи" : "Создание записи", &user);

    b += "<form class=\"form\" method=\"post\" action=\"order_form.cgi\">\n";
    if (editing) {
        b += "<input type=\"hidden\" name=\"id\" value=\"" + esc(f.id) + "\">\n";
    }

    b += "<div class=\"form-row\"><label for=\"order_number\">Идентификатор (номер заказа)</label>"
         "<input id=\"order_number\" name=\"order_number\" type=\"text\" value=\"" +
         esc(f.order_number) + "\">" + field_error(errors, "order_number") + "</div>\n";

    b += "<div class=\"form-row\"><label for=\"medicine_id\">Наименование лекарства</label>"
         "<select id=\"medicine_id\" name=\"medicine_id\">"
         "<option value=\"\">— выберите лекарство —</option>";
    for (auto& m : meds) {
        b += "<option value=\"" + esc(m["medicine_id"]) + "\"";
        if (m["medicine_id"] == f.medicine_id) b += " selected";
        b += ">" + esc(m["medicine_name"]) + "</option>";
    }
    b += "</select>" + field_error(errors, "medicine_id") + "</div>\n";

    b += "<div class=\"form-row\"><label for=\"order_date\">Дата</label>"
         "<input id=\"order_date\" name=\"order_date\" type=\"date\" value=\"" +
         esc(f.order_date) + "\">" + field_error(errors, "order_date") + "</div>\n";

    b += "<div class=\"form-row\"><label for=\"quantity\">Количество</label>"
         "<input id=\"quantity\" name=\"quantity\" type=\"text\" value=\"" +
         esc(f.quantity) + "\">" + field_error(errors, "quantity") + "</div>\n";

    b += "<div class=\"form-actions\">"
         "<button type=\"submit\" class=\"btn btn-primary\">Сохранение</button>"
         "<a class=\"btn\" href=\"orders.cgi\">К списку</a></div>\n";
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
        AuthUser user = require_login(db, req);

        if (req.method != "POST") {
            FormData f;
            f.id = trim(req.get_param("id"));
            if (!f.id.empty()) {
                if (!valid_positive_int(f.id)) {
                    redirect("orders.cgi").send();
                    return 0;
                }
                auto rows = db.query(
                    "SELECT order_number, medicine_id, "
                    "       DATE_FORMAT(created_at, '%Y-%m-%d') AS order_date, quantity "
                    "FROM orders WHERE order_id = ?",
                    {f.id});
                if (rows.empty()) {
                    redirect("orders.cgi").send();
                    return 0;
                }
                f.order_number = rows[0]["order_number"];
                f.medicine_id = rows[0]["medicine_id"];
                f.order_date = rows[0]["order_date"];
                f.quantity = rows[0]["quantity"];
            }
            render_form(user, db, f, {});
            return 0;
        }

        FormData f;
        f.id = trim(req.post_param("id"));
        f.order_number = trim(req.post_param("order_number"));
        f.medicine_id = trim(req.post_param("medicine_id"));
        f.order_date = trim(req.post_param("order_date"));
        f.quantity = trim(req.post_param("quantity"));

        // Редактировать можно только существующую запись.
        if (!f.id.empty()) {
            if (!valid_positive_int(f.id) ||
                db.query("SELECT order_id FROM orders WHERE order_id = ?", {f.id}).empty()) {
                redirect("orders.cgi").send();
                return 0;
            }
        }

        std::map<std::string, std::string> errors;

        if (f.order_number.empty()) {
            errors["order_number"] = "Заполните поле «Идентификатор»";
        } else if (f.order_number.size() > 30) {
            errors["order_number"] = "Идентификатор не длиннее 30 символов";
        } else {
            // Уникальность номера заказа (кроме редактируемой записи).
            auto dup = db.query(
                "SELECT order_id FROM orders WHERE order_number = ?", {f.order_number});
            if (!dup.empty() && (f.id.empty() || dup[0]["order_id"] != f.id)) {
                errors["order_number"] = "Заказ с таким идентификатором уже существует";
            }
        }

        if (f.medicine_id.empty()) {
            errors["medicine_id"] = "Выберите наименование лекарства";
        } else if (!valid_positive_int(f.medicine_id) ||
                   db.query("SELECT medicine_id FROM medicines WHERE medicine_id = ?",
                            {f.medicine_id}).empty()) {
            errors["medicine_id"] = "Такого лекарства нет в каталоге";
        }

        if (f.order_date.empty()) {
            errors["order_date"] = "Заполните поле «Дата»";
        } else if (!valid_date(f.order_date)) {
            errors["order_date"] = "Укажите дату в формате ГГГГ-ММ-ДД";
        }

        if (f.quantity.empty()) {
            errors["quantity"] = "Заполните поле «Количество»";
        } else if (!valid_positive_int(f.quantity)) {
            errors["quantity"] = "Количество — целое число больше нуля";
        }

        if (!errors.empty()) {
            render_form(user, db, f, errors);
            return 0;
        }

        // Сумма заказа считается на сервере по цене из каталога.
        if (f.id.empty()) {
            db.exec(
                "INSERT INTO orders (order_number, medicine_id, quantity, created_at, "
                "total_price) SELECT ?, medicine_id, ?, ?, selling_price * ? "
                "FROM medicines WHERE medicine_id = ?",
                {f.order_number, f.quantity, f.order_date, f.quantity, f.medicine_id});
        } else {
            db.exec(
                "UPDATE orders o JOIN medicines m ON m.medicine_id = ? "
                "SET o.order_number = ?, o.medicine_id = m.medicine_id, o.quantity = ?, "
                "    o.created_at = ?, o.total_price = m.selling_price * ? "
                "WHERE o.order_id = ?",
                {f.medicine_id, f.order_number, f.quantity, f.order_date, f.quantity, f.id});
        }
        redirect("orders.cgi?saved=1").send();
    } catch (const std::exception& e) {
        send_error_page(e.what());
    }
    return 0;
}
