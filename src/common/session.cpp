#include "session.hpp"

#include <cstdlib>

#include "sha256.hpp"

namespace apteka {

static const char* kCookieName = "session";

void start_session(Db& db, unsigned long long user_id, int minutes,
                   Response& resp) {
    // Попутная уборка истёкших сессий.
    db.exec("DELETE FROM sessions WHERE expires_at <= NOW()");

    std::string token = random_hex(16); // 128 бит
    db.exec(
        "INSERT INTO sessions (user_id, token, created_at, expires_at) "
        "VALUES (?, ?, NOW(), DATE_ADD(NOW(), INTERVAL ? MINUTE))",
        {std::to_string(user_id), token, std::to_string(minutes)});
    resp.add_cookie(kCookieName, token, minutes * 60, /*http_only=*/true);
}

void clear_session(Db& db, const Request& req, Response& resp) {
    std::string token = req.cookie(kCookieName);
    if (!token.empty()) {
        db.exec("DELETE FROM sessions WHERE token = ?", {token});
    }
    resp.add_cookie(kCookieName, "", 0, /*http_only=*/true); // погасить cookie
}

std::optional<AuthUser> current_user(Db& db, const Request& req) {
    std::string token = req.cookie(kCookieName);
    if (token.empty() || token.size() > 64) return std::nullopt;

    auto rows = db.query(
        "SELECT u.user_id, u.first_name, u.last_name, u.patronymic, u.email "
        "FROM sessions s JOIN users u ON u.user_id = s.user_id "
        "WHERE s.token = ? AND s.expires_at > NOW()",
        {token});
    if (rows.empty()) return std::nullopt;

    AuthUser user;
    user.id = std::strtoull(rows[0]["user_id"].c_str(), nullptr, 10);
    user.first_name = rows[0]["first_name"];
    user.last_name = rows[0]["last_name"];
    user.patronymic = rows[0]["patronymic"];
    user.email = rows[0]["email"];
    return user;
}

AuthUser require_login(Db& db, const Request& req) {
    auto user = current_user(db, req);
    if (!user) {
        redirect("login.cgi").send();
        std::exit(0);
    }
    return *user;
}

} // namespace apteka
