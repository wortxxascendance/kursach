#pragma once
#include <string>
#include <vector>

struct HourlyPoint {
    std::string time;   // ISO: YYYY-MM-DDTHH:MM
    double      temperature;
    double      precip; // μμ
    int         weathercode;
};

class WeatherService {
public:
    static std::string buildUrl(double lat, double lon,
        const std::string& start_date,
        const std::string& end_date);
    static std::vector<HourlyPoint> parseHourly(const std::string& jsonText);
};
