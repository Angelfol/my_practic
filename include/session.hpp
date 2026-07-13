// Сессии на cookie-токенах и guard доступа.
#pragma once

#include <optional>
#include <string>

#include "db.hpp"
#include "http.hpp"

namespace apteka {

struct AuthUser {
    unsigned long long id = 0;
    std::string first_name;
    std::string last_name;
    std::string patronymic;
    std::string email;
};

// Создаёт сессию для пользователя и добавляет HttpOnly-cookie в ответ.
void start_session(Db& db, unsigned long long user_id, int minutes,
                   Response& resp);

// Удаляет сессию из БД и гасит cookie.
void clear_session(Db& db, const Request& req, Response& resp);

// Возвращает пользователя по действующей (не истёкшей) сессии.
std::optional<AuthUser> current_user(Db& db, const Request& req);

// Единый guard: неавторизованный посетитель получает redirect на login.cgi,
// после чего процесс завершается. Вызывается в начале каждого защищённого
// модуля.
AuthUser require_login(Db& db, const Request& req);

} // namespace apteka
