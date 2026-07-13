// Главная страница: навигация по модулям (только для авторизованных).
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

        Response resp;
        std::string& b = resp.body;
        b += page_begin("Главная страница", &user);
        b += "<p class=\"lead\">Здравствуйте, " + esc(user.first_name + " " + user.patronymic) +
             "! Это информационная система аптеки «Целебная»: продажа готовых "
             "медикаментов и изготовление лекарств по рецептам.</p>\n";

        b += "<div class=\"cards\">\n";
        b += "<a class=\"card card-link\" href=\"medicines.cgi\">"
             "<h2>Лекарства</h2><p>Каталог товаров аптеки: наименование, описание, "
             "изображение, цена.</p></a>\n";
        b += "<a class=\"card card-link\" href=\"orders.cgi\">"
             "<h2>Заказы</h2><p>Список заказов на изготовление и продажу лекарств: "
             "добавление, редактирование, удаление.</p></a>\n";
        b += "<a class=\"card card-link\" href=\"search.cgi\">"
             "<h2>Поиск</h2><p>Поиск заказов по номеру или наименованию лекарства.</p></a>\n";
        b += "<a class=\"card card-link\" href=\"upload.cgi\">"
             "<h2>Изображения</h2><p>Загрузка изображений (JPG/PNG) и список "
             "загруженных файлов.</p></a>\n";
        b += "<a class=\"card card-link\" href=\"logout.cgi\">"
             "<h2>Выход</h2><p>Завершение сеанса работы с системой.</p></a>\n";
        b += "</div>\n";
        b += page_end();
        resp.send();
    } catch (const std::exception& e) {
        send_error_page(e.what());
    }
    return 0;
}
