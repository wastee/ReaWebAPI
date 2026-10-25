#include "runtime/jsfx_enum.hpp"
#include <cstring>
#include <iostream>
#include <limits>
using namespace reaweb;
#define CHECK(x) do { if (!(x)) throw std::runtime_error(#x); } while (false)
namespace {
std::vector<std::string> labels{"Off", "Low", "High"};
int flag = 2, reads = 0;
double lo = 0, hi = 2, increment = 1;
Json parameters() {
  Json result = Json::array();
  for (int i = 0; i < 4; ++i) result.push_back({{"index", i}, {"min", lo}, {"max", hi}, {"reaweb", {{"enum", nullptr}}}});
  return result;
}
}
int main() {
  try {
    JsfxEngineApi api{};
    api.version = jsfx_engine_version;
    api.count = [](void*) { return 1; };
    api.is_enum = [](void*, int index) { CHECK(index == 0); return flag; };
    api.get = [](void*, int index, double* minimum, double* maximum, double* step) {
      CHECK(index == 0); *minimum = lo; *maximum = hi; *step = increment; return 1.;
    };
    api.display = [](void*, int index, char* text, int size, double* value) {
      CHECK(index == 0 && value); ++reads;
      const auto& label = labels.at(size_t(*value));
      std::strncpy(text, label.c_str(), size - 1); text[size - 1] = 0;
    };
    api.set = [](void*, int, double, int) { throw std::runtime_error("Unexpected parameter write"); };
    const auto read = [&] { auto result = parameters(); jsfx_enum_metadata(api, &api, result); return result; };
    auto result = read();
    CHECK(result[0]["reaweb"]["enum"] == Json::parse(R"([{"value":0,"label":"Off"},{"value":1,"label":"Low"},{"value":2,"label":"High"}])"));
    CHECK(result[1]["reaweb"]["enum"].is_null() && reads == 3);
    labels = {u8"低", "", u8"低"};
    CHECK(read()[0]["reaweb"]["enum"][2]["label"] == u8"低");
    labels = {"Only"}; hi = 0; flag = 1;
    CHECK(read()[0]["reaweb"]["enum"].size() == 1);
    auto singleton = parameters(); singleton[0]["max"] = 1;
    jsfx_enum_metadata(api, &api, singleton);
    CHECK(singleton[0]["reaweb"]["enum"].size() == 1);
    labels = {"Off", "On"}; hi = 1;
    CHECK(read()[0]["reaweb"]["enum"].size() == 2);
    flag = 0;
    CHECK(read()[0]["reaweb"]["enum"].is_null());
    flag = 1; api.version++;
    CHECK(read()[0]["reaweb"]["enum"].is_null());
    api.version--;
    auto mismatch = parameters(); mismatch.push_back(mismatch[0]);
    jsfx_enum_metadata(api, &api, mismatch); CHECK(mismatch[0]["reaweb"]["enum"].is_null());
    mismatch = parameters(); mismatch[0]["max"] = 99;
    jsfx_enum_metadata(api, &api, mismatch); CHECK(mismatch[0]["reaweb"]["enum"].is_null());
    increment = 0.1; CHECK(read()[0]["reaweb"]["enum"].is_null()); increment = 1;
    lo = -1; CHECK(read()[0]["reaweb"]["enum"].is_null()); lo = 0;
    hi = std::numeric_limits<double>::infinity(); CHECK(read()[0]["reaweb"]["enum"].is_null());
    hi = 1024; flag = 1024; CHECK(read()[0]["reaweb"]["enum"].is_null());
    hi = 1; flag = 1; labels[1] = std::string(4095, 'x');
    CHECK(read()[0]["reaweb"]["enum"].is_null());
    labels[1] = std::string(1, char(0xff)); CHECK(read()[0]["reaweb"]["enum"].is_null());
    labels[1] = "On";
    api.display = nullptr; CHECK(read()[0]["reaweb"]["enum"].is_null());
    api.count = [](void*) { return 5; };
    api.is_enum = [](void*, int) { return 1023; };
    api.get = [](void*, int, double* minimum, double* maximum, double* step) {
      *minimum = 0; *maximum = 1023; *step = 1; return 0.;
    };
    api.display = [](void*, int, char* text, int, double*) { ++reads; std::strcpy(text, "Choice"); };
    Json bounded = Json::array();
    for (int i = 0; i < 8; ++i) bounded.push_back({{"index", i}, {"min", 0}, {"max", 1023}, {"reaweb", {{"enum", nullptr}}}});
    reads = 0; jsfx_enum_metadata(api, &api, bounded);
    CHECK(reads == 4096 && bounded[3]["reaweb"]["enum"].size() == 1024 && bounded[4]["reaweb"]["enum"].is_null());
    for (auto& parameter : bounded) parameter["reaweb"]["enum"] = nullptr;
    api.display = [](void*, int, char* text, int, double*) { ++reads; std::memset(text, 'x', 1000); text[1000] = 0; };
    reads = 0; jsfx_enum_metadata(api, &api, bounded);
    CHECK(reads < 140);
    for (const auto& parameter : bounded) CHECK(parameter["reaweb"]["enum"].is_null());
    bounded[0]["name"] = std::string(950000, 'x');
    api.display = [](void*, int, char* text, int, double*) { std::memset(text, 1, 100); text[100] = 0; };
    jsfx_enum_metadata(api, &api, bounded);
    CHECK(bounded.dump().size() < host_message_limit - 8192);
    CHECK(bounded[0]["reaweb"]["enum"].is_null());
    std::cout << "JSFX engine enumeration tests passed\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
