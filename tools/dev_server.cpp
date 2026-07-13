// Локальный веб-сервер разработки на standalone Asio: эмулирует Apache + CGI.
//
// На хостинге (Beget) не нужен — там CGI исполняет настоящий Apache. Локально
// позволяет открыть сайт в браузере без установки Apache:
//
//     make devserver && ./dev_server [порт]      # по умолчанию 8000
//
// Запускать из корня проекта (рядом с *.cgi). Делает то же, что Apache:
//   /имя.cgi            -> запуск бинарника с окружением CGI (REQUEST_METHOD,
//                          QUERY_STRING, CONTENT_TYPE, CONTENT_LENGTH,
//                          HTTP_COOKIE), тело запроса на stdin
//   /public/..., /uploads/... -> статические файлы
//   /                   -> index.cgi (DirectoryIndex)
// Всё остальное (config/, src/, ...) наружу не отдаётся.
//
// Сеть — Asio (см. third_party/asio), по потоку на соединение; запуск
// CGI-процессов — через временные файлы stdin/stdout (без пайпов — нет
// взаимоблокировок). Инструмент разработки: в состав сервиса не входит.

#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#endif

#include <asio.hpp>

#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using asio::ip::tcp;

namespace {

const int kCgiTimeoutSec = 30;
std::atomic<unsigned long> g_seq{0};

// --- утилиты -----------------------------------------------------------

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string content_type_for(const std::string& path) {
    static const std::map<std::string, std::string> kTypes = {
        {".css", "text/css; charset=utf-8"}, {".png", "image/png"},
        {".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"},
        {".ico", "image/x-icon"}, {".woff2", "font/woff2"},
    };
    auto dot = path.find_last_of('.');
    if (dot != std::string::npos) {
        auto it = kTypes.find(lower(path.substr(dot)));
        if (it != kTypes.end()) return it->second;
    }
    return "application/octet-stream";
}

std::string status_text(int code) {
    switch (code) {
        case 200: return "OK";
        case 302: return "Found";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 500: return "Internal Server Error";
        case 504: return "Gateway Timeout";
        default:  return "OK";
    }
}

std::string temp_name(const char* suffix) {
    std::ostringstream os;
    os << "devsrv_" <<
#ifdef _WIN32
        GetCurrentProcessId()
#else
        getpid()
#endif
       << "_" << g_seq.fetch_add(1) << suffix;
    return (fs::temp_directory_path() / os.str()).string();
}

bool read_file(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// --- запуск CGI-процесса -------------------------------------------------
// Возвращает true и сырой вывод программы (заголовки + тело) либо false.

struct CgiVars {
    std::string method, query, content_type, content_length, cookie, script;
};

#ifdef _WIN32

bool run_cgi_process(const std::string& exe, const CgiVars& v,
                     const std::string& body, std::string& out, std::string& err) {
    const std::string in_path = temp_name(".in");
    const std::string out_path = temp_name(".out");
    {
        std::ofstream f(in_path, std::ios::binary);
        f.write(body.data(), static_cast<std::streamsize>(body.size()));
    }

    // Блок окружения: текущее окружение + переменные CGI.
    std::string block;
    for (LPCH env = GetEnvironmentStringsA(); env && *env;) {
        std::string entry(env);
        env += entry.size() + 1;
        block += entry;
        block.push_back('\0');
    }
    const std::pair<const char*, const std::string*> vars[] = {
        {"GATEWAY_INTERFACE=CGI/1.1", nullptr},
        {"REQUEST_METHOD=", &v.method},   {"QUERY_STRING=", &v.query},
        {"CONTENT_TYPE=", &v.content_type}, {"CONTENT_LENGTH=", &v.content_length},
        {"HTTP_COOKIE=", &v.cookie},      {"SCRIPT_NAME=", &v.script},
    };
    for (const auto& kv : vars) {
        block += kv.first;
        if (kv.second) block += *kv.second;
        block.push_back('\0');
    }
    block.push_back('\0');

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE h_in = CreateFileA(in_path.c_str(), GENERIC_READ, FILE_SHARE_READ, &sa,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE h_out = CreateFileA(out_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h_in == INVALID_HANDLE_VALUE || h_out == INVALID_HANDLE_VALUE) {
        err = "cannot open temp files";
        return false;
    }

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = h_in;
    si.hStdOutput = h_out;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION pi{};
    std::string cmdline = "\"" + exe + "\"";

    BOOL ok = CreateProcessA(exe.c_str(), cmdline.data(), nullptr, nullptr, TRUE,
                             0, block.data(), nullptr, &si, &pi);
    CloseHandle(h_in);
    CloseHandle(h_out);
    if (!ok) {
        err = "CreateProcess failed, code " + std::to_string(GetLastError());
        return false;
    }
    bool finished = WaitForSingleObject(pi.hProcess, kCgiTimeoutSec * 1000) == WAIT_OBJECT_0;
    if (!finished) TerminateProcess(pi.hProcess, 1);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    bool have = read_file(out_path, out);
    std::error_code ec;
    fs::remove(in_path, ec);
    fs::remove(out_path, ec);
    if (!finished) { err = "CGI timeout"; return false; }
    if (!have) { err = "no CGI output"; return false; }
    return true;
}

#else  // POSIX

bool run_cgi_process(const std::string& exe, const CgiVars& v,
                     const std::string& body, std::string& out, std::string& err) {
    const std::string in_path = temp_name(".in");
    const std::string out_path = temp_name(".out");
    {
        std::ofstream f(in_path, std::ios::binary);
        f.write(body.data(), static_cast<std::streamsize>(body.size()));
    }

    pid_t pid = fork();
    if (pid < 0) { err = "fork failed"; return false; }
    if (pid == 0) {
        if (!std::freopen(in_path.c_str(), "rb", stdin) ||
            !std::freopen(out_path.c_str(), "wb", stdout))
            _exit(127);
        setenv("GATEWAY_INTERFACE", "CGI/1.1", 1);
        setenv("REQUEST_METHOD", v.method.c_str(), 1);
        setenv("QUERY_STRING", v.query.c_str(), 1);
        setenv("CONTENT_TYPE", v.content_type.c_str(), 1);
        setenv("CONTENT_LENGTH", v.content_length.c_str(), 1);
        setenv("HTTP_COOKIE", v.cookie.c_str(), 1);
        setenv("SCRIPT_NAME", v.script.c_str(), 1);
        execl(exe.c_str(), exe.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    // Ожидание с тайм-аутом (poll каждые 100 мс).
    int status = 0;
    bool finished = false;
    for (int i = 0; i < kCgiTimeoutSec * 10; ++i) {
        if (waitpid(pid, &status, WNOHANG) == pid) { finished = true; break; }
        usleep(100 * 1000);
    }
    if (!finished) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
    }

    bool have = read_file(out_path, out);
    std::error_code ec;
    fs::remove(in_path, ec);
    fs::remove(out_path, ec);
    if (!finished) { err = "CGI timeout"; return false; }
    if (!have || (WIFEXITED(status) && WEXITSTATUS(status) == 127)) {
        err = "cannot execute CGI";
        return false;
    }
    return true;
}

#endif

// --- HTTP ----------------------------------------------------------------

void write_response(tcp::socket& sock, int code,
                    const std::vector<std::pair<std::string, std::string>>& headers,
                    const std::string& body) {
    std::ostringstream os;
    os << "HTTP/1.1 " << code << " " << status_text(code) << "\r\n";
    bool have_type = false;
    for (const auto& h : headers) {
        if (lower(h.first) == "content-type") have_type = true;
        os << h.first << ": " << h.second << "\r\n";
    }
    if (!have_type) os << "Content-Type: text/plain; charset=utf-8\r\n";
    os << "Content-Length: " << body.size() << "\r\n"
       << "Connection: close\r\n\r\n";
    std::string head = os.str();
    asio::error_code ec;
    asio::write(sock, asio::buffer(head), ec);
    if (!ec) asio::write(sock, asio::buffer(body), ec);
}

void simple_response(tcp::socket& sock, int code, const std::string& msg) {
    write_response(sock, code, {}, msg + "\n");
}

void log_line(const std::string& s) {
    std::cerr << "[dev] " << s << "\n";
}

void handle_cgi(tcp::socket& sock, const fs::path& root, const std::string& fs_path,
                const std::string& script, const std::string& query,
                const std::string& method,
                const std::map<std::string, std::string>& headers,
                const std::string& body) {
    CgiVars v;
    v.method = method;
    v.query = query;
    v.script = "/" + script;
    v.content_length = std::to_string(body.size());
    auto it = headers.find("content-type");
    if (it != headers.end()) v.content_type = it->second;
    it = headers.find("cookie");
    if (it != headers.end()) v.cookie = it->second;

    // Рабочий каталог процесса — корень проекта (config/, uploads/ по
    // относительным путям). Сервер запускается из корня, поэтому просто
    // передаём абсолютный путь бинарника.
    (void)root;

    std::string out, err;
    if (!run_cgi_process(fs_path, v, body, out, err)) {
        log_line("CGI " + script + " FAILED: " + err);
        return simple_response(sock, err == "CGI timeout" ? 504 : 500, err);
    }

    // Ответ CGI: заголовки, пустая строка, тело.
    size_t sep = out.find("\r\n\r\n");
    size_t sep_len = 4;
    if (sep == std::string::npos) {
        sep = out.find("\n\n");
        sep_len = 2;
    }
    if (sep == std::string::npos)
        return simple_response(sock, 500, "CGI produced no headers");

    std::string head = out.substr(0, sep);
    std::string payload = out.substr(sep + sep_len);

    int code = 200;
    std::vector<std::pair<std::string, std::string>> resp_headers;
    std::istringstream hs(head);
    std::string line;
    while (std::getline(hs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        while (!value.empty() && value.front() == ' ') value.erase(0, 1);
        if (lower(name) == "status") {
            code = std::atoi(value.c_str());
        } else {
            if (lower(name) == "location" && code == 200) code = 302;
            resp_headers.emplace_back(name, value);
        }
    }
    log_line(method + " /" + script + (query.empty() ? "" : "?" + query) +
             " -> " + std::to_string(code));
    write_response(sock, code, resp_headers, payload);
}

void handle_static(tcp::socket& sock, const std::string& fs_path,
                   const std::string& rel) {
    std::string data;
    if (!fs::is_regular_file(fs_path) || !read_file(fs_path, data))
        return simple_response(sock, 404, "Not Found");
    log_line("GET /" + rel + " -> 200 (" + std::to_string(data.size()) + " bytes)");
    write_response(sock, 200, {{"Content-Type", content_type_for(fs_path)}}, data);
}

void handle_connection(tcp::socket sock, const fs::path& root) {
    try {
        asio::streambuf buf;
        asio::read_until(sock, buf, "\r\n\r\n");
        std::istream is(&buf);

        std::string method, target, version;
        is >> method >> target >> version;
        std::string dummy;
        std::getline(is, dummy); // остаток строки запроса

        std::map<std::string, std::string> headers;
        std::string line;
        while (std::getline(is, line) && line != "\r" && !line.empty()) {
            if (line.back() == '\r') line.pop_back();
            auto colon = line.find(':');
            if (colon == std::string::npos) continue;
            std::string name = lower(line.substr(0, colon));
            std::string value = line.substr(colon + 1);
            while (!value.empty() && value.front() == ' ') value.erase(0, 1);
            headers[name] = value;
        }

        if (method != "GET" && method != "POST")
            return simple_response(sock, 405, "Method Not Allowed");

        // Тело: часть могла уже попасть в буфер read_until.
        size_t content_length = 0;
        auto cl = headers.find("content-length");
        if (cl != headers.end())
            content_length = static_cast<size_t>(std::strtoull(cl->second.c_str(), nullptr, 10));
        std::string body;
        if (content_length > 0) {
            body.reserve(content_length);
            std::size_t in_buf = buf.size();
            if (in_buf > 0) {
                std::size_t take = std::min(in_buf, content_length);
                std::istreambuf_iterator<char> it(&buf);
                for (std::size_t i = 0; i < take; ++i, ++it) body.push_back(*it);
            }
            if (body.size() < content_length) {
                std::string rest(content_length - body.size(), '\0');
                asio::read(sock, asio::buffer(&rest[0], rest.size()));
                body += rest;
            }
        }

        // Маршрутизация (как в Apache с нашим .htaccess).
        std::string path = target;
        std::string query;
        auto qpos = path.find('?');
        if (qpos != std::string::npos) {
            query = path.substr(qpos + 1);
            path = path.substr(0, qpos);
        }
        while (!path.empty() && path.front() == '/') path.erase(0, 1);
        if (path.empty()) path = "index.cgi"; // DirectoryIndex

        // Защита от выхода за пределы корня (../../...).
        fs::path fs_path = fs::weakly_canonical(root / path);
        auto rel_check = fs_path.lexically_relative(root).generic_string();
        if (rel_check.empty() || rel_check.rfind("..", 0) == 0)
            return simple_response(sock, 403, "Forbidden");

        std::string rel = fs_path.lexically_relative(root).generic_string();
        if (rel.size() > 4 && rel.substr(rel.size() - 4) == ".cgi" &&
            rel.find('/') == std::string::npos) {
            if (!fs::is_regular_file(fs_path))
                return simple_response(sock, 404, "CGI not found: " + rel);
            handle_cgi(sock, root, fs_path.string(), rel, query, method, headers, body);
        } else if (rel.rfind("public/", 0) == 0 || rel.rfind("uploads/", 0) == 0) {
            handle_static(sock, fs_path.string(), rel);
        } else {
            simple_response(sock, 404, "Not Found");
        }
    } catch (const std::exception& e) {
        log_line(std::string("connection error: ") + e.what());
    }
    asio::error_code ec;
    sock.shutdown(tcp::socket::shutdown_both, ec);
}

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    unsigned short port = 8000;
    if (argc > 1) port = static_cast<unsigned short>(std::atoi(argv[1]));

    const fs::path root = fs::current_path();
    if (!fs::exists(root / "index.cgi"))
        std::cerr << "[dev] ВНИМАНИЕ: index.cgi не найден в " << root.string()
                  << " — запускайте из корня проекта после сборки (make)\n";

    try {
        asio::io_context io;
        tcp::acceptor acceptor(io, tcp::endpoint(asio::ip::make_address("127.0.0.1"), port));
        std::cout << "Сервер разработки (Asio): http://127.0.0.1:" << port
                  << "/  (Ctrl+C — остановить)\n"
                  << "Корень проекта: " << root.string() << "\n";
        for (;;) {
            tcp::socket sock(io);
            acceptor.accept(sock);
            std::thread(handle_connection, std::move(sock), root).detach();
        }
    } catch (const std::exception& e) {
        std::cerr << "[dev] fatal: " << e.what() << "\n";
        return 1;
    }
}
