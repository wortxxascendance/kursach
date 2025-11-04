#include "weather_service.h"
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>

using json = nlohmann::json;

std::string WeatherService::buildUrl(double lat, double lon,
    const std::string& start_date,
    const std::string& end_date) {
    std::ostringstream oss;
    oss.setf(std::ios::fixed); oss.precision(6);
    oss << "https://api.open-meteo.com/v1/forecast"
        << "?latitude=" << lat
        << "&longitude=" << lon
        << "&start_date=" << start_date
        << "&end_date=" << end_date
        << "&hourly=temperature_2m,precipitation,weathercode"
        << "&timezone=auto";
    return oss.str();
}

std::vector<HourlyPoint> WeatherService::parseHourly(const std::string& jsonText) {
    json j = json::parse(jsonText);
    const auto& h = j.at("hourly");
    const auto& times = h.at("time");
    const auto& temps = h.at("temperature_2m");
    const auto& precs = h.at("precipitation");
    const auto& codes = h.at("weathercode");

    std::vector<HourlyPoint> out;
    out.reserve(times.size());
    for (size_t i = 0; i < times.size(); ++i) {
        HourlyPoint p{};
        p.time = times.at(i).get<std::string>();
        p.temperature = temps.at(i).get<double>();
        p.precip = precs.at(i).get<double>();
        p.weathercode = codes.at(i).get<int>();
        out.push_back(p);
    }
    return out;
}
