#include "config.hpp"

#include <cstdlib>
#include <fstream>
#include <stdexcept>

#include "validate.hpp" // trim

namespace apteka {

Config load_config() {
    const char* env_path = std::getenv("APP_CONFIG");
    std::string path = env_path && *env_path ? env_path : "config/app.conf";

    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("Не найден файл конфигурации: " + path);
    }

    Config cfg;
    std::string line;
    while (std::getline(in, line)) {
        std::string s = trim(line);
        if (s.empty() || s[0] == '#') continue;
        auto eq = s.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(s.substr(0, eq));
        std::string value = trim(s.substr(eq + 1));

        if (key == "db_host") cfg.db_host = value;
        else if (key == "db_port") cfg.db_port = static_cast<unsigned int>(std::strtoul(value.c_str(), nullptr, 10));
        else if (key == "db_name") cfg.db_name = value;
        else if (key == "db_user") cfg.db_user = value;
        else if (key == "db_password") cfg.db_password = value;
        else if (key == "session_minutes") cfg.session_minutes = std::atoi(value.c_str());
        else if (key == "upload_dir") cfg.upload_dir = value;
        else if (key == "max_upload_bytes") cfg.max_upload_bytes = std::strtoull(value.c_str(), nullptr, 10);
        // неизвестные ключи игнорируются
    }

    if (cfg.db_name.empty() || cfg.db_user.empty()) {
        throw std::runtime_error("В конфигурации не заданы db_name/db_user (" + path + ")");
    }
    if (cfg.session_minutes <= 0) cfg.session_minutes = 120;
    return cfg;
}

} // namespace apteka
