#define NOMINMAX

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <iomanip>
#include <stdexcept>
#include <sstream>
#include <ctime>

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include "http_client.h"
#include "weather_service.h"

// ------- платформа и кодировки -------
#ifdef _WIN32
#include <windows.h>
// Конвертация UTF-8 -> текущая кодовая страница консоли
static std::string to_console_cp(const std::string& utf8) {
    int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return utf8;

    std::wstring w(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &w[0], wlen);

    UINT cp = GetConsoleOutputCP();
    int blen = WideCharToMultiByte(cp, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (blen <= 0) return utf8;

    std::string out(blen ? blen - 1 : 0, '\0'); // -1 без завершающего '\0'
    if (blen) WideCharToMultiByte(cp, 0, w.c_str(), -1, &out[0], blen, nullptr, nullptr);
    return out;
}
#else
static std::string to_console_cp(const std::string& s) { return s; }
#endif

// Нормализатор локального времени cross-platform
#ifdef _WIN32
#define LOCALTIME(dst, src) localtime_s((dst), (src))
#else
#define LOCALTIME(dst, src) localtime_r((src), (dst))
#endif

// ---------- Вспомогательные функции ----------
static inline std::string day_of(const std::string& iso) { return iso.size() >= 10 ? iso.substr(0, 10) : iso; }
static inline std::string hhmm_of(const std::string& iso) { return iso.size() >= 16 ? iso.substr(11, 5) : ""; }

static std::string iso_date(const std::tm& tm) {
    char buf[11];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
    return buf;
}

static void today_and_tomorrow(std::string& start, std::string& end) {
    std::time_t t = std::time(nullptr);
    std::tm lt{};
    LOCALTIME(&lt, &t);

    start = iso_date(lt);        // сегодня

    lt.tm_mday += 1;             // завтра
    std::mktime(&lt);             // нормализация
    end = iso_date(lt);
}

static const char* wmo_text(int c) {
    switch (c) {
    case 0: return "ясно"; case 1: return "малооблачно"; case 2: return "переменная облачность";
    case 3: return "пасмурно"; case 45: case 48: return "туман/изморозь";
    case 51: return "морось слабая"; case 53: return "морось"; case 55: return "морось сильная";
    case 56: case 57: return "ледяная морось"; case 61: return "дождь слабый";
    case 63: return "дождь"; case 65: return "дождь сильный"; case 66: case 67: return "ледяной дождь";
    case 71: return "снег слабый"; case 73: return "снег"; case 75: return "снег сильный";
    case 77: return "снежные зерна"; case 80: return "ливни слабые"; case 81: return "ливни";
    case 82: return "ливни сильные"; case 85: return "снегопад (слаб)"; case 86: return "снегопад (сильн)";
    case 95: return "гроза"; case 96: case 99: return "гроза с градом"; default: return "нет данных";
    }
}

static void print_table_header() {
    std::cout << std::left << std::setw(6) << "Time"
        << std::left << std::setw(24) << "Weather"
        << std::right << std::setw(8) << "T,C"
        << std::right << std::setw(10) << "Precip" << "\n";
    std::cout << std::string(6 + 24 + 8 + 10, '-') << "\n";
}
static void print_day_break(const std::string& date) {
    std::cout << "\n-- " << date << " " << std::string(50 - (int)date.size(), '-') << "\n";
    print_table_header();
}

static void print_help() {
    std::cout <<
        "Usage:\n"
        "  kursach.exe [--lat <lat>] [--lon <lon>] [--start YYYY-MM-DD] [--end YYYY-MM-DD]\n"
        "              [--file path.json] [--place \"Название\"]\n";
}

// ---------- Обратное геокодирование ----------
static std::string resolve_place(double lat, double lon, HttpClient& http) {
    try {
        std::ostringstream u;
        u.setf(std::ios::fixed); u.precision(6);
        u << "https://geocoding-api.open-meteo.com/v1/reverse?latitude=" << lat
            << "&longitude=" << lon << "&language=ru&format=json&count=10";
        const std::string body = http.get(u.str());
        json j = json::parse(body);
        if (j.contains("results") && !j["results"].empty()) {
            const auto& res = j["results"];
            int best = 0;
            for (int i = 0; i < (int)res.size(); ++i) {
                std::string fc = res[i].value("feature_code", std::string());
                if (fc == "PPLC" || fc == "PPLA" || fc == "PPLA2") { best = i; break; }
            }
            const auto& r = res[best];
            std::string name = r.value("name", std::string());
            std::string admin2 = r.value("admin2", std::string());
            std::string admin1 = r.value("admin1", std::string());
            std::string country = r.value("country", std::string());

            std::string s;
            auto add = [&](const std::string& part) {
                if (!part.empty() && s.find(part) == std::string::npos) {
                    if (!s.empty()) s += ", ";
                    s += part;
                }
                };
            add(name); add(admin2); add(admin1); add(country);
            if (!s.empty()) return s;
        }
    }
    catch (...) {}

    try {
        std::ostringstream u;
        u.setf(std::ios::fixed); u.precision(6);
        u << "https://nominatim.openstreetmap.org/reverse?format=jsonv2"
            << "&lat=" << lat << "&lon=" << lon << "&accept-language=ru";
        const std::string body = http.get(u.str());
        json j = json::parse(body);
        if (j.contains("address")) {
            const auto& a = j["address"];
            std::string city = a.value("city", std::string());
            if (city.empty()) city = a.value("town", std::string());
            if (city.empty()) city = a.value("village", std::string());
            std::string state = a.value("state", std::string());
            std::string country = a.value("country", std::string());

            std::string s;
            if (!city.empty())   s += city;
            if (!state.empty() && state != city) s += (s.empty() ? "" : ", ") + state;
            if (!country.empty()) s += (s.empty() ? "" : ", ") + country;
            if (!s.empty()) return s;
        }
    }
    catch (...) {}

    return "неизвестное место";
}

// =========================== main ===========================
int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleCP(1251);
    SetConsoleOutputCP(1251);
#endif
    try {
        // умолчания
        double lat = 55.7558, lon = 37.6173;
        std::string start, end;
        today_and_tomorrow(start, end);
        bool offline = false; std::string path;
        std::string place_override;

        // разбор аргументов
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--lat" && i + 1 < argc) lat = std::stod(argv[++i]);
            else if (a == "--lon" && i + 1 < argc) lon = std::stod(argv[++i]);
            else if (a == "--start" && i + 1 < argc) start = argv[++i];
            else if (a == "--end" && i + 1 < argc) end = argv[++i];
            else if (a == "--file" && i + 1 < argc) { offline = true; path = argv[++i]; }
            else if (a == "--place" && i + 1 < argc) { place_override = argv[++i]; }
            else if (a == "--help" || a == "-h") { print_help(); return 0; }
        }

        HttpClient http;

        // получение данных
        std::string jsonText;
        if (offline) {
            std::ifstream in(path);
            if (!in) throw std::runtime_error("не удалось открыть файл: " + path);
            jsonText.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        }
        else {
            const std::string url = WeatherService::buildUrl(lat, lon, start, end);
            jsonText = http.get(url);
        }

        // парсинг
        const auto pts = WeatherService::parseHourly(jsonText);

        // заголовок
        std::string place = !place_override.empty()
            ? place_override
            : (offline ? "офлайн-данные" : resolve_place(lat, lon, http));

        std::cout << "Weather forecast [" << start << " - " << end << "]\n";
        std::cout << "Location: " << to_console_cp(place)
            << " (" << std::fixed << std::setprecision(4) << lat << ", " << lon << ")\n";

        // вывод таблицы
        std::string cur_day;
        for (const auto& p : pts) {
            const std::string d = day_of(p.time);
            if (d != cur_day) { cur_day = d; print_day_break(cur_day); }
            std::cout << std::left << std::setw(6) << hhmm_of(p.time)
                << std::left << std::setw(24) << wmo_text(p.weathercode)
                << std::right << std::setw(8) << std::fixed << std::setprecision(1) << p.temperature
                << std::right << std::setw(10) << std::setprecision(1) << p.precip
                << "\n";
        }
        std::cout << "\nSource: Open-Meteo (hourly) + Reverse Geocoding\n";
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
