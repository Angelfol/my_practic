// Удаление записи (только POST).
#include "config.hpp"
#include "db.hpp"
#include "html.hpp"
#include "http.hpp"
#include "session.hpp"
#include "validate.hpp"

using namespace apteka;

int main() {
    try {
        Config cfg = load_config();
        Request req = Request::parse(64 * 1024);
        Db db(cfg);
        require_login(db, req);

        if (req.method != "POST") {
            // Удаление по GET-ссылке запрещено.
            redirect("orders.cgi").send();
            return 0;
        }

        std::string id = trim(req.post_param("id"));
        if (valid_positive_int(id)) {
            // Сначала зависимые строки состава, затем сам заказ.
            db.exec("DELETE FROM order_ingredients WHERE order_id = ?", {id});
            db.exec("DELETE FROM orders WHERE order_id = ?", {id});
        }
        redirect("orders.cgi?deleted=1").send();
    } catch (const std::exception& e) {
        send_error_page(e.what());
    }
    return 0;
}
