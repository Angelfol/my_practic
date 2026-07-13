# Информационная система аптеки — CGI + MySQL (C++17).
# Цели: all (по умолчанию), clean.
#
# Linux (Beget): пути к MySQL берутся из mysql_config/mariadb_config,
# SHA-256 — из libcrypto (OpenSSL). Локальная сборка под Windows (MinGW):
#   make MYSQL_CFLAGS="-I<mariadb>/include/mysql" \
#        MYSQL_LIBS="-L<mariadb>/lib -lmariadb" CRYPTO_LIBS=
# (без OpenSSL подключается встроенная реализация SHA-256, см. src/common/sha256.cpp)

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
CPPFLAGS += -Iinclude

MYSQL_CFLAGS ?= $(shell mysql_config --include 2>/dev/null || mariadb_config --include 2>/dev/null || echo -I/usr/include/mysql)
MYSQL_LIBS   ?= $(shell mysql_config --libs 2>/dev/null || mariadb_config --libs 2>/dev/null || echo -lmysqlclient)
CRYPTO_LIBS  ?= -lcrypto
# Статическая линковка рантайма C++: бинарник не зависит от версии
# libstdc++ на хостинге (и от посторонних DLL при локальной сборке).
LDFLAGS ?= -static-libstdc++ -static-libgcc
LDLIBS = $(MYSQL_LIBS) $(CRYPTO_LIBS)

HEADERS    = $(wildcard include/*.hpp)
COMMON_SRC = $(wildcard src/common/*.cpp)
COMMON_OBJ = $(patsubst src/common/%.cpp,build/common/%.o,$(COMMON_SRC))

PAGES = index register login logout medicines orders order_form order_delete search upload
CGIS  = $(addsuffix .cgi,$(PAGES))

all: $(CGIS)

build/common/%.o: src/common/%.cpp $(HEADERS)
	@mkdir -p build/common
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(MYSQL_CFLAGS) -c $< -o $@

build/libcommon.a: $(COMMON_OBJ)
	ar rcs $@ $^

%.cgi: src/pages/%.cpp build/libcommon.a $(HEADERS)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(MYSQL_CFLAGS) $(LDFLAGS) $< build/libcommon.a $(LDLIBS) -o $@

clean:
	rm -rf build $(CGIS) dev_server dev_server.exe

# --- Дев-сервер (инструмент разработки, в деплой не входит) ---------------
# Локальный аналог Apache: C++17 + standalone Asio (third_party/asio).
# Сборка:  make devserver   Запуск из корня проекта:  ./dev_server [порт]
ASIO_INC ?= third_party/asio/include
ifeq ($(OS),Windows_NT)
DEVSRV_LIBS = -lws2_32 -lmswsock
DEVSRV_BIN  = dev_server.exe
else
DEVSRV_LIBS = -pthread
DEVSRV_BIN  = dev_server
endif

devserver: $(DEVSRV_BIN)

$(DEVSRV_BIN): tools/dev_server.cpp
	$(CXX) $(CXXFLAGS) -isystem $(ASIO_INC) -DASIO_STANDALONE $(LDFLAGS) $< $(DEVSRV_LIBS) -o $@

.PHONY: all clean devserver
