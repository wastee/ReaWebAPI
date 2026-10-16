#include "runtime/runtime.hpp"
#include <cmath>

namespace reaweb {
void Runtime::initialize_jsfx(JsfxDiscovery discovery) {
  check_thread();
  if (jsfx_service_) return;
  jsfx_discovery_ = std::move(discovery);
  ReaWeb_ServiceCallbacks callbacks{sizeof(ReaWeb_ServiceCallbacks), REAWEB_SERVICE_ABI, this,
    [](void* context, uint64_t handle, uint64_t request, int window, const char* method, const char* payload) -> int {
      auto& runtime = *static_cast<Runtime*>(context);
      try {
        const auto result = runtime.jsfx_call(window, method, Json::parse(payload));
        if (request) return runtime.services_.complete(handle, request, result.dump().c_str(), REAWEB_OK, nullptr);
        return REAWEB_OK;
      } catch (const std::exception& error) {
        if (request) return runtime.services_.complete(handle, request, nullptr, REAWEB_SERVICE_ERROR, error.what());
        return REAWEB_SERVICE_ERROR;
      }
    }, nullptr};
  if (services_.add("jsfx", &callbacks, &jsfx_service_) != REAWEB_OK) throw Error("SERVICE_EXISTS", "Cannot register the JSFX service");
  next_jsfx_discovery_ = Clock::now();
}
void Runtime::end_jsfx_edit(Session& session) {
  if (!session.jsfx) return;
  auto& ui = *session.jsfx;
  if (ui.editing >= 0) {
    try { ui.binding->end(ui.editing); } catch (...) {}
    ui.editing = -1;
  }
  if (!ui.undo.empty()) {
    if (undo_owner_ == session.id) finish_undo();
    ui.undo.clear();
  }
}
void Runtime::release_jsfx(Session& session) {
  if (!session.jsfx) return;
  end_jsfx_edit(session);
  auto host = session.jsfx->editor.host;
  if (host) { host->on_destroy({}); host->restore(); }
  if (session.window && !session.window->closed()) session.window->set_visible(false);
  session.jsfx->initialized = false;
}
void Runtime::park_jsfx(Session& session) {
  auto& ui = *session.jsfx;
  if (!ui.editor.host) return;
  end_jsfx_edit(session);
  auto host = ui.editor.host;
  host->on_destroy({});
  session.window->reparent_host(nullptr);
  host->park();
  ui.editor = {};
  ui.presented = false; ui.initialized = false;
  ui.last_used = Clock::now();
  next_jsfx_discovery_ = {};
  ++ui.activation;
  if (session.ready) service_event(jsfx_service_, "jsfx", session.id, "visibilityChanged", {{"active", false}, {"activation", ui.activation}});
}
Json Runtime::jsfx_state(Session& session) {
  auto& ui = *session.jsfx;
  auto state = ui.binding->state();
  ui.count = state["count"]; ui.initialized = bool(ui.editor.host);
  ui.scan = 0; ui.changes = Json::array(); ui.values = Json::array();
  for (const auto& p : state["parameters"])
    ui.values.push_back({{"index", p["index"]}, {"value", p["value"]}, {"normalized", p["normalized"]}, {"formatted", p["formatted"]}});
  state["revision"] = ++ui.revision;
  state["activation"] = ui.activation;
  state["active"] = bool(ui.editor.host);
  return state;
}
void Runtime::attach_jsfx(Session& session, const JsfxEditor& editor) {
  auto& ui = *session.jsfx;
  session.window->reparent_host(editor.host->parent());
  ui.editor = editor; ++ui.activation;
  ui.presented = false; ui.last_used = Clock::now();
  if (session.ready) editor.host->set_background(session.window->background_color());
  editor.host->present(session.window->native_handle());
  std::weak_ptr<Session> weak = sessions_.at(session.id);
  editor.host->on_destroy([this, weak] {
    if (auto s = weak.lock(); s && !s->closing) {
      try { park_jsfx(*s); }
      catch (...) { release_jsfx(*s); s->closing = true; }
    }
  });
  if (session.ready) service_event(jsfx_service_, "jsfx", session.id, "visibilityChanged",
    {{"active", true}, {"activation", ui.activation}, {"state", jsfx_state(session)}});
}
Json Runtime::jsfx_call(int window, const std::string& method, const Json& payload) {
  check_thread();
  auto it = sessions_.find(window);
  if (it == sessions_.end() || !it->second->jsfx || it->second->closing || it->second->window->closed())
    throw Error("FX_UNBOUND", "The JSFX service requires a bound Hosted WebView");
  auto& session = *it->second; auto& ui = *session.jsfx;
  if (method == "getState") return jsfx_state(session);
  if (method == "present" && (!ui.editor.host || (payload.is_object() && payload.value("activation", Json()) != ui.activation))) return false;
  if (!ui.binding->editor_matches(ui.editor)) throw Error("FX_STALE", "The JSFX editor binding is no longer active");
  if (!payload.is_object() || payload.value("activation", Json()) != ui.activation)
    throw Error("FX_STALE", "The JSFX editor activation changed");
  if (!ui.initialized) throw Error("FX_NOT_READY", "Read the host state before editing parameters");
  if (method == "present") {
    ui.editor.host->set_background(session.window->background_color());
    ui.presentation_ms = std::chrono::duration<double, std::milli>(Clock::now() - ui.last_used).count();
    ui.presented = true; session.window->set_visible(true);
    if (session.window->visible()) ui.editor.host->reveal();
    return true;
  }
  if (method == "endEdit") { end_jsfx_edit(session); return true; }
  if (method != "beginEdit" && method != "setParam") throw Error("UNKNOWN_METHOD", "Unknown JSFX service method");
  if (!payload.is_object() || !payload.value("index", Json()).is_number_integer()) throw Error("INVALID_ARGUMENT", "Expected a parameter index");
  for (auto p = payload.begin(); p != payload.end(); ++p)
    if (p.key() != "activation" && p.key() != "index" && (method != "setParam" || (p.key() != "value" && p.key() != "normalized")))
      throw Error("INVALID_ARGUMENT", "Unexpected JSFX parameter field");
  if (payload["index"].get<double>() < 0 || payload["index"].get<double>() > 258)
    throw Error("INVALID_ARGUMENT", "Invalid JSFX parameter index");
  const int param = payload.at("index").get<int>();
  if (param < 0 || param >= ui.binding->parameter_count()) throw Error("INVALID_ARGUMENT", "Invalid JSFX parameter index");
  if (method == "beginEdit") {
    end_jsfx_edit(session);
    ui.undo = transaction_call(window, "ReaWeb_BeginUndo", Json::array({"JSFX Web UI parameter"})).get<std::string>();
    ui.editing = param; ui.last_edit = Clock::now();
    return true;
  }
  if (!payload.value("value", Json()).is_number() || !payload.value("normalized", Json(false)).is_boolean())
    throw Error("INVALID_ARGUMENT", "Expected a finite value and optional normalized boolean");
  const auto value = payload["value"].get<double>();
  if (!std::isfinite(value)) throw Error("INVALID_ARGUMENT", "Expected a finite parameter value");
  if (ui.editing >= 0 && ui.editing != param) throw Error("GESTURE_BUSY", "Finish the active parameter gesture first");
  const bool discrete = ui.editing < 0;
  if (discrete) ui.undo = transaction_call(window, "ReaWeb_BeginUndo", Json::array({"JSFX Web UI parameter"})).get<std::string>();
  try { ui.binding->set(param, value, payload.value("normalized", false)); }
  catch (...) { if (discrete) end_jsfx_edit(session); throw; }
  ui.last_edit = Clock::now();
  if (undo_owner_ == window) undo_deadline_ = Clock::now() + std::chrono::seconds(30);
  if (discrete) { ui.binding->end(param); end_jsfx_edit(session); }
  ui.next_poll = Clock::now();
  auto result = ui.binding->parameter(param);
  result["activation"] = ui.activation;
  for (auto& change : ui.changes) if (change["index"] == param) change = result;
  return result;
}
void Runtime::tick_jsfx() {
  if (!jsfx_service_) return;
  const auto now = Clock::now(), deadline = now + std::chrono::milliseconds(1);
  auto item = sessions_.upper_bound(jsfx_poll_cursor_);
  const auto host_revision = host_.event_revision ? host_.event_revision("fx-changed") : 0;
  for (size_t remaining = sessions_.size(); remaining; --remaining) {
    if (item == sessions_.end()) item = sessions_.begin();
    auto& s = *item++->second;
    if (!s.jsfx) continue;
    auto& ui = *s.jsfx;
    if (s.closing || s.window->closed()) { release_jsfx(s); continue; }
    try {
      ui.binding->resolve();
      if (ui.editor.host && !ui.binding->editor_matches(ui.editor)) {
        if (ui.editor.host->retain_during_switch()) { end_jsfx_edit(s); continue; }
        park_jsfx(s);
      }
      if (!ui.editor.host) {
        if (now - ui.last_used > std::chrono::seconds(60)) s.closing = true;
        continue;
      }
      if (s.ready) ui.editor.host->set_background(s.window->background_color());
      ui.editor.host->present(s.window->native_handle());
      if (ui.presented && s.window->visible()) ui.editor.host->reveal();
      if (!ui.presented && now - ui.last_used > std::chrono::seconds(15))
        throw Error("FX_NOT_READY", "JSFX page did not acknowledge its host state within 15 seconds");
      if (ui.editing >= 0 && now - ui.last_edit > std::chrono::seconds(30)) end_jsfx_edit(s);
      if (ui.host_revision != host_revision) { ui.host_revision = host_revision; ui.next_poll = now; }
      if (!s.ready || !ui.initialized || !s.window->visible() || now < ui.next_poll || Clock::now() >= deadline || s.output_pending >= 8) continue;
      jsfx_poll_cursor_ = s.id;
      if (!ui.scan) ui.count = ui.binding->parameter_count();
      for (int n = 0; ui.scan < ui.count && n < 32 && Clock::now() < deadline; ++n, ++ui.scan) {
        auto value = ui.binding->parameter(ui.scan);
        if (ui.values.size() <= static_cast<size_t>(ui.scan)) { ui.values.push_back(value); ui.changes.push_back(value); }
        else if (ui.values[ui.scan] != value) { ui.values[ui.scan] = value; ui.changes.push_back(value); }
      }
      if (ui.scan >= ui.count) {
        if (!ui.changes.empty()) service_event(jsfx_service_, "jsfx", s.id, "paramChanged", {{"parameters", ui.changes}, {"revision", ++ui.revision}, {"activation", ui.activation}});
        ui.changes = Json::array(); ui.scan = 0; ui.next_poll = now + std::chrono::milliseconds(33);
      }
    } catch (const Error& error) {
      if (error.code == "FX_STALE") { release_jsfx(s); s.closing = true; }
      else fail(s, error.what());
    } catch (const std::exception& error) { fail(s, error.what()); }
  }
  for (auto it = jsfx_blocked_.begin(); it != jsfx_blocked_.end();) {
    if (!it->second->valid()) it = jsfx_blocked_.erase(it); else ++it;
  }
  const auto trim_cache = [&] {
    std::vector<Session*> inactive;
    for (auto& entry : sessions_) if (entry.second->jsfx && !entry.second->closing && !entry.second->jsfx->editor.host) inactive.push_back(entry.second.get());
    std::sort(inactive.begin(), inactive.end(), [](const Session* a, const Session* b) { return a->jsfx->last_used > b->jsfx->last_used; });
    // Allow one handoff slot while REAPER replaces the native editor.
    if (inactive.size() == 5 && now - inactive.front()->jsfx->last_used < std::chrono::milliseconds(500)) return;
    for (size_t n = 4; n < inactive.size(); ++n) inactive[n]->closing = true;
  };
  if (!jsfx_discovery_ || (now < next_jsfx_discovery_ && !jsfx_discovery_->pending())) { trim_cache(); return; }
  next_jsfx_discovery_ = now + std::chrono::milliseconds(200);
  for (auto editor : jsfx_discovery_->scan(host_, [this](void* track, int fx) -> std::optional<JsfxEditorDiscovery::Size> {
      try {
        JsfxBinding binding(host_, track, fx);
        if (auto association = jsfx_association(resource_ / "Effects", binding.ident()))
          return JsfxEditorDiscovery::Size{association->width, association->height};
      } catch (...) {}
      return {};
    })) {
    if (sessions_.size() >= 32) break;
    try {
      auto binding = std::make_unique<JsfxBinding>(host_, editor.track, editor.fx);
      if (!binding->editor_matches(editor)) continue;
      const auto association = jsfx_association(resource_ / "Effects", binding->ident());
      if (!association) continue;
      editor.host->prepare();
      editor.host->request_size(association->width, association->height);
      Session* cached = nullptr;
      for (auto& entry : sessions_) {
        auto& candidate = *entry.second;
        if (!candidate.jsfx || candidate.closing || candidate.window->closed() || candidate.jsfx->editor.host) continue;
        auto& previous = *candidate.jsfx->binding;
        if (previous.project() == binding->project() && previous.track() == binding->track() && previous.guid() == binding->guid() &&
            candidate.entry == association->entry) { cached = &candidate; break; }
      }
      if (cached) { attach_jsfx(*cached, editor); continue; }
      auto ui = std::make_shared<Session::Jsfx>();
      ui->editor = editor; ui->binding = std::move(binding);
      open_impl(association->entry.u8string(), {}, "", "", true, false, true, false, ui);
    } catch (const std::exception& error) {
      editor.host->restore(); jsfx_blocked_[editor.host->parent()] = editor.host;
      log_(std::string("JSFX Web UI: ") + error.what());
    }
  }
  trim_cache();
}
}
