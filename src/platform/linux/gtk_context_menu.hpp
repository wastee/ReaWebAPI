#pragma once
#include <webkit2/webkit2.h>
#include <functional>
#include "platform/shared/devtools.hpp"

namespace reaweb {
class GtkDockMenu {
  WebKitWebView* view_;
  GSimpleAction* action_;
  GSimpleAction* visibility_;
  GSimpleAction* mode_;
  std::function<void()> toggle_;
  std::function<void(DevToolsAction)> devtools_;
  std::function<DevToolsMenuState()> state_;
  bool docked_ = false;
public:
  GtkDockMenu(WebKitWebView* view, std::function<void()> toggle,
      std::function<void(DevToolsAction)> devtools = {}, std::function<DevToolsMenuState()> state = {}) : view_(view),
      action_(g_simple_action_new("reaweb-dock", nullptr)),
      visibility_(g_simple_action_new("reaweb-devtools", nullptr)), mode_(g_simple_action_new("reaweb-devtools-mode", nullptr)),
      toggle_(std::move(toggle)), devtools_(std::move(devtools)), state_(std::move(state)) {
    g_signal_connect(action_, "activate", G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
      auto self = static_cast<GtkDockMenu*>(data);
      if (self->toggle_) self->toggle_();
    }), this);
    g_signal_connect(visibility_, "activate", G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
      auto self = static_cast<GtkDockMenu*>(data);
      if (self->devtools_ && self->state_) self->devtools_(self->state_().shown ? DevToolsAction::Hide : DevToolsAction::Open);
    }), this);
    g_signal_connect(mode_, "activate", G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
      auto self = static_cast<GtkDockMenu*>(data);
      if (self->devtools_ && self->state_ && self->state_().mode_enabled())
        self->devtools_(self->state_().floating ? DevToolsAction::Embed : DevToolsAction::Float);
    }), this);
    g_signal_connect(view_, "context-menu", G_CALLBACK(+[](WebKitWebView*, WebKitContextMenu* menu,
        GdkEvent*, WebKitHitTestResult*, gpointer data) -> gboolean {
      auto self = static_cast<GtkDockMenu*>(data);
      // WebKitGTK can emit an empty proposed menu after DOM preventDefault().
      if (!webkit_context_menu_get_n_items(menu)) return FALSE;
      webkit_context_menu_remove_all(menu);
      if (self->toggle_) webkit_context_menu_append(menu, webkit_context_menu_item_new_from_gaction(G_ACTION(self->action_),
        self->docked_ ? "Undock from REAPER" : "Dock in REAPER", nullptr));
      if (self->devtools_ && self->state_) {
        const auto state = self->state_();
        g_simple_action_set_enabled(self->visibility_, state.available);
        g_simple_action_set_enabled(self->mode_, state.mode_enabled());
        webkit_context_menu_append(menu, webkit_context_menu_item_new_from_gaction(G_ACTION(self->visibility_),
          state.shown ? "Hide DevTools" : "Open DevTools", nullptr));
        webkit_context_menu_append(menu, webkit_context_menu_item_new_from_gaction(G_ACTION(self->mode_),
          state.floating ? "Embed DevTools" : "Float DevTools", nullptr));
      }
      return FALSE;
    }), this);
  }
  ~GtkDockMenu() {
    g_signal_handlers_disconnect_by_data(view_, this);
    g_signal_handlers_disconnect_by_data(action_, this);
    g_object_unref(action_);
    g_signal_handlers_disconnect_by_data(visibility_, this); g_object_unref(visibility_);
    g_signal_handlers_disconnect_by_data(mode_, this); g_object_unref(mode_);
  }
  void set_docked(bool docked) { docked_ = docked; }
};
}
