// Конфигурация приложения: чтение config/app.conf (формат «ключ=значение»).
// Путь к файлу можно переопределить переменной окружения APP_CONFIG
// (используется в тестах).
#pragma once

#include <string>

namespace apteka {

struct Config {
    std::string db_host = "localhost";
    unsigned int db_port = 3306;
    std::string db_name;
    std::string db_user;
    std::string db_password;
    int session_minutes = 120;          // время жизни сессии
    std::string upload_dir = "uploads"; // каталог для загружаемых изображений
    size_t max_upload_bytes = 5 * 1024 * 1024;
};

// Загружает конфигурацию; бросает std::runtime_error, если файл недоступен
// или отсутствуют обязательные ключи (db_name, db_user).
Config load_config();

} // namespace apteka
