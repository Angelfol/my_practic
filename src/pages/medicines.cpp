// Модуль «Информация о товарах/услугах»: каталог лекарств из БД.
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

        auto meds = db.query(
            "SELECT medicine_name, medicine_type, category, form, dosage, unit, "
            "       manufacturer, selling_price, description, image_path "
            "FROM medicines ORDER BY medicine_name");

        Response resp;
        std::string& b = resp.body;
        b += page_begin("Лекарства", &user);
        b += "<p class=\"lead\">Каталог товаров аптеки: наименование, описание и "
             "изображение выводятся из базы данных.</p>\n";
        b += "<div class=\"cards\">\n";
        for (auto& m : meds) {
            b += "<div class=\"card med-card\">\n";
            if (!m["image_path"].empty()) {
                b += "<img class=\"med-img\" src=\"" + esc(m["image_path"]) +
                     "\" alt=\"" + esc(m["medicine_name"]) + "\">\n";
            }
            b += "<h2>" + esc(m["medicine_name"]) + "</h2>\n";
            b += "<p class=\"med-meta\">" + esc(m["medicine_type"]) + " · " +
                 esc(m["category"]) + " · " + esc(m["form"]);
            if (!m["dosage"].empty()) b += ", " + esc(m["dosage"]);
            b += "</p>\n";
            b += "<p>" + esc(m["description"]) + "</p>\n";
            b += "<p class=\"med-meta\">Производитель: " + esc(m["manufacturer"]) + "</p>\n";
            b += "<p class=\"med-price\">" + esc(m["selling_price"]) + " ₽ за " +
                 esc(m["unit"]) + "</p>\n";
            b += "</div>\n";
        }
        b += "</div>\n";
        b += page_end();
        resp.send();
    } catch (const std::exception& e) {
        send_error_page(e.what());
    }
    return 0;
}
