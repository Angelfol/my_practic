# Asio (standalone) 1.34.2

Заголовочная (header-only) сетевая библиотека C++.
Источник: https://github.com/chriskohlhoff/asio (тег `asio-1-34-2`),
каталог `asio/include`. Лицензия — Boost Software License 1.0
(см. `LICENSE_1_0.txt`).

Используется **только** инструментом разработки `tools/dev_server.cpp`
(локальный веб-сервер, играющий роль Apache на машине разработчика).
Сами CGI-модули, разворачиваемые на хостинг, от Asio **не зависят** —
ограничение задания (стандартная библиотека + libmysqlclient + libcrypto)
для боевого кода соблюдено.

Подключение: `-isystem third_party/asio/include -DASIO_STANDALONE`
(без Boost; на Windows дополнительно линкуется `-lws2_32 -lmswsock`).
