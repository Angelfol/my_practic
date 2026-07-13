// Модуль «Добавление изображения»: только JPG/PNG, имя файла генерирует сервер,
// путь и дата загрузки сохраняются в БД.
#include <cctype>
#include <ctime>
#include <fstream>
#include <string>

#include "config.hpp"
#include "db.hpp"
#include "html.hpp"
#include "http.hpp"
#include "multipart.hpp"
#include "session.hpp"
#include "sha256.hpp"
#include "validate.hpp"

using namespace apteka;

namespace {

// Расширение имени файла в нижнем регистре, без точки.
std::string file_ext(const std::string& name) {
    auto dot = name.rfind('.');
    if (dot == std::string::npos) return "";
    std::string ext = name.substr(dot + 1);
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

bool magic_is_png(const std::string& d) {
    static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    if (d.size() < 8) return false;
    for (int i = 0; i < 8; ++i) {
        if (static_cast<unsigned char>(d[i]) != sig[i]) return false;
    }
    return true;
}

bool magic_is_jpeg(const std::string& d) {
    return d.size() >= 3 && static_cast<unsigned char>(d[0]) == 0xff &&
           static_cast<unsigned char>(d[1]) == 0xd8 &&
           static_cast<unsigned char>(d[2]) == 0xff;
}

void render_page(const AuthUser& user, Db& db, const std::string& error,
                 bool uploaded) {
    auto rows = db.query(
        "SELECT file_path, original_name, "
        "       DATE_FORMAT(uploaded_at, '%Y-%m-%d') AS upload_date "
        "FROM uploads ORDER BY uploaded_at DESC, upload_id DESC");

    Response resp;
    std::string& b = resp.body;
    b += page_begin("Загрузка изображений", &user);
    if (uploaded) {
        b += "<div class=\"flash flash-ok\">Изображение загружено.</div>\n";
    }
    if (!error.empty()) {
        b += "<div class=\"flash flash-err\">" + esc(error) + "</div>\n";
    }

    b += "<form class=\"form\" method=\"post\" action=\"upload.cgi\" "
         "enctype=\"multipart/form-data\">\n";
    b += "<div class=\"form-row\"><label for=\"image\">Файл изображения (JPG или PNG, "
         "до 5 МБ)</label><input id=\"image\" name=\"image\" type=\"file\"></div>\n";
    b += "<div class=\"form-actions\">"
         "<button type=\"submit\" class=\"btn btn-primary\">Загрузить</button></div>\n";
    b += "</form>\n";

    b += "<h2>Загруженные изображения</h2>\n";
    if (rows.empty()) {
        b += "<p>Изображений пока нет.</p>\n";
    } else {
        b += "<table class=\"table\">\n<tr><th>Изображение</th><th>Файл</th>"
             "<th>Дата загрузки</th></tr>\n";
        for (auto& r : rows) {
            b += "<tr><td><img class=\"thumb\" src=\"" + esc(r["file_path"]) +
                 "\" alt=\"" + esc(r["original_name"]) + "\"></td>";
            b += "<td>" + esc(r["original_name"]) + "</td>";
            b += "<td>" + esc(r["upload_date"]) + "</td></tr>\n";
        }
        b += "</table>\n";
    }
    b += page_end();
    resp.send();
}

} // namespace

int main() {
    try {
        Config cfg = load_config();
        // Лимит: тело multipart чуть больше максимального размера файла.
        Request req = Request::parse(cfg.max_upload_bytes + 64 * 1024);
        Db db(cfg);
        AuthUser user = require_login(db, req);

        if (req.method != "POST") {
            render_page(user, db, "", req.get_param("uploaded") == "1");
            return 0;
        }

        if (req.body_too_large) {
            render_page(user, db, "Файл слишком большой: допускается не более 5 МБ.", false);
            return 0;
        }

        MultipartData mp = parse_multipart(req.content_type, req.raw_body);
        const FilePart* file = nullptr;
        for (const auto& fp : mp.files) {
            if (fp.field == "image") {
                file = &fp;
                break;
            }
        }

        if (!file || file->filename.empty() || file->data.empty()) {
            render_page(user, db, "Выберите файл изображения.", false);
            return 0;
        }

        std::string ext = file_ext(file->filename);
        bool ext_ok = ext == "jpg" || ext == "jpeg" || ext == "png";
        bool magic_ok = (ext == "png" && magic_is_png(file->data)) ||
                        ((ext == "jpg" || ext == "jpeg") && magic_is_jpeg(file->data));
        if (!ext_ok || !magic_ok) {
            render_page(user, db,
                        "Допустимы только изображения в формате JPG или PNG.", false);
            return 0;
        }

        // Имя файла генерирует сервер — клиентскому имени не доверяем.
        char stamp[32];
        std::time_t now = std::time(nullptr);
        std::tm* tm = std::localtime(&now);
        std::strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", tm);
        std::string fname = "img_" + std::string(stamp) + "_" + random_hex(4) + "." +
                            (ext == "jpeg" ? "jpg" : ext);
        std::string rel_path = cfg.upload_dir + "/" + fname;

        std::ofstream out(rel_path, std::ios::binary);
        if (!out || !out.write(file->data.data(),
                               static_cast<std::streamsize>(file->data.size()))) {
            render_page(user, db,
                        "Не удалось сохранить файл: нет доступа к каталогу загрузок.", false);
            return 0;
        }
        out.close();

        db.exec(
            "INSERT INTO uploads (user_id, file_path, original_name) VALUES (?, ?, ?)",
            {std::to_string(user.id), rel_path, file->filename});

        // PRG: после успешной загрузки — редирект, чтобы избежать повторной отправки.
        redirect("upload.cgi?uploaded=1").send();
    } catch (const std::exception& e) {
        send_error_page(e.what());
    }
    return 0;
}
