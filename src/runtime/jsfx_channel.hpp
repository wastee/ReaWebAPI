#pragma once
#include "runtime/jsfx_webui.hpp"
#include <mutex>
#include <chrono>
#include <vector>

namespace reaweb {
// All memory addresses remain inside this native/EEL protocol, never in page APIs.
class JsfxChannelEngine {
public:
  struct Lease {
    int slot = -1;
    uint64_t token = 0, epoch = 0, response = 0;
    std::chrono::steady_clock::time_point renewal_started{};
  };
  struct Packet {
    unsigned id = 0, channels = 0, sample_rate = 0;
    uint64_t first_sample = 0, sequence = 0, dropped = 0, epoch = 0, clock_epoch = 0;
    double position = 0; int play_state = 0;
    std::vector<double> values;
  };
  struct Reply { uint64_t request = 0; int code = 0; std::vector<double> values; };
  JsfxChannelEngine(Host& host, const std::string& name, unsigned base);
  ~JsfxChannelEngine();
  JsfxChannelEngine(const JsfxChannelEngine&) = delete;
  Lease reserve();
  bool bind(Lease& lease, JsfxBinding& binding, int parameter);
  bool valid(const Lease& lease, JsfxBinding& binding, int parameter);
  // 1 renewed by the same live VM, 0 publication pending, -1 fresh binding required.
  int renew(Lease& lease, JsfxBinding& binding, int parameter);
  bool release(const Lease& lease);
  // 1 cancelled before execution, 2 cancellation signalled, 0 busy or completed.
  int cancel(const Lease& lease, uint64_t request, bool allow_running = true);
  bool subscriptions(const Lease& lease, unsigned mask);
  std::vector<double> status(const Lease& lease);
  bool receive(const Lease& lease, Packet& packet);
  bool send(const Lease& lease, uint64_t request, unsigned kind, unsigned id, const std::vector<double>& values, uint64_t owner = 0);
  bool reply(const Lease& lease, Reply& reply);
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
