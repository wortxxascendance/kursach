#pragma once
#include <string>

class HttpClient {
public:
    // Простой GET. Возвращает тело ответа или бросает std::runtime_error.
    std::string get(const std::string& url, long timeout_sec = 20);
};
