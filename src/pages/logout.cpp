// Модуль «Выход»: уничтожение сессии и возврат на экран входа.
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

        Response resp = redirect("login.cgi");
        clear_session(db, req, resp);
        resp.send();
    } catch (const std::exception& e) {
        send_error_page(e.what());
    }
    return 0;
}
