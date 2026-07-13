// Модуль поиска: по номеру заказа и наименованию лекарства.
#include <string>

#include "config.hpp"
#include "db.hpp"
#include "html.hpp"
#include "http.hpp"
#include "session.hpp"
#include "validate.hpp"

using namespace apteka;

namespace {

// Экранирование спецсимволов LIKE в пользовательском вводе.
std::string escape_like(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '%' || c == '_' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

} // namespace

int main() {
    try {
        Config cfg = load_config();
        Request req = Request::parse(64 * 1024);
        Db db(cfg);
        AuthUser user = require_login(db, req);

        std::string q = trim(req.get_param("q"));

        Response resp;
        std::string& b = resp.body;
        b += page_begin("Поиск по записям", &user);
        b += "<form class=\"form form-inline\" method=\"get\" action=\"search.cgi\">\n"
             "<div class=\"form-row\"><label for=\"q\">Номер заказа или наименование лекарства</label>"
             "<input id=\"q\" name=\"q\" type=\"text\" value=\"" + esc(q) + "\"></div>\n"
             "<div class=\"form-actions\">"
             "<button type=\"submit\" class=\"btn btn-primary\">Найти</button></div>\n"
             "</form>\n";

        if (!q.empty()) {
            std::string pattern = "%" + escape_like(q) + "%";
            auto rows = db.query(
                "SELECT o.order_number, m.medicine_name, m.form, "
                "       DATE_FORMAT(o.created_at, '%Y-%m-%d') AS order_date, "
                "       o.quantity, o.status, o.total_price "
                "FROM orders o JOIN medicines m ON m.medicine_id = o.medicine_id "
                "WHERE o.order_number LIKE ? OR m.medicine_name LIKE ? "
                "ORDER BY o.created_at DESC, o.order_id DESC",
                {pattern, pattern});

            b += "<h2 class=\"results-title\">Результаты поиска: " +
                 std::to_string(rows.size()) + "</h2>\n";
            if (rows.empty()) {
                b += "<p>По запросу «" + esc(q) + "» ничего не найдено.</p>\n";
            } else {
                b += "<div class=\"cards\">\n";
                for (auto& r : rows) {
                    b += "<div class=\"card\">\n";
                    b += "<h2>Заказ " + esc(r["order_number"]) + "</h2>\n";
                    b += "<p>Лекарство: <strong>" + esc(r["medicine_name"]) +
                         "</strong> (" + esc(r["form"]) + ")</p>\n";
                    b += "<p>Дата: " + esc(r["order_date"]) +
                         " · Количество: " + esc(r["quantity"]) +
                         " · Статус: " + esc(r["status"]) + "</p>\n";
                    b += "<p class=\"med-price\">Сумма заказа: " +
                         esc(r["total_price"]) + " ₽</p>\n";
                    b += "</div>\n";
                }
                b += "</div>\n";
            }
        }
        b += page_end();
        resp.send();
    } catch (const std::exception& e) {
        send_error_page(e.what());
    }
    return 0;
}
