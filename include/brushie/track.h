// brushie/track.h - env-gated per-stage timeline + counter instrumentation.
// Enabled only when BRUSHIE_TRACK=1 is set at process start (zero cost otherwise,
// beyond one function call per scope that returns immediately). Purely
// observational: it never alters the bitstream or any codec decision.
#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace brushie {
namespace track {

inline bool enabled() {
  static const bool on = []() {
    const char* e = std::getenv("BRUSHIE_TRACK");
    return e && e[0] == '1';
  }();
  return on;
}

inline std::uint64_t now_cycles() {
#if defined(__x86_64__) || defined(_M_X64)
  unsigned lo = 0, hi = 0;
  __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
  return (static_cast<std::uint64_t>(hi) << 32) | lo;
#else
  return 0;
#endif
}

struct Event {
  const char* phase = "";
  const char* name = "";
  double ns = 0.0;
  std::uint64_t cycles = 0;
  std::uint32_t tid = 0;
  std::uint64_t a = 0, b = 0, c = 0, d = 0;
  std::string kv;
};

inline std::vector<Event>& events() {
  static std::vector<Event> v;
  return v;
}
inline std::mutex& mutex() {
  static std::mutex m;
  return m;
}

inline std::uint32_t tid_hash() {
  return static_cast<std::uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

inline void emit(const char* phase, const char* name, double ns, std::uint64_t cycles,
                 std::uint64_t a = 0, std::uint64_t b = 0, std::uint64_t c = 0,
                 std::uint64_t d = 0, std::string kv = std::string()) {
  if (!enabled()) return;
  Event e;
  e.phase = phase;
  e.name = name;
  e.ns = ns;
  e.cycles = cycles;
  e.tid = tid_hash();
  e.a = a;
  e.b = b;
  e.c = c;
  e.d = d;
  e.kv = std::move(kv);
  std::lock_guard<std::mutex> lock(mutex());
  events().push_back(std::move(e));
}

struct Scope {
  const char* phase;
  const char* name;
  std::chrono::steady_clock::time_point t0;
  std::uint64_t c0;
  explicit Scope(const char* p, const char* n)
      : phase(p), name(n), t0(std::chrono::steady_clock::now()), c0(now_cycles()) {}
  ~Scope() {
    const auto t1 = std::chrono::steady_clock::now();
    const double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
    emit(phase, name, ns, now_cycles() - c0);
  }
};

// Serialize collected events as JSONL. Called once per CLI invocation.
inline std::string dump_jsonl() {
  std::lock_guard<std::mutex> lock(mutex());
  std::string out;
  out.reserve(events().size() * 160);
  char buf[512];
  for (const Event& e : events()) {
    std::snprintf(buf, sizeof(buf),
                  "{\"phase\":\"%s\",\"name\":\"%s\",\"ns\":%.1f,\"cycles\":%llu,"
                  "\"tid\":%u,\"a\":%llu,\"b\":%llu,\"c\":%llu,\"d\":%llu",
                  e.phase, e.name, e.ns,
                  static_cast<unsigned long long>(e.cycles), e.tid,
                  static_cast<unsigned long long>(e.a),
                  static_cast<unsigned long long>(e.b),
                  static_cast<unsigned long long>(e.c),
                  static_cast<unsigned long long>(e.d));
    out.append(buf);
    if (!e.kv.empty()) {
      out.push_back(',');
      out.append(e.kv);
    }
    out.append("}\n");
  }
  return out;
}

}  // namespace track
}  // namespace brushie

#define BRUSHIE_TRACK_CAT_(a, b) a##b
#define BRUSHIE_TRACK_CAT(a, b) BRUSHIE_TRACK_CAT_(a, b)
#define BRUSHIE_TRACK_SCOPE(phase, name) \
  brushie::track::Scope BRUSHIE_TRACK_CAT(brushie_track_scope_, __LINE__)(phase, name)
