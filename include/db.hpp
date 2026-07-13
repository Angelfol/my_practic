// Обёртка над MySQL C API. Все запросы с параметрами выполняются только
// через подготовленные выражения (mysql_stmt_*) — конкатенация SQL
// из пользовательского ввода исключена.
#pragma once

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "config.hpp"

namespace apteka {

using Row = std::map<std::string, std::string>;

struct DbError : std::runtime_error {
    explicit DbError(const std::string& msg) : std::runtime_error(msg) {}
};

struct ExecResult {
    unsigned long long affected_rows = 0;
    unsigned long long insert_id = 0;
};

class Db {
public:
    explicit Db(const Config& cfg);
    ~Db();
    Db(const Db&) = delete;
    Db& operator=(const Db&) = delete;

    // SELECT: возвращает строки; значения NULL приходят пустой строкой.
    std::vector<Row> query(const std::string& sql,
                           const std::vector<std::string>& params = {});

    // INSERT/UPDATE/DELETE.
    ExecResult exec(const std::string& sql,
                    const std::vector<std::string>& params = {});

private:
    // Указатель на MYSQL; тип скрыт (void*), чтобы не тянуть mysql.h в каждый
    // модуль: имя структуры различается в libmysqlclient и MariaDB.
    void* conn_ = nullptr;
};

} // namespace apteka
