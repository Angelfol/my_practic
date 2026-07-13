#!/usr/bin/env bash
# HTTP-проверки запуска проекта: скрипт сам поднимает локальный дев-сервер
# (tools/dev_server.cpp, Asio) и проверяет сайт «снаружи» через curl — так,
# как с ним работал бы браузер: редиректы, вход по cookie, статика, загрузка
# файла, защита служебных каталогов.
#
# Использование:  bash tests/http_smoke.sh [порт]     # по умолчанию 8090
# Требуется: собранные *.cgi и dev_server (make && make devserver),
# запущенная БД с данными seed.sql, заполненный config/app.conf.
#
# Дополняет tests/smoke.sh (тот прогоняет CGI-бинарники напрямую, без сети).

set -u
cd "$(dirname "$0")/.."

PORT="${1:-8090}"
BASE="http://127.0.0.1:$PORT"
TMP="$(mktemp -d)"
JAR="$TMP/cookies.txt"
PASS=0
FAIL=0

ok()  { PASS=$((PASS+1)); printf 'ok       %s\n' "$1"; }
bad() { FAIL=$((FAIL+1)); printf 'FAIL     %s\n' "$1"; }

# check_code "описание" ожидаемый_код фактический_код
check_code() {
    if [ "$3" = "$2" ]; then ok "$1"; else bad "$1 (получен код $3, ожидался $2)"; fi
}
# check_contains "описание" файл строка
check_contains() {
    if grep -q "$3" "$2" 2>/dev/null; then ok "$1"; else bad "$1"; fi
}

BIN=./dev_server
[ -x ./dev_server.exe ] && BIN=./dev_server.exe
if [ ! -x "$BIN" ]; then
    echo "Не найден dev_server — соберите его: make devserver" >&2
    exit 2
fi
if [ ! -f index.cgi ]; then
    echo "Не найдены *.cgi — соберите проект: make" >&2
    exit 2
fi

"$BIN" "$PORT" >"$TMP/server.log" 2>&1 &
SRV=$!
trap 'kill $SRV 2>/dev/null; rm -rf "$TMP"' EXIT

# Ожидание готовности сервера (до 10 секунд)
READY=0
for _ in $(seq 1 40); do
    if curl -s -o /dev/null --max-time 2 "$BASE/login.cgi"; then READY=1; break; fi
    sleep 0.25
done
if [ "$READY" != 1 ]; then
    echo "Сервер не поднялся на порту $PORT" >&2
    cat "$TMP/server.log" >&2
    exit 2
fi
ok "дев-сервер (Asio) поднялся на порту $PORT"

echo "=== Доступ без авторизации ==="
CODE=$(curl -s -o /dev/null -w '%{http_code}' "$BASE/")
check_code "GET / без сессии -> 302" 302 "$CODE"
LOC=$(curl -s -o /dev/null -w '%{redirect_url}' "$BASE/")
case "$LOC" in
    *login.cgi*) ok "редирект ведёт на страницу входа" ;;
    *)           bad "редирект ведёт на страницу входа (получено: $LOC)" ;;
esac
CODE=$(curl -s -o /dev/null -w '%{http_code}' "$BASE/orders.cgi")
check_code "orders.cgi без сессии -> 302" 302 "$CODE"

echo "=== Вход ==="
CODE=$(curl -s -o "$TMP/r" -w '%{http_code}' "$BASE/login.cgi")
check_code "страница входа -> 200" 200 "$CODE"
curl -s -o "$TMP/r" -d "email=test%40example.com&password=WRONG" "$BASE/login.cgi"
check_contains "неверный пароль -> текст ошибки" "$TMP/r" "Неверный e-mail или пароль"
CODE=$(curl -s -o /dev/null -w '%{http_code}' -c "$JAR" \
    -d "email=test%40example.com&password=test123" "$BASE/login.cgi")
check_code "верные данные -> 302" 302 "$CODE"
check_contains "выдана cookie сессии" "$JAR" "session"

echo "=== Страницы под сессией ==="
CODE=$(curl -s -o "$TMP/r" -w '%{http_code}' -b "$JAR" "$BASE/index.cgi")
check_code "главная -> 200" 200 "$CODE"
check_contains "главная содержит заголовок" "$TMP/r" "Главная страница"
curl -s -o "$TMP/r" -b "$JAR" "$BASE/medicines.cgi"
check_contains "каталог: лекарство из БД" "$TMP/r" "Парацетамол"
check_contains "каталог: изображение из БД" "$TMP/r" "uploads/seed_med_01.png"
curl -s -o "$TMP/r" -b "$JAR" "$BASE/orders.cgi"
check_contains "список записей: заказ из БД" "$TMP/r" "ORD-2026-0001"
curl -s -o "$TMP/r" -b "$JAR" "$BASE/search.cgi?q=ORD-2026-0001"
check_contains "поиск: найден заказ" "$TMP/r" "ORD-2026-0001"

echo "=== Статика ==="
CODE=$(curl -s -o /dev/null -w '%{http_code} %{content_type}' "$BASE/public/style.css")
case "$CODE" in
    "200 text/css"*) ok "style.css -> 200, text/css" ;;
    *)               bad "style.css -> 200, text/css (получено: $CODE)" ;;
esac
CODE=$(curl -s -o /dev/null -w '%{http_code} %{content_type}' "$BASE/public/fonts/iosevka-term-regular.woff2")
case "$CODE" in
    "200 font/woff2"*) ok "шрифт Iosevka -> 200, font/woff2" ;;
    *)                 bad "шрифт Iosevka -> 200, font/woff2 (получено: $CODE)" ;;
esac
CODE=$(curl -s -o /dev/null -w '%{http_code} %{content_type}' "$BASE/uploads/seed_med_01.png")
case "$CODE" in
    "200 image/png"*) ok "картинка -> 200, image/png" ;;
    *)                bad "картинка -> 200, image/png (получено: $CODE)" ;;
esac

echo "=== Защита служебных файлов ==="
CODE=$(curl -s -o /dev/null -w '%{http_code}' "$BASE/config/app.conf")
check_code "config/app.conf недоступен" 404 "$CODE"
CODE=$(curl -s -o /dev/null -w '%{http_code}' "$BASE/sql/schema.sql")
check_code "sql/schema.sql недоступен" 404 "$CODE"
CODE=$(curl -s -o /dev/null -w '%{http_code}' --path-as-is "$BASE/uploads/../config/app.conf")
case "$CODE" in
    403|404) ok "обход каталога (..) отклонён" ;;
    *)       bad "обход каталога (..) отклонён (получен код $CODE)" ;;
esac

echo "=== Загрузка изображения по HTTP ==="
# Файлы для curl -F создаются по ОТНОСИТЕЛЬНОМУ пути: в Git Bash (Windows)
# абсолютный /tmp/... с ';type=' внутри аргумента ломает автоконвертацию
# путей, и curl не может открыть файл (ошибка 26).
mkdir -p build
printf 'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==' \
    | base64 -d >build/http_test.png
LOC=$(curl -s -o /dev/null -w '%{redirect_url}' -b "$JAR" \
    -F "image=@build/http_test.png" "$BASE/upload.cgi")
case "$LOC" in
    *uploaded=1*) ok "multipart-загрузка PNG -> редирект об успехе" ;;
    *)            bad "multipart-загрузка PNG -> редирект об успехе (получено: $LOC)" ;;
esac
curl -s -o "$TMP/r" -b "$JAR" "$BASE/upload.cgi"
check_contains "файл появился в таблице загрузок" "$TMP/r" "http_test.png"
printf 'это не картинка' >build/fake.png
curl -s -o "$TMP/r" -b "$JAR" -F "image=@build/fake.png" "$BASE/upload.cgi"
check_contains "поддельный PNG отклонён" "$TMP/r" "Допустимы только изображения"
rm -f build/http_test.png build/fake.png

echo "=== Выход ==="
CODE=$(curl -s -o /dev/null -w '%{http_code}' -b "$JAR" "$BASE/logout.cgi")
check_code "выход -> 302" 302 "$CODE"
CODE=$(curl -s -o /dev/null -w '%{http_code}' -b "$JAR" "$BASE/orders.cgi")
check_code "старая сессия недействительна" 302 "$CODE"

echo
echo "Итого: пройдено $PASS, провалено $FAIL"
[ "$FAIL" = 0 ]
