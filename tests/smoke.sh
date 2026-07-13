#!/usr/bin/env bash
# Сквозная (smoke) проверка CGI-модулей информационной системы аптеки.
#
# Бинарники вызываются напрямую, как это делает веб-сервер: переменные
# окружения CGI (REQUEST_METHOD, QUERY_STRING, CONTENT_TYPE, CONTENT_LENGTH,
# HTTP_COOKIE) и тело запроса на stdin.
#
# Требования: собранные *.cgi в корне проекта (make), доступная БД
# с накатанными schema.sql и seed.sql, config/app.conf с реквизитами.
# Запуск:  bash tests/smoke.sh
# Код возврата: 0 — все проверки пройдены, 1 — есть провалы.

set -u
cd "$(dirname "$0")/.."

PASS=0
FAIL=0
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# check <название> <файл-с-ответом> <ожидаемая-подстрока>
check() {
    if grep -q -- "$3" "$2"; then
        PASS=$((PASS + 1))
        echo "ok       $1"
    else
        FAIL=$((FAIL + 1))
        echo "FAILED   $1  (нет строки: $3)"
    fi
}

# check_absent <название> <файл-с-ответом> <строка-которой-быть-не-должно>
check_absent() {
    if grep -q -- "$3" "$2"; then
        FAIL=$((FAIL + 1))
        echo "FAILED   $1  (найдена лишняя строка: $3)"
    else
        PASS=$((PASS + 1))
        echo "ok       $1"
    fi
}

# run_cgi <бинарник> <метод> <query> <cookie> <файл-тела|-> [content-type] > ответ
run_cgi() {
    local bin=$1 method=$2 query=$3 cookie=$4 body=$5 ctype=${6:-application/x-www-form-urlencoded}
    if [ "$body" = "-" ]; then
        REQUEST_METHOD=$method QUERY_STRING=$query HTTP_COOKIE=$cookie \
            "./$bin" </dev/null
    else
        REQUEST_METHOD=$method QUERY_STRING=$query HTTP_COOKIE=$cookie \
            CONTENT_TYPE=$ctype CONTENT_LENGTH=$(wc -c <"$body") \
            "./$bin" <"$body"
    fi
}

post_body() { # printf-форматирование тела в файл, возвращает имя файла
    printf '%s' "$1" >"$TMP/body"
    echo "$TMP/body"
}

STAMP=$(date +%s)
EMAIL="smoke${STAMP}@test.ru"
ORDER_NUM="SMOKE-${STAMP}"
TODAY=$(date +%Y-%m-%d)

echo "=== Права доступа ==="
run_cgi orders.cgi GET "" "" - >"$TMP/r"
check "orders.cgi без сессии -> редирект на вход" "$TMP/r" "Location: login.cgi"
run_cgi upload.cgi GET "" "" - >"$TMP/r"
check "upload.cgi без сессии -> редирект на вход" "$TMP/r" "Location: login.cgi"
run_cgi index.cgi GET "" "" - >"$TMP/r"
check "index.cgi без сессии -> редирект на вход" "$TMP/r" "Location: login.cgi"

echo "=== Регистрация ==="
run_cgi register.cgi GET "" "" - >"$TMP/r"
check "форма регистрации открывается" "$TMP/r" "Регистрация"

B=$(post_body "last_name=Смоков&first_name=Тест&patronymic=Скриптович&birth_date=1999-01-15&email=$EMAIL&password=abc")
run_cgi register.cgi POST "" "" "$B" >"$TMP/r"
check "успешная регистрация -> экран входа" "$TMP/r" "Location: login.cgi?registered=1"

run_cgi register.cgi POST "" "" "$B" >"$TMP/r"
check "повторный e-mail отклонён" "$TMP/r" "уже зарегистрирован"

B=$(post_body "last_name=А&first_name=Б&patronymic=В&birth_date=1999-01-15&email=x${STAMP}@test.ru&password=ab")
run_cgi register.cgi POST "" "" "$B" >"$TMP/r"
check "короткий пароль отклонён" "$TMP/r" "не менее 3 символов"

B=$(post_body "last_name=&first_name=&patronymic=&birth_date=&email=плохой-адрес&password=abc")
run_cgi register.cgi POST "" "" "$B" >"$TMP/r"
check "пустая фамилия — ошибка"   "$TMP/r" "Заполните поле «Фамилия»"
check "пустое имя — ошибка"       "$TMP/r" "Заполните поле «Имя»"
check "пустое отчество — ошибка"  "$TMP/r" "Заполните поле «Отчество»"
check "пустая дата — ошибка"      "$TMP/r" "Заполните поле «Дата рождения»"
check "некорректный e-mail — ошибка" "$TMP/r" "Некорректный адрес"

echo "=== Авторизация ==="
B=$(post_body "email=$EMAIL&password=WRONG")
run_cgi login.cgi POST "" "" "$B" >"$TMP/r"
check "неверный пароль -> ошибка" "$TMP/r" "Неверный e-mail или пароль"

B=$(post_body "email=$EMAIL&password=abc")
run_cgi login.cgi POST "" "" "$B" >"$TMP/r"
check "верный вход -> редирект на главную" "$TMP/r" "Location: index.cgi"
check "сессия в HttpOnly-cookie" "$TMP/r" "HttpOnly"
TOKEN=$(sed -n 's/^Set-Cookie: session=\([a-f0-9]*\).*/\1/p' "$TMP/r" | tr -d '\r')
if [ -z "$TOKEN" ]; then
    echo "FAILED   не удалось получить токен сессии — дальнейшие проверки невозможны"
    exit 1
fi
COOKIE="session=$TOKEN"

run_cgi index.cgi GET "" "$COOKIE" - >"$TMP/r"
check "главная страница доступна с сессией" "$TMP/r" "Главная страница"

echo "=== Каталог лекарств ==="
run_cgi medicines.cgi GET "" "$COOKIE" - >"$TMP/r"
check "выводится наименование из БД" "$TMP/r" "Парацетамол"
check "выводится описание из БД" "$TMP/r" "Анальгетик-антипиретик"
check "выводится изображение из БД" "$TMP/r" "uploads/seed_med_01.png"

echo "=== Записи (заказы): создание ==="
B=$(post_body "order_number=$ORDER_NUM&medicine_id=1&order_date=$TODAY&quantity=5")
run_cgi order_form.cgi POST "" "$COOKIE" "$B" >"$TMP/r"
check "создание записи -> редирект на список" "$TMP/r" "Location: orders.cgi?saved=1"

run_cgi orders.cgi GET "" "$COOKIE" - >"$TMP/r"
check "новая запись в списке" "$TMP/r" "$ORDER_NUM"
check "в списке есть кнопка «Редактировать»" "$TMP/r" "Редактировать"
check "в списке есть кнопка «Удалить»" "$TMP/r" "Удалить"
check "в списке есть кнопка «Добавить запись»" "$TMP/r" "Добавить запись"

OID=$(grep -- "$ORDER_NUM" "$TMP/r" | sed -n 's/.*order_form\.cgi?id=\([0-9]*\).*/\1/p' | head -1)
if [ -z "$OID" ]; then
    echo "FAILED   не удалось определить id созданной записи"
    FAIL=$((FAIL + 1))
fi

B=$(post_body "order_number=&medicine_id=&order_date=&quantity=")
run_cgi order_form.cgi POST "" "$COOKIE" "$B" >"$TMP/r"
check "пустой идентификатор — ошибка" "$TMP/r" "Заполните поле «Идентификатор»"
check "пустое наименование — ошибка"  "$TMP/r" "Выберите наименование лекарства"
check "пустая дата — ошибка"          "$TMP/r" "Заполните поле «Дата»"
check "пустое количество — ошибка"    "$TMP/r" "Заполните поле «Количество»"

B=$(post_body "order_number=$ORDER_NUM&medicine_id=1&order_date=$TODAY&quantity=3")
run_cgi order_form.cgi POST "" "$COOKIE" "$B" >"$TMP/r"
check "дубль номера заказа отклонён" "$TMP/r" "уже существует"

echo "=== Записи (заказы): редактирование ==="
run_cgi order_form.cgi GET "id=$OID" "$COOKIE" - >"$TMP/r"
check "форма редактирования подставляет значения" "$TMP/r" "value=\"$ORDER_NUM\""
check "кнопка «Сохранение» на форме" "$TMP/r" "Сохранение"
check "кнопка «К списку» на форме" "$TMP/r" "К списку"

B=$(post_body "id=$OID&order_number=$ORDER_NUM&medicine_id=2&order_date=$TODAY&quantity=7")
run_cgi order_form.cgi POST "" "$COOKIE" "$B" >"$TMP/r"
check "сохранение изменений -> редирект" "$TMP/r" "Location: orders.cgi?saved=1"

run_cgi orders.cgi GET "" "$COOKIE" - >"$TMP/r"
grep -- "$ORDER_NUM" "$TMP/r" >"$TMP/row"
check "количество обновилось" "$TMP/row" "<td>7</td>"
check "лекарство обновилось"  "$TMP/row" "Ибупрофен"

echo "=== Поиск ==="
run_cgi search.cgi GET "q=$ORDER_NUM" "$COOKIE" - >"$TMP/r"
check "кнопка «Найти» на странице" "$TMP/r" "Найти"
check "заказ находится по номеру" "$TMP/r" "Заказ $ORDER_NUM"
check "в блоке результата есть описание" "$TMP/r" "Количество: 7"

# «Ибупрофен» в percent-encoding — как отправляет браузер
run_cgi search.cgi GET "q=%D0%98%D0%B1%D1%83%D0%BF%D1%80%D0%BE%D1%84%D0%B5%D0%BD" "$COOKIE" - >"$TMP/r"
check "поиск по наименованию лекарства" "$TMP/r" "$ORDER_NUM"

run_cgi search.cgi GET "q=NO-SUCH-ORDER-42" "$COOKIE" - >"$TMP/r"
check "пустой результат — сообщение" "$TMP/r" "ничего не найдено"

echo "=== Загрузка изображений ==="
# Маленький валидный PNG (1x1), сгенерированный из base64.
printf 'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==' \
    | base64 -d >"$TMP/test.png"
BOUND="----smokeboundary$STAMP"
{
    printf -- '--%s\r\n' "$BOUND"
    printf 'Content-Disposition: form-data; name="image"; filename="smoke.png"\r\n'
    printf 'Content-Type: image/png\r\n\r\n'
    cat "$TMP/test.png"
    printf '\r\n--%s--\r\n' "$BOUND"
} >"$TMP/mp"
run_cgi upload.cgi POST "" "$COOKIE" "$TMP/mp" "multipart/form-data; boundary=$BOUND" >"$TMP/r"
check "загрузка PNG -> редирект" "$TMP/r" "Location: upload.cgi?uploaded=1"

run_cgi upload.cgi GET "" "$COOKIE" - >"$TMP/r"
check "файл в таблице загрузок" "$TMP/r" "smoke.png"
check "дата в формате ГГГГ-ММ-ДД" "$TMP/r" "<td>$TODAY</td>"

# Негатив: текстовый файл под видом PNG (расширение верное, сигнатура — нет).
{
    printf -- '--%s\r\n' "$BOUND"
    printf 'Content-Disposition: form-data; name="image"; filename="fake.png"\r\n'
    printf 'Content-Type: image/png\r\n\r\n'
    printf 'это не картинка'
    printf '\r\n--%s--\r\n' "$BOUND"
} >"$TMP/mp2"
run_cgi upload.cgi POST "" "$COOKIE" "$TMP/mp2" "multipart/form-data; boundary=$BOUND" >"$TMP/r"
check "подделка сигнатуры отклонена" "$TMP/r" "Допустимы только изображения"

# Негатив: недопустимое расширение.
{
    printf -- '--%s\r\n' "$BOUND"
    printf 'Content-Disposition: form-data; name="image"; filename="note.txt"\r\n'
    printf 'Content-Type: text/plain\r\n\r\n'
    printf 'просто текст'
    printf '\r\n--%s--\r\n' "$BOUND"
} >"$TMP/mp3"
run_cgi upload.cgi POST "" "$COOKIE" "$TMP/mp3" "multipart/form-data; boundary=$BOUND" >"$TMP/r"
check "недопустимое расширение отклонено" "$TMP/r" "Допустимы только изображения"

echo "=== Удаление записи ==="
B=$(post_body "id=$OID")
run_cgi order_delete.cgi GET "id=$OID" "$COOKIE" - >"$TMP/r"
check "удаление по GET запрещено (редирект без удаления)" "$TMP/r" "Location: orders.cgi"
run_cgi orders.cgi GET "" "$COOKIE" - >"$TMP/r"
check "запись всё ещё на месте после GET" "$TMP/r" "$ORDER_NUM"

run_cgi order_delete.cgi POST "" "$COOKIE" "$B" >"$TMP/r"
check "удаление записи -> редирект" "$TMP/r" "Location: orders.cgi?deleted=1"
run_cgi orders.cgi GET "" "$COOKIE" - >"$TMP/r"
check_absent "запись исчезла из списка" "$TMP/r" "$ORDER_NUM"

echo "=== Выход ==="
run_cgi logout.cgi GET "" "$COOKIE" - >"$TMP/r"
check "выход -> редирект на вход" "$TMP/r" "Location: login.cgi"
run_cgi orders.cgi GET "" "$COOKIE" - >"$TMP/r"
check "старая сессия недействительна" "$TMP/r" "Location: login.cgi"

echo
echo "Итого: пройдено $PASS, провалено $FAIL"
[ "$FAIL" -eq 0 ] || exit 1
exit 0
