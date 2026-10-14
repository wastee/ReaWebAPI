#include "platform/shared/startup.hpp"
#include <stdexcept>
#include <iostream>
using reaweb::StartupPresentation;
#define CHECK(value) do { if (!(value)) throw std::runtime_error("Check failed: " #value); } while (false)
int main() {
  try {
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

    StartupPresentation closed;
    const auto pending = closed.navigate();
    closed.cancel(); closed.prepared(pending); closed.request(true);
    CHECK(!closed.present()); CHECK(!closed.expired());

    StartupPresentation cancelled_ready;
    cancelled_ready.prepared(cancelled_ready.navigate()); cancelled_ready.cancel();
    CHECK(!cancelled_ready.present());
    std::cout << "PASS: hide/show, reload generations and close races\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
