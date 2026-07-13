#include "db.hpp"

#include <cstring>
#include <type_traits>

#include <mysql.h>

namespace apteka {

// В libmysqlclient 8 поле is_null имеет тип bool*, в MariaDB — my_bool*.
// Выводим фактический тип из самого MYSQL_BIND, чтобы собираться с обоими.
using db_bool = std::remove_pointer_t<decltype(MYSQL_BIND{}.is_null)>;

namespace {

struct StmtGuard {
    MYSQL_STMT* stmt;
    ~StmtGuard() {
        if (stmt) mysql_stmt_close(stmt);
    }
};

struct MetaGuard {
    MYSQL_RES* res;
    ~MetaGuard() {
        if (res) mysql_free_result(res);
    }
};

[[noreturn]] void throw_stmt_error(MYSQL_STMT* stmt, const std::string& what) {
    throw DbError(what + ": " + mysql_stmt_error(stmt));
}

// Подготовка выражения и привязка строковых параметров.
MYSQL_STMT* prepare_and_bind(MYSQL* conn, const std::string& sql,
                             const std::vector<std::string>& params,
                             std::vector<MYSQL_BIND>& binds,
                             std::vector<unsigned long>& lengths) {
    MYSQL_STMT* stmt = mysql_stmt_init(conn);
    if (!stmt) throw DbError("mysql_stmt_init: нет памяти");
    StmtGuard guard{stmt};

    if (mysql_stmt_prepare(stmt, sql.c_str(), static_cast<unsigned long>(sql.size()))) {
        throw_stmt_error(stmt, "Ошибка подготовки запроса");
    }
    if (!params.empty()) {
        binds.resize(params.size());
        lengths.resize(params.size());
        std::memset(binds.data(), 0, sizeof(MYSQL_BIND) * binds.size());
        for (size_t i = 0; i < params.size(); ++i) {
            lengths[i] = static_cast<unsigned long>(params[i].size());
            binds[i].buffer_type = MYSQL_TYPE_STRING;
            binds[i].buffer = const_cast<char*>(params[i].data());
            binds[i].buffer_length = lengths[i];
            binds[i].length = &lengths[i];
        }
        if (mysql_stmt_bind_param(stmt, binds.data())) {
            throw_stmt_error(stmt, "Ошибка привязки параметров");
        }
    }
    guard.stmt = nullptr; // владение передаётся вызывающему
    return stmt;
}

} // namespace

Db::Db(const Config& cfg) {
    conn_ = mysql_init(nullptr);
    if (!conn_) throw DbError("mysql_init: нет памяти");
    MYSQL* h = static_cast<MYSQL*>(conn_);
    if (!mysql_real_connect(h, cfg.db_host.c_str(), cfg.db_user.c_str(),
                            cfg.db_password.c_str(), cfg.db_name.c_str(),
                            cfg.db_port, nullptr, 0)) {
        std::string msg = mysql_error(h);
        mysql_close(h);
        conn_ = nullptr;
        throw DbError("Не удалось подключиться к базе данных: " + msg);
    }
    mysql_set_character_set(h, "utf8mb4");
}

Db::~Db() {
    if (conn_) mysql_close(static_cast<MYSQL*>(conn_));
}

std::vector<Row> Db::query(const std::string& sql,
                           const std::vector<std::string>& params) {
    std::vector<MYSQL_BIND> in_binds;
    std::vector<unsigned long> in_lengths;
    MYSQL_STMT* stmt = prepare_and_bind(static_cast<MYSQL*>(conn_), sql, params, in_binds, in_lengths);
    StmtGuard guard{stmt};

    if (mysql_stmt_execute(stmt)) {
        throw_stmt_error(stmt, "Ошибка выполнения запроса");
    }

    MYSQL_RES* meta = mysql_stmt_result_metadata(stmt);
    if (!meta) return {}; // запрос без результирующего набора
    MetaGuard meta_guard{meta};

    unsigned int ncols = mysql_num_fields(meta);
    MYSQL_FIELD* fields = mysql_fetch_fields(meta);
    std::vector<std::string> names(ncols);
    for (unsigned int i = 0; i < ncols; ++i) names[i] = fields[i].name;

    // Первый проход fetch без буферов: узнаём длины значений,
    // затем дочитываем каждую колонку mysql_stmt_fetch_column.
    std::vector<MYSQL_BIND> out(ncols);
    std::memset(out.data(), 0, sizeof(MYSQL_BIND) * ncols);
    std::vector<unsigned long> lens(ncols, 0);
    std::vector<db_bool> nulls(ncols, 0);
    for (unsigned int i = 0; i < ncols; ++i) {
        out[i].buffer_type = MYSQL_TYPE_STRING;
        out[i].buffer = nullptr;
        out[i].buffer_length = 0;
        out[i].length = &lens[i];
        out[i].is_null = &nulls[i];
    }
    if (mysql_stmt_bind_result(stmt, out.data())) {
        throw_stmt_error(stmt, "Ошибка привязки результата");
    }

    std::vector<Row> rows;
    while (true) {
        int rc = mysql_stmt_fetch(stmt);
        if (rc == MYSQL_NO_DATA) break;
        if (rc == 1) throw_stmt_error(stmt, "Ошибка чтения строки");
        // rc == 0 или MYSQL_DATA_TRUNCATED — данные доступны
        Row row;
        for (unsigned int i = 0; i < ncols; ++i) {
            if (nulls[i]) {
                row[names[i]] = "";
                continue;
            }
            std::string val(lens[i], '\0');
            if (lens[i] > 0) {
                MYSQL_BIND cell;
                std::memset(&cell, 0, sizeof(cell));
                unsigned long cell_len = 0;
                cell.buffer_type = MYSQL_TYPE_STRING;
                cell.buffer = &val[0];
                cell.buffer_length = lens[i];
                cell.length = &cell_len;
                if (mysql_stmt_fetch_column(stmt, &cell, i, 0)) {
                    throw_stmt_error(stmt, "Ошибка чтения значения");
                }
            }
            row[names[i]] = std::move(val);
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

ExecResult Db::exec(const std::string& sql,
                    const std::vector<std::string>& params) {
    std::vector<MYSQL_BIND> in_binds;
    std::vector<unsigned long> in_lengths;
    MYSQL_STMT* stmt = prepare_and_bind(static_cast<MYSQL*>(conn_), sql, params, in_binds, in_lengths);
    StmtGuard guard{stmt};

    if (mysql_stmt_execute(stmt)) {
        throw_stmt_error(stmt, "Ошибка выполнения запроса");
    }
    ExecResult r;
    r.affected_rows = mysql_stmt_affected_rows(stmt);
    r.insert_id = mysql_stmt_insert_id(stmt);
    return r;
}

} // namespace apteka
