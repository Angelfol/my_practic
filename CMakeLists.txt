# Информационная система аптеки — альтернативная сборка через CMake.
#
# Каноническая сборка проекта — обычный Makefile (требование задания;
# на Beget собираем именно им). CMakeLists.txt добавлен для удобства
# работы в IDE (CLion, VS Code, Qt Creator) и локальной разработки.
#
# Использование (каталог сборки отдельный, чтобы не мешать Makefile):
#   cmake -S . -B build-cmake
#   cmake --build build-cmake
# Готовые *.cgi (и dev_server) кладутся в корень проекта — так же, как
# делает Makefile: тесты и config/ ищутся по относительным путям.
#
# Windows (MinGW + портативная MariaDB из zip):
#   cmake -S . -B build-cmake -G Ninja ^
#         -DMYSQL_INCLUDE_DIR="C:/путь/mariadb/include/mysql" ^
#         -DMYSQL_LIBRARY="C:/путь/mariadb/lib/libmariadb.dll"
# (OpenSSL не обязателен: без него используется встроенный SHA-256,
#  см. src/common/sha256.cpp)

cmake_minimum_required(VERSION 3.16)
project(apteka LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Тот же режим предупреждений, что и в Makefile: проект обязан
# собираться без единого warning.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra)
    # Статическая линковка рантайма C++: бинарники не зависят от версии
    # libstdc++ на хостинге и от посторонних DLL на Windows.
    add_link_options(-static-libstdc++ -static-libgcc)
endif()

# --- Клиентская библиотека MySQL/MariaDB -----------------------------------
# Стандартного FindMySQL в CMake нет — ищем вручную. Пути можно задать
# кэш-переменными MYSQL_INCLUDE_DIR и MYSQL_LIBRARY (см. шапку файла).
find_path(MYSQL_INCLUDE_DIR
    NAMES mysql.h
    PATH_SUFFIXES mysql mariadb
    DOC "Каталог с mysql.h")
find_library(MYSQL_LIBRARY
    NAMES mysqlclient mariadb libmariadb
    DOC "Библиотека libmysqlclient/libmariadb")

if(NOT MYSQL_INCLUDE_DIR OR NOT MYSQL_LIBRARY)
    message(FATAL_ERROR
        "Не найдена клиентская библиотека MySQL/MariaDB.\n"
        "Linux:   sudo apt install default-libmysqlclient-dev\n"
        "Windows: укажите пути явно, например\n"
        "  -DMYSQL_INCLUDE_DIR=<mariadb>/include/mysql\n"
        "  -DMYSQL_LIBRARY=<mariadb>/lib/libmariadb.dll")
endif()
message(STATUS "MySQL client: ${MYSQL_LIBRARY}")

# --- OpenSSL (libcrypto) — опционально --------------------------------------
# На целевой платформе (Linux/Beget) SHA-256 берётся из libcrypto.
# Если OpenSSL не найден (локальная разработка под Windows), sha256.cpp
# автоматически использует встроенную портативную реализацию.
find_package(OpenSSL QUIET)
if(OPENSSL_FOUND)
    message(STATUS "OpenSSL найден: SHA-256 из libcrypto")
else()
    message(STATUS "OpenSSL не найден: используется встроенный SHA-256")
endif()

# --- Библиотека общего кода common ------------------------------------------
file(GLOB COMMON_SOURCES CONFIGURE_DEPENDS src/common/*.cpp)
add_library(common STATIC ${COMMON_SOURCES})
target_include_directories(common PUBLIC include)
target_include_directories(common SYSTEM PRIVATE ${MYSQL_INCLUDE_DIR})
target_link_libraries(common PUBLIC ${MYSQL_LIBRARY})
if(OPENSSL_FOUND)
    target_link_libraries(common PUBLIC OpenSSL::Crypto)
endif()

# --- CGI-модули (по одному исполняемому файлу на экран) ----------------------
set(PAGES index register login logout medicines orders
          order_form order_delete search upload)

foreach(page IN LISTS PAGES)
    add_executable(${page} src/pages/${page}.cpp)
    target_link_libraries(${page} PRIVATE common)
    set_target_properties(${page} PROPERTIES
        OUTPUT_NAME "${page}.cgi"
        SUFFIX ""                                  # без .exe даже на Windows
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR})
endforeach()

# --- Дев-сервер (C++ + standalone Asio, инструмент разработки) --------------
option(BUILD_DEVSERVER "Собирать локальный веб-сервер dev_server" ON)
if(BUILD_DEVSERVER)
    add_executable(dev_server tools/dev_server.cpp)
    target_include_directories(dev_server SYSTEM PRIVATE third_party/asio/include)
    target_compile_definitions(dev_server PRIVATE ASIO_STANDALONE)
    set_target_properties(dev_server PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR})
    if(WIN32)
        target_link_libraries(dev_server PRIVATE ws2_32 mswsock)
    else()
        find_package(Threads REQUIRED)
        target_link_libraries(dev_server PRIVATE Threads::Threads)
    endif()
endif()
