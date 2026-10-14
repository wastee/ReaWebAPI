#include "platform/shared/drag_regions.hpp"
#include <iostream>
using namespace reaweb;
#define CHECK(v) do { if (!(v)) throw std::runtime_error(#v); } while (false)
int main() {
  try {
    DragRegions regions;
    const auto update = [&](Json drag, Json exclude) {
      return regions.message(Json{{"__reawebDragRegions", {{"width", 800}, {"height", 600}, {"drag", drag}, {"exclude", exclude}}}}.dump());
    };
    CHECK(!regions.hit(20, 20, 800, 600));
    CHECK(update({{0, 0, 800, 40}}, {{600, 0, 800, 40}}));
    CHECK(regions.hit(20, 20, 800, 600));
    CHECK(!regions.hit(610, 20, 800, 600));
    CHECK(!regions.hit(20, 42, 800, 600));
    CHECK(!regions.hit(20, 20, 850, 600));
    CHECK(!regions.message("unrelated"));
    CHECK(regions.hit(20, 20, 800, 600));
    CHECK(update({{0, 0, 800, 40}}, {{"invalid", 0, 5, 5}}));
    CHECK(!regions.hit(20, 20, 800, 600));
    CHECK(update({{0, 0, 800, 40}}, Json::array()));
    regions.clear(); CHECK(!regions.hit(20, 20, 800, 600));
    std::cout << "PASS: drag region exclusions, layout invalidation, malformed input and navigation\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
