# Информационная система аптеки

Веб-сервис для предметной области «аптека»: продажа готовых медикаментов и изготовление лекарств по рецептам. Выполнен в рамках учебно-технологической практики.

Написан на **C++17** в архитектуре **CGI + MySQL + серверный рендеринг HTML** — под shared-хостинг Beget (`http://olkhovskiy.beget.tech/UTP2026`), где нельзя держать постоянно работающий процесс: Apache запускает CGI-программу на каждый запрос. Интерфейс оформлен в стиле терминала (TUI) со шрифтом Iosevka Term.

## Возможности

- Регистрация и авторизация (сессии на HttpOnly-cookie)
- Каталог лекарств: наименование, описание, изображение — всё из БД
- Заказы: список, создание, редактирование, удаление (CRUD)
- Поиск по номеру заказа и наименованию лекарства
- Загрузка изображений (только JPG/PNG, с проверкой магических байтов)
- Разграничение прав: гостю доступны только вход и регистрация

## Технологии

| Что | Чем |
|---|---|
| Язык | C++17, только стандартная библиотека |
| База данных | MySQL / MariaDB (InnoDB, utf8mb4), схема в 3НФ — 16 таблиц |
| Доступ к БД | `libmysqlclient` (MySQL C API), только prepared statements |
| Хэширование | `libcrypto` (OpenSSL, SHA-256) с портативным фолбэком |
| Веб-сервер | Apache + CGI (на хостинге); локально — свой дев-сервер на C++/**Asio** (`tools/dev_server.cpp`, `make devserver`), запасной — `tools/dev_server.py` |
| Сборка | GNU Make (основная, используется на хостинге); CMake — альтернатива для IDE. `-std=c++17 -Wall -Wextra -O2`, без предупреждений |

Без фреймворков, ORM и внешних зависимостей — по условиям задания
(ограничение касается кода, разворачиваемого на хостинг; библиотека Asio
используется только локальным инструментом разработки и на Beget не попадает).

## Документация

| Файл | О чём |
|---|---|
| [RUN.md](RUN.md) | Как запустить: база данных, сборка, сайт в браузере (Windows/Linux/Beget) |
| [read.md](read.md) | Разбор проекта «для новичка»: как работает CGI-сервер, зачем каждый файл, как написать такой же |
| [BASICS.md](BASICS.md) | Основы с нуля: что такое CGI, MariaDB vs MySQL, Apache и Beget, базы данных, три звена, сетевые протоколы |
| [DEPLOY.md](DEPLOY.md) | Пошаговое развёртывание на хостинге Beget |
| [NOTES_FOR_REPORT.md](NOTES_FOR_REPORT.md) | Тезисы для отчёта по практике |

## Быстрый старт (Linux/Debian)

```sh
# 1. Инструменты и сервер БД
sudo apt install g++ make default-libmysqlclient-dev libssl-dev mariadb-server

# 2. База данных
sudo mysql -e "CREATE DATABASE apteka CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
               CREATE USER 'apteka'@'localhost' IDENTIFIED BY 'apteka_pass';
               GRANT ALL PRIVILEGES ON apteka.* TO 'apteka'@'localhost';"
mysql -u apteka -papteka_pass apteka < sql/schema.sql
mysql -u apteka -papteka_pass apteka < sql/seed.sql

# 3. Конфигурация (вписать реквизиты БД)
cp config/app.conf.example config/app.conf

# 4. Сборка и тесты
make                     # CGI-модули
make devserver           # локальный веб-сервер (C++/Asio)
bash tests/smoke.sh      # 51 проверка модулей напрямую
bash tests/http_smoke.sh # 25 проверок через HTTP

# 5. Открыть сайт в браузере
./dev_server 8000                    # http://127.0.0.1:8000/
```

Тестовые пользователи: `test@example.com` / `test123` и `admin@apteka.ru` / `admin123`.

Сборка под Windows (MinGW) для разработки — с указанием путей к MariaDB:

```sh
mingw32-make MYSQL_CFLAGS="-I<mariadb>/include/mysql" \
             MYSQL_LIBS="-L<mariadb>/lib -lmariadb" CRYPTO_LIBS=
```

`libmariadb.dll` кладётся рядом с `*.cgi`. Пустой `CRYPTO_LIBS` включает встроенную реализацию SHA-256 (дайджесты совпадают с libcrypto). Подробности — в [RUN.md](RUN.md).

Альтернатива для IDE (CLion, VS Code) — CMake; готовые файлы кладутся туда же, в корень проекта:

```sh
cmake -S . -B build-cmake        # на Windows добавить -DMYSQL_INCLUDE_DIR=... -DMYSQL_LIBRARY=...
cmake --build build-cmake        # соберёт и *.cgi, и dev_server
```

## Структура проекта

```
project_cpp/
├── Makefile               # сборка: make / make clean
├── config/app.conf.example
├── sql/schema.sql         # схема БД (3НФ) с комментариями-обоснованиями
├── sql/seed.sql           # тестовые данные для всех таблиц
├── include/               # заголовки библиотеки common
├── src/common/            # общий код: config, db, http, multipart,
│                          #   session, sha256, validate, html
├── src/pages/             # по одному .cpp на каждый CGI-модуль
├── public/style.css       # оформление (TUI-тема)
├── public/fonts/          # субсет Iosevka Term (woff2)
├── uploads/               # загружаемые изображения + демо-картинки
├── tests/smoke.sh         # сквозной smoke-тест CGI-модулей (51 проверка)
├── tests/http_smoke.sh    # проверки запуска через HTTP (25 проверок)
├── tools/dev_server.cpp   # локальный веб-сервер на C++/Asio (make devserver)
├── tools/dev_server.py    # он же на Python (запасной вариант)
├── third_party/asio/      # standalone Asio 1.34.2 (только для дев-сервера)
└── deploy/                # htaccess и hello.cgi для Beget
```

## Модули

Каждый экран — отдельный исполняемый файл; общий код собран в статическую библиотеку `common`.

| CGI | Назначение |
|---|---|
| `index.cgi` | Главная страница с навигацией по модулям |
| `register.cgi` | Регистрация: все поля обязательны, e-mail уникален, пароль от 3 символов |
| `login.cgi` | Авторизация по e-mail и паролю |
| `logout.cgi` | Выход: уничтожение сессии |
| `medicines.cgi` | Каталог лекарств (информация о товарах) |
| `orders.cgi` | Список записей с кнопками «Редактировать» и «Удалить» |
| `order_form.cgi` | Создание/редактирование записи, кнопки «Сохранение» и «К списку» |
| `order_delete.cgi` | Удаление записи (только POST) |
| `search.cgi` | Поиск, результаты блоками на той же странице |
| `upload.cgi` | Загрузка изображений, таблица с датой в формате ГГГГ-ММ-ДД |

Неавторизованный пользователь при обращении к любому защищённому модулю получает редирект на `login.cgi` — единый guard `require_login()` в `common`.

## Тесты

`tests/smoke.sh` запускает CGI-бинарники напрямую, как это делает веб-сервер: переменные окружения (`REQUEST_METHOD`, `QUERY_STRING`, `CONTENT_TYPE`, `CONTENT_LENGTH`, `HTTP_COOKIE`) плюс тело запроса на stdin.

Сценарий: регистрация → вход → каталог → создание записи → редактирование → поиск → загрузка изображения → удаление → выход. Негативные проверки: доступ без сессии, занятый e-mail, короткий пароль, пустые поля, файл с поддельной сигнатурой. При провале любой проверки скрипт завершается ненулевым кодом.

## Безопасность

- **SQL-инъекции** — исключены: только prepared statements (`mysql_stmt_*`), спецсимволы `LIKE` экранируются
- **XSS** — весь пользовательский вывод экранируется при вставке в HTML
- **Сессии** — HttpOnly-cookie, токен 128 бит из `/dev/urandom`, срок жизни проверяется в БД
- **Пароли** — `SHA-256(соль + пароль)`, индивидуальная соль; для продакшена следует использовать bcrypt/argon2 (здесь — учебное упрощение)
- **Загрузка файлов** — лимит 5 МБ, проверка расширения и магических байтов, имя файла генерирует сервер
