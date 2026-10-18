#pragma once
#include "runtime/jsfx_channel.hpp"
#include "runtime/jsfx_schema.hpp"
#include "runtime/native_stream.hpp"
#include "runtime/jsfx_transfer.hpp"
#include <deque>

namespace reaweb {
// Main-thread lifetime and host identity. The worker accesses only the private bridge.
class JsfxNativeHub {
public:
  struct Result { uint64_t request = 0; int code = 0; std::vector<double> values; };
  class Node {
  public:
    ~Node();
    const JsfxNativeDeclaration declaration;
    bool refresh(JsfxBinding& binding);
    bool available() const;
    void enqueue(uint64_t request, unsigned kind, unsigned id, std::vector<double> values, uint64_t owner = 0);
    bool running(uint64_t request) const;
    int cancel(uint64_t request, bool allow_running = true);
    std::vector<Result> results();
    Json open_stream(const std::string& id, const Json& options, int window, uint64_t document, const std::string& session);
    void close_window(int window);
    Json diagnostics() const;
    std::vector<double> header() const;
    std::shared_ptr<JsfxTransfer> create_transfer(size_t total, bool upload);
    Json attach_transfer(std::shared_ptr<JsfxTransfer> transfer, int window, uint64_t document, const std::string& origin);
  private:
    friend class JsfxNativeHub;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Node(JsfxNativeDeclaration value, std::shared_ptr<JsfxChannelEngine> engine, StreamHub& streams);
    void step();
  };
  JsfxNativeHub(Host& host, StreamHub& streams);
  ~JsfxNativeHub();
  std::shared_ptr<Node> create(const JsfxNativeDeclaration& declaration, JsfxBinding& binding);
  void collect();
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
