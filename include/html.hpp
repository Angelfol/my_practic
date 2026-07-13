// HTML-шаблоны: общие шапка и подвал, экранирование вывода.
#pragma once

#include <map>
#include <string>

#include "session.hpp"

namespace apteka {

// Экранирование для подстановки в HTML (защита от XSS).
// Экранируются & < > " '.
std::string esc(const std::string& s);

// Начало страницы: doctype, <head> со стилями, шапка с навигацией.
// user == nullptr — навигация для неавторизованного посетителя.
std::string page_begin(const std::string& title, const AuthUser* user);

// Подвал и закрывающие теги.
std::string page_end();

// Блок текста ошибки под полем формы; пустая строка, если ошибки нет.
std::string field_error(const std::map<std::string, std::string>& errors,
                        const std::string& field);

// Страница ошибки сервера (500) — используется в catch каждого модуля.
void send_error_page(const std::string& message);

} // namespace apteka
