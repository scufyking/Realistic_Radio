#include "ReceptionCalculator.h"
#include "json.hpp"
#include <fstream>
#include <iostream>
#include <cstdio>
using nlohmann::json;
int main(int argc, char** argv) {
    std::ifstream f(argv[1]); json j = json::parse(f);
    StationDatabase db;
    for (auto& [k, v] : j["countries"].items()) db.countries[k] = CountryInfo{v.value("name", k), "", v.value("relative_radius", 1.0)};
    auto& cp = j["city_properties"];
    for (auto& c : j["cities"]) { City city{c["gameName"], c["realName"], c["country"], c["x"], c["z"]};
        if (cp.contains(city.gameName)) { city.relRadius = cp[city.gameName].value("relative_radius", 1.0); city.relWhitenoise = cp[city.gameName].value("relative_whitenoise", 1.0); }
        db.cities.push_back(city); }
    std::ifstream pts(argv[2]); double x, z;
    while (pts >> x >> z) {
        auto r = CalculateReception(db, x, z, 7500);
        std::cout << r.bestCountry;
        for (auto& [k, v] : r.available) printf(" %s=%.6f", k.c_str(), v.whitenoise);
        std::cout << "\n";
    }
}
