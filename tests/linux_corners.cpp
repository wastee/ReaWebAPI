#include "platform/linux/linux_corners.hpp"
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/extensions/shape.h>
#include <iostream>

#define CHECK(v) do { if (!(v)) throw std::runtime_error(#v); } while (false)
int main(int argc, char** argv) {
  if (!gtk_init_check(&argc, &argv)) return 1;
  try {
    auto host = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_decorated(GTK_WINDOW(host), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(host), 400, 300);
    gtk_widget_show_all(host);
    while (gtk_events_pending()) gtk_main_iteration();
    auto native = gtk_widget_get_window(host);
    auto display = gdk_x11_display_get_xdisplay(gdk_window_get_display(native));
    auto xid = gdk_x11_window_get_xid(native);
    reaweb::LinuxCorners corners; CHECK(corners.supported());
    for (double radius : {24., 64., 0., 12.5}) {
      corners.apply(native, 400, 300, radius);
      for (int i = 0; i < 30; ++i) { while (gtk_events_pending()) gtk_main_iteration(); g_usleep(10000); }
      gdk_display_flush(gdk_window_get_display(native));
      int count = 0, order = 0;
      auto rectangles = XShapeGetRectangles(display, xid, ShapeBounding, &count, &order);
      CHECK(rectangles && count > 0);
      bool corner = false, middle = false;
      for (int i = 0; i < count; ++i) {
        auto r = rectangles[i];
        corner |= r.x == 0 && r.y == 0;
        middle |= 200 >= r.x && 200 < r.x+r.width && 150 >= r.y && 150 < r.y+r.height;
      }
      if (radius == 24) CHECK(rectangles[0].x > 12 && rectangles[0].x < 24);
      if (radius == 64) CHECK(rectangles[0].x > 48);
      if (radius == 12.5) CHECK(rectangles[0].x > 0 && rectangles[0].x < 13);
      XFree(rectangles); CHECK(corner == (radius == 0) && middle);
    }
    corners.reset(native);
    for (int i = 0; i < 30; ++i) { while (gtk_events_pending()) gtk_main_iteration(); g_usleep(10000); }
    gdk_display_flush(gdk_window_get_display(native));
    int count = 0, order = 0; auto rectangles = XShapeGetRectangles(display, xid, ShapeBounding, &count, &order);
    CHECK(rectangles && count == 1 && rectangles[0].x == 0 && rectangles[0].y == 0); XFree(rectangles);
    gtk_widget_destroy(host);
    std::cout << "PASS: native X11 rounded shape, radius changes, zero and suspension\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
