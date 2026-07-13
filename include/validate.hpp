// Серверная валидация пользовательского ввода.
#pragma once

#include <string>

namespace apteka {

std::string trim(const std::string& s);

// Формат e-mail: непустые локальная часть и домен, ровно один '@',
// точка в домене, без пробелов и управляющих символов.
bool valid_email(const std::string& s);

// Дата в формате ГГГГ-ММ-ДД с проверкой календарной корректности.
bool valid_date(const std::string& s);

// Непустая строка из одних цифр (до 9 знаков), значение > 0.
bool valid_positive_int(const std::string& s);

} // namespace apteka
