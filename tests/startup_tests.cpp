#include "platform/shared/startup.hpp"
#include "platform/shared/startup_timing.hpp"
#include "platform/shared/idle_pool.hpp"
#include <stdexcept>
#include <iostream>
using reaweb::StartupPresentation;
#define CHECK(value) do { if (!(value)) throw std::runtime_error("Check failed: " #value); } while (false)
int main() {
  try {
    struct Spare { bool ready = false, failed = false; };
    reaweb::IdlePool<Spare> pool;
    auto now = reaweb::IdlePool<Spare>::Clock::now();
    std::vector<std::weak_ptr<Spare>> spares;
    const auto create = [&] { auto value = std::make_shared<Spare>(); spares.push_back(value); return value; };
    pool.capacity(5); pool.maintain(create, now);
    CHECK(pool.diagnostics()["pending"] == 1 && !pool.take());
    pool.maintain(create, now + std::chrono::seconds(1)); CHECK(spares.size() == 1);
    for (int i = 0; i < 5; ++i) {
      spares.back().lock()->ready = true;
      pool.maintain(create, now + std::chrono::seconds(2+i));
    }
    CHECK(pool.diagnostics()["ready"] == 5 && spares.size() == 5);
    auto active = pool.take(); CHECK(active && pool.diagnostics()["ready"] == 4);
    pool.capacity(0); CHECK(pool.diagnostics()["ready"] == 0 && active->ready);
    CHECK(spares.back().expired());
    pool.capacity(1); now += std::chrono::seconds(10);
    pool.maintain(create, now); auto failed = spares.back().lock(); failed->failed = true;
    pool.maintain(create, now); CHECK(pool.diagnostics()["failed"] == 1 && pool.diagnostics()["pending"] == 0);
    pool.maintain(create, now + std::chrono::milliseconds(999)); CHECK(spares.size() == 6);
    pool.maintain(create, now + std::chrono::seconds(1)); CHECK(spares.size() == 7);
    pool.maintain(create, now + std::chrono::seconds(17)); CHECK(pool.diagnostics()["failed"] == 2);
    pool.maintain([]() -> std::shared_ptr<Spare> { throw std::runtime_error("creation failed"); }, now + std::chrono::seconds(18));
    CHECK(pool.diagnostics()["failed"] == 3);
    bool invalid = false;
    try { pool.capacity(33); } catch (const reaweb::Error&) { invalid = true; }
    CHECK(invalid);
    reaweb::StartupTiming timing;
    timing.mark(reaweb::StartupTiming::Handshake);
    const auto first_timing = timing.snapshot();
    timing.mark(reaweb::StartupTiming::Handshake);
    CHECK(timing.snapshot() == first_timing);
    CHECK(first_timing["milestones"]["runtimeHandshakeMs"] >= 0);
    CHECK(!first_timing["milestones"].contains("revealCompleteMs"));
    timing.reset("cachedReopen");
    CHECK(timing.snapshot()["milestones"].size() == 1);
    CHECK(timing.snapshot()["kind"] == "cachedReopen");
    StartupPresentation hidden;
    const auto first = hidden.navigate();
    hidden.request(false); hidden.prepared(first);
    CHECK(!hidden.present());
    hidden.request(true); CHECK(hidden.present()); CHECK(!hidden.present());

    StartupPresentation reload;
    const auto old = reload.navigate();
    const auto current = reload.navigate();
    reload.prepared(old); CHECK(!reload.present());
    reload.prepared(current); CHECK(reload.present());
    const auto hosted = reload.navigate(true);
    CHECK(reload.pending() && !reload.wanted());
    reload.request(true); reload.prepared(current); CHECK(!reload.present());
    reload.prepared(hosted); CHECK(reload.present());
    // Ordinary App navigation keeps an already presented window visible.
    reload.navigate(); CHECK(!reload.pending());

    StartupPresentation closed;
    const auto pending = closed.navigate();
    closed.cancel(); closed.prepared(pending); closed.request(true);
    CHECK(!closed.present()); CHECK(!closed.expired());

    StartupPresentation cancelled_ready;
    cancelled_ready.prepared(cancelled_ready.navigate()); cancelled_ready.cancel();
    CHECK(!cancelled_ready.present());
    StartupPresentation preload(true);
    preload.prepared(preload.navigate());
    CHECK(!preload.wanted() && !preload.present() && !preload.expired());
    preload.show(); CHECK(preload.present() && !preload.focus_requested());
    StartupPresentation early(true);
    early.navigate(); early.show(true); early.request(false);
    CHECK(!early.present());
    early.show(); CHECK(early.present() && !early.focus_requested());
    StartupPresentation cancelled_hidden(true);
    cancelled_hidden.cancel(); cancelled_hidden.show(true);
    CHECK(!cancelled_hidden.present() && !cancelled_hidden.wanted());
    std::cout << "PASS: hide/show, reload generations and close races\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
