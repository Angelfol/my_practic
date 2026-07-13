// Разбор тела запроса multipart/form-data (загрузка файлов).
#pragma once

#include <map>
#include <string>
#include <vector>

namespace apteka {

struct FilePart {
    std::string field;        // имя поля формы
    std::string filename;     // имя файла, присланное клиентом (не доверять!)
    std::string content_type;
    std::string data;         // содержимое файла
};

struct MultipartData {
    std::map<std::string, std::string> fields;
    std::vector<FilePart> files;
};

// content_type — заголовок Content-Type запроса (из него берётся boundary),
// body — сырое тело запроса. Бросает std::runtime_error при некорректном теле.
MultipartData parse_multipart(const std::string& content_type,
                              const std::string& body);

} // namespace apteka
