// Модуль «Список записей»: заказы аптеки.
#include "config.hpp"
#include "db.hpp"
#include "html.hpp"
#include "http.hpp"
#include "session.hpp"

using namespace apteka;

int main() {
    try {
        Config cfg = load_config();
        Request req = Request::parse(64 * 1024);
        Db db(cfg);
        AuthUser user = require_login(db, req);

        auto orders = db.query(
            "SELECT o.order_id, o.order_number, m.medicine_name, "
            "       DATE_FORMAT(o.created_at, '%Y-%m-%d') AS order_date, "
            "       o.quantity, o.status "
            "FROM orders o JOIN medicines m ON m.medicine_id = o.medicine_id "
            "ORDER BY o.created_at DESC, o.order_id DESC");

        Response resp;
        std::string& b = resp.body;
        b += page_begin("Заказы", &user);

        if (req.get_param("saved") == "1") {
            b += "<div class=\"flash flash-ok\">Запись сохранена.</div>\n";
        } else if (req.get_param("deleted") == "1") {
            b += "<div class=\"flash flash-ok\">Запись удалена.</div>\n";
        }

        b += "<p><a class=\"btn btn-primary\" href=\"order_form.cgi\">Добавить запись</a></p>\n";

        if (orders.empty()) {
            b += "<p>Записей пока нет.</p>\n";
        } else {
            b += "<table class=\"table\">\n<tr>"
                 "<th>Идентификационный номер</th><th>Наименование</th>"
                 "<th>Дата</th><th>Количество</th><th>Статус</th>"
                 "<th class=\"col-actions\">Действия</th></tr>\n";
            for (auto& o : orders) {
                b += "<tr>";
                b += "<td>" + esc(o["order_number"]) + "</td>";
                b += "<td>" + esc(o["medicine_name"]) + "</td>";
                b += "<td>" + esc(o["order_date"]) + "</td>";
                b += "<td>" + esc(o["quantity"]) + "</td>";
                b += "<td>" + esc(o["status"]) + "</td>";
                b += "<td class=\"col-actions\">"
                     "<a class=\"btn btn-small\" href=\"order_form.cgi?id=" +
                     esc(o["order_id"]) + "\">Редактировать</a> " +
                     "<form class=\"inline-form\" method=\"post\" action=\"order_delete.cgi\">"
                     "<input type=\"hidden\" name=\"id\" value=\"" + esc(o["order_id"]) + "\">"
                     "<button type=\"submit\" class=\"btn btn-small btn-danger\">Удалить</button>"
                     "</form></td>";
                b += "</tr>\n";
            }
            b += "</table>\n";
        }
        b += page_end();
        resp.send();
    } catch (const std::exception& e) {
        send_error_page(e.what());
    }
    return 0;
}
