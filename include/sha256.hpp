// SHA-256 и криптостойкие случайные токены.
// На целевой платформе (Linux/Beget) используется libcrypto (OpenSSL);
// при сборке без заголовков OpenSSL (локальная разработка под Windows)
// подключается встроенная портативная реализация с теми же дайджестами.
#pragma once

#include <string>

namespace apteka {

// SHA-256 от произвольной строки, результат — 64 hex-символа в нижнем регистре.
std::string sha256_hex(const std::string& data);

// n_bytes случайных байтов в hex (2*n_bytes символов).
// Источник: /dev/urandom; вне POSIX — std::random_device.
std::string random_hex(size_t n_bytes);

} // namespace apteka
