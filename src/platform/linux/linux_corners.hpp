#pragma once
#include <dlfcn.h>
#include <algorithm>
#include <cmath>

namespace reaweb {
class LinuxCorners {
  struct Rectangle { int x, y, width, height; };
  using Create = void* (*)();
  using Union = int (*)(void*, const Rectangle*);
  using Destroy = void (*)(void*);
  using Shape = void (*)(void*, const void*, int, int);
  using Invalidate = void (*)(void*, const Rectangle*, int);
  Create create_ = reinterpret_cast<Create>(dlsym(RTLD_DEFAULT, "cairo_region_create"));
  Union unite_ = reinterpret_cast<Union>(dlsym(RTLD_DEFAULT, "cairo_region_union_rectangle"));
  Destroy destroy_ = reinterpret_cast<Destroy>(dlsym(RTLD_DEFAULT, "cairo_region_destroy"));
  Shape shape_ = reinterpret_cast<Shape>(dlsym(RTLD_DEFAULT, "gdk_window_shape_combine_region"));
  Invalidate invalidate_ = reinterpret_cast<Invalidate>(dlsym(RTLD_DEFAULT, "gdk_window_invalidate_rect"));
  void* applied_ = nullptr;
  int width_ = 0, height_ = 0, radius_ = -1;
public:
  bool supported() const { return create_ && unite_ && destroy_ && shape_ && invalidate_; }
  void reset(void* window) {
    if (window && supported() && radius_ > 0) { shape_(window, nullptr, 0, 0); invalidate_(window, nullptr, true); }
    applied_ = nullptr; radius_ = -1;
  }
  void apply(void* window, int width, int height, double radius) {
    if (!window || !supported()) return;
    const int r = std::max(0, std::min({int(std::lround(radius)), width / 2, height / 2}));
    if (window == applied_ && width == width_ && height == height_ && r == radius_) return;
    if (!r) { if (radius_ > 0 && applied_ == window) shape_(window, nullptr, 0, 0); }
    else {
      auto region = create_();
      Rectangle middle{0, r, width, std::max(0, height - 2 * r)}; unite_(region, &middle);
      for (int y = 0; y < r; ++y) {
        const double dy = r - y - .5;
        const int inset = int(std::ceil(r - std::sqrt(r * r - dy * dy) - .5));
        Rectangle row{inset, y, width - 2 * inset, 1}; unite_(region, &row);
        row.y = height - y - 1; unite_(region, &row);
      }
      shape_(window, region, 0, 0); destroy_(region);
    }
    // GDK commits shape changes during paint, including changes that only remove pixels.
    if (r || radius_ > 0) invalidate_(window, nullptr, true);
    applied_ = window; width_ = width; height_ = height; radius_ = r;
  }
};
}
