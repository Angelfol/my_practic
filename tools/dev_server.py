#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Мини веб-сервер для ЛОКАЛЬНОЙ разработки: эмулирует Apache + CGI.

На хостинге (Beget) этот файл не нужен — там CGI исполняет настоящий Apache.
Локально же он позволяет открыть сайт в браузере без установки Apache:

    python tools/dev_server.py [порт]     # по умолчанию 8000

Сервер делает ровно то, что делает Apache с CGI-программами:
- запрос к /имя.cgi  -> запускает бинарник, передав окружение CGI
  (REQUEST_METHOD, QUERY_STRING, CONTENT_TYPE, CONTENT_LENGTH, HTTP_COOKIE)
  и тело запроса на stdin, а вывод программы превращает в HTTP-ответ;
- запрос к /public/... и /uploads/...  -> отдаёт статический файл;
- запрос к /  -> перенаправляет на index.cgi (как DirectoryIndex).

Только стандартная библиотека Python, никаких зависимостей.
"""

import http.server
import os
import subprocess
import sys
import urllib.parse

# Консоль Windows может быть не в UTF-8 — не падать на русских сообщениях
for stream in (sys.stdout, sys.stderr):
    if hasattr(stream, "reconfigure"):
        stream.reconfigure(encoding="utf-8", errors="replace")

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8000

# Какие каталоги можно отдавать как статику и с какими типами
STATIC_PREFIXES = ("public/", "uploads/")
CONTENT_TYPES = {
    ".css": "text/css; charset=utf-8",
    ".png": "image/png",
    ".jpg": "image/jpeg",
    ".jpeg": "image/jpeg",
    ".ico": "image/x-icon",
    ".woff2": "font/woff2",
}


class CgiHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        self.route()

    def do_POST(self):
        self.route()

    def route(self):
        parsed = urllib.parse.urlsplit(self.path)
        path = parsed.path.lstrip("/")
        if path == "":
            path = "index.cgi"  # DirectoryIndex

        # Защита от выхода за пределы каталога проекта (../../...)
        fs_path = os.path.normpath(os.path.join(ROOT, path))
        if not fs_path.startswith(os.path.normpath(ROOT)):
            return self.fail(403, "Forbidden")

        if path.endswith(".cgi"):
            self.run_cgi(fs_path, parsed.query)
        elif path.replace("\\", "/").startswith(STATIC_PREFIXES):
            self.serve_static(fs_path)
        else:
            self.fail(404, "Not Found")

    # --- CGI ---------------------------------------------------------------

    def run_cgi(self, fs_path, query):
        if not os.path.isfile(fs_path):
            return self.fail(404, "CGI not found: " + os.path.basename(fs_path))

        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length) if length else b""

        env = os.environ.copy()
        env["GATEWAY_INTERFACE"] = "CGI/1.1"
        env["REQUEST_METHOD"] = self.command
        env["QUERY_STRING"] = query
        env["CONTENT_TYPE"] = self.headers.get("Content-Type") or ""
        env["CONTENT_LENGTH"] = str(length)
        env["HTTP_COOKIE"] = self.headers.get("Cookie") or ""
        env["SERVER_NAME"] = "localhost"
        env["SERVER_PORT"] = str(PORT)
        env["SCRIPT_NAME"] = "/" + os.path.basename(fs_path)

        try:
            proc = subprocess.run(
                [fs_path], input=body, env=env, cwd=ROOT,
                capture_output=True, timeout=30,
            )
        except subprocess.TimeoutExpired:
            return self.fail(504, "CGI timeout")
        if proc.returncode != 0 and not proc.stdout:
            sys.stderr.write(proc.stderr.decode("utf-8", "replace"))
            return self.fail(500, "CGI exited with code %d" % proc.returncode)

        out = proc.stdout
        # Ответ CGI: заголовки, пустая строка, тело
        sep = out.find(b"\r\n\r\n")
        sep_len = 4
        if sep == -1:
            sep = out.find(b"\n\n")
            sep_len = 2
        if sep == -1:
            return self.fail(500, "CGI produced no headers")
        head, payload = out[:sep], out[sep + sep_len:]

        status = 200
        headers = []
        for line in head.decode("utf-8", "replace").splitlines():
            if ":" not in line:
                continue
            name, value = line.split(":", 1)
            name, value = name.strip(), value.strip()
            if name.lower() == "status":
                status = int(value.split()[0])
            else:
                headers.append((name, value))
                if name.lower() == "location" and status == 200:
                    status = 302

        self.send_response(status)
        for name, value in headers:
            self.send_header(name, value)
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    # --- Статика -----------------------------------------------------------

    def serve_static(self, fs_path):
        if not os.path.isfile(fs_path):
            return self.fail(404, "Not Found")
        ext = os.path.splitext(fs_path)[1].lower()
        ctype = CONTENT_TYPES.get(ext, "application/octet-stream")
        with open(fs_path, "rb") as f:
            data = f.read()
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    # --- Служебное ---------------------------------------------------------

    def fail(self, code, message):
        data = message.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, fmt, *args):
        sys.stderr.write("[dev] %s\n" % (fmt % args))


def main():
    addr = ("127.0.0.1", PORT)
    httpd = http.server.ThreadingHTTPServer(addr, CgiHandler)
    print("Сервер разработки: http://127.0.0.1:%d/  (Ctrl+C — остановить)" % PORT)
    print("Корень проекта: %s" % ROOT)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nОстановлено.")


if __name__ == "__main__":
    main()
