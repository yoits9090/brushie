// Coder-core head-to-head on real CAPS band symbol streams.
// Reads the TSV dump (tag pass ctx bit) from scripts/entropy_audit.py
// (BRUSHIE_AUDIT_DUMP). Groups symbols per band, encodes/decodes each band
// with three coder cores and reports bytes + ns/symbol.
//
// All rANS variants use the L=2^16 formulation (ryg rANS_static): freqs are
// the 12-bit model probabilities scaled x16 (sum = 2^16), state in [2^16, 2^32).
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <chrono>
#include <unordered_map>
#include <algorithm>

using Clock = std::chrono::steady_clock;

struct Sym { uint32_t pass; uint32_t ctx; uint32_t bit; };

static std::vector<std::vector<Sym>> load(const char* path) {
  FILE* f = fopen(path, "r");
  std::vector<std::vector<Sym>> bands;
  std::unordered_map<std::string, int> tag2band;
  char tag[64], pass[16];
  int ctx, bit;
  while (fscanf(f, "%63s %15s %d %d", tag, pass, &ctx, &bit) == 4) {
    uint32_t pid = 0;
    if (!strcmp(pass, "sign")) pid = 1;
    else if (!strcmp(pass, "unary")) pid = 2;
    else if (!strcmp(pass, "rem")) pid = 3;
    else if (!strcmp(pass, "blk")) pid = 4;
    int b;
    auto it = tag2band.find(tag);
    if (it == tag2band.end()) { b = (int)bands.size(); tag2band[tag] = b; bands.emplace_back(); }
    else b = it->second;
    bands[b].push_back(Sym{pid, (uint32_t)ctx, (uint32_t)bit});
  }
  fclose(f);
  return bands;
}

static inline unsigned shift_for(uint32_t pass) { return pass == 2 ? 4 : 5; }

// f1 = P(bit==1) in 12-bit units; adaptation moves it toward the observed bit.
static inline uint16_t adapt(uint16_t f1, uint32_t b, unsigned shift) {
  int p = f1;
  if (b) { p += (4096 - p) >> shift; if (p > 4095) p = 4095; }
  else { p -= p >> shift; if (p < 1) p = 1; }
  return (uint16_t)p;
}

// ---------- variant 1: current 32-bit carryless binary range coder ----------
struct RangeEnc {
  std::vector<uint8_t>& out;
  uint64_t low = 0; uint32_t range = 0xFFFFFFFFu;
  uint8_t cache = 0; size_t cache_size = 1;
  explicit RangeEnc(std::vector<uint8_t>& o) : out(o) {}
  void shift_low() {
    if (low < 0xFF000000ull || low > 0xFFFFFFFFull) {
      uint8_t carry = (uint8_t)(low >> 32);
      out.push_back((uint8_t)(cache + carry));
      for (size_t i = 1; i < cache_size; ++i) out.push_back((uint8_t)(0xFF + carry));
      cache = (uint8_t)(low >> 24); cache_size = 1;
    } else ++cache_size;
    low = (low << 8) & 0xFFFFFFFFull;
  }
  void bit(uint16_t& prob, uint32_t b, unsigned shift) {
    uint32_t bound = (uint32_t)(((uint64_t)range >> 11) * prob);
    if (b == 0) { range = bound; int p = prob; p += (2048 - p) >> shift; prob = (uint16_t)(p < 2048 ? p : 2047); }
    else { low += bound; range -= bound; int p = prob; p -= p >> shift; prob = (uint16_t)(p > 0 ? p : 1); }
    while (range < (1u << 24)) { range <<= 8; shift_low(); }
  }
  void flush() { for (int i = 0; i < 5; ++i) shift_low(); }
};

struct RangeDec {
  const uint8_t* data; size_t size, pos = 0;
  uint32_t code = 0, range = 0xFFFFFFFFu;
  RangeDec(const uint8_t* d, size_t s) : data(d), size(s) { for (int i = 0; i < 5; ++i) code = (code << 8) | read(); }
  uint8_t read() { if (pos >= size) { ++pos; return 0; } return data[pos++]; }
  uint32_t bit(uint16_t& prob, unsigned shift) {
    uint32_t bound = (uint32_t)(((uint64_t)range >> 11) * prob);
    uint32_t b;
    if (code < bound) { range = bound; int p = prob; p += (2048 - p) >> shift; prob = (uint16_t)(p < 2048 ? p : 2047); b = 0; }
    else { code -= bound; range -= bound; int p = prob; p -= p >> shift; prob = (uint16_t)(p > 0 ? p : 1); b = 1; }
    while (range < (1u << 24)) { code = (code << 8) | read(); range <<= 8; }
    return b;
  }
};

// ---------- variant 2: adaptive binary rANS (12-bit probs, L=2^16) ----------
struct RansEnc {
  std::vector<uint8_t>& out;
  uint32_t x = 1u << 16;
  std::vector<uint16_t> chunks;
  explicit RansEnc(std::vector<uint8_t>& o) : out(o) {}
  void out16() { chunks.push_back((uint16_t)(x & 0xFFFF)); x >>= 16; }
  void bit(uint16_t f1, uint32_t b) {
    uint32_t f = b ? f1 : (4096u - f1);           // M = 4096 prob scale
    uint32_t c = b ? (4096u - f1) : 0u;
    while (x >= (f << 20)) out16();
    x = (x / f) * 4096u + (x % f) + c;
  }
  void flush() {
    out.push_back((uint8_t)(x >> 24)); out.push_back((uint8_t)(x >> 16));
    out.push_back((uint8_t)(x >> 8)); out.push_back((uint8_t)x);
    for (size_t i = chunks.size(); i-- > 0;) {
      out.push_back((uint8_t)(chunks[i] & 0xFF));
      out.push_back((uint8_t)(chunks[i] >> 8));
    }
  }
};

struct RansDec {
  const uint8_t* data; size_t size, pos = 0;
  uint32_t x = 0;
  RansDec(const uint8_t* d, size_t s) : data(d), size(s) { for (int i = 0; i < 4; ++i) x = (x << 8) | read(); }
  uint8_t read() { if (pos >= size) { ++pos; return 0; } return data[pos++]; }
  uint32_t bit(uint16_t f1) {
    uint32_t slot = x & 4095u;
    uint32_t b = slot >= (4096u - f1) ? 1u : 0u;
    uint32_t f = b ? f1 : (4096u - f1);
    uint32_t c = b ? (4096u - f1) : 0u;
    x = f * (x >> 12) + slot - c;
    while (x < (1u << 16)) x = (x << 16) | (uint32_t)read() | ((uint32_t)read() << 8);
    return b;
  }
};

// ---------- variant 3: static binary rANS, per-band probability tables ------
// Per (pass,ctx) a 12-bit f1 collected from the band's own counts, transmitted
// in a 4-byte header entry (pass, ctx, f1), 0xff terminator.
struct StaticRans {
  std::vector<int> f1s;                 // indexed by pass*256+ctx, -1 = unused
  std::vector<std::pair<int,int>> used; // (pass, ctx)

  void build_counts(const std::vector<Sym>& syms) {
    std::unordered_map<uint64_t, std::pair<uint64_t,uint64_t>> cnt;
    for (auto& s : syms) { auto& c = cnt[(uint64_t)s.pass * 256 + s.ctx]; if (s.bit) c.second++; else c.first++; }
    f1s.assign(65536, -1);
    used.clear();
    for (auto& kv : cnt) {
      uint32_t pass = (uint32_t)(kv.first >> 8), ctx = (uint32_t)(kv.first & 255);
      uint64_t n0 = kv.second.first, n1 = kv.second.second;
      int f1 = (int)((n1 * 4096 + (n0 + n1) / 2) / (n0 + n1));
      if (f1 < 1) f1 = 1; if (f1 > 4095) f1 = 4095;
      f1s[pass * 256 + ctx] = f1;
      used.push_back({(int)pass, (int)ctx});
    }
  }

  std::vector<uint8_t> make_header() {
    std::vector<uint8_t> h;
    for (auto& u : used) {
      h.push_back((uint8_t)u.first); h.push_back((uint8_t)u.second);
      uint16_t f1 = (uint16_t)f1s[u.first * 256 + u.second];
      h.push_back((uint8_t)(f1 & 0xff)); h.push_back((uint8_t)(f1 >> 8));
    }
    h.push_back(0xff);
    return h;
  }

  std::vector<uint8_t> encode(const std::vector<Sym>& syms) {
    build_counts(syms);
    std::vector<uint8_t> out = make_header();
    uint32_t x = 1u << 16;
    std::vector<uint16_t> chunks;
    for (auto& s : syms) {
      int f1 = f1s[s.pass * 256 + s.ctx];
      uint32_t f = s.bit ? (uint32_t)f1 : (4096u - (uint32_t)f1);
      uint32_t c = s.bit ? (4096u - (uint32_t)f1) : 0u;
      while (x >= (f << 20)) { chunks.push_back((uint16_t)(x & 0xFFFF)); x >>= 16; }
      x = (x / f) * 4096u + (x % f) + c;
    }
    out.push_back((uint8_t)(x >> 24)); out.push_back((uint8_t)(x >> 16));
    out.push_back((uint8_t)(x >> 8)); out.push_back((uint8_t)x);
    for (size_t i = chunks.size(); i-- > 0;) {
      out.push_back((uint8_t)(chunks[i] & 0xFF));
      out.push_back((uint8_t)(chunks[i] >> 8));
    }
    return out;
  }

  bool decode(const std::vector<uint8_t>& data, const std::vector<Sym>& syms, std::vector<uint32_t>& bits) {
    size_t p = 0;
    f1s.assign(65536, -1);
    while (p + 4 <= data.size()) {
      if (data[p] == 0xff) { p++; break; }
      uint32_t pass = data[p], ctx = data[p + 1];
      uint16_t f1 = (uint16_t)(data[p + 2] | ((uint16_t)data[p + 3] << 8));
      if (f1 == 0 || f1 > 4095) return false;
      f1s[pass * 256 + ctx] = f1;
      p += 4;
    }
    uint32_t x = 0;
    for (int i = 0; i < 4; ++i) { if (p >= data.size()) return false; x = (x << 8) | data[p++]; }
    bits.clear();
    for (size_t i = syms.size(); i-- > 0;) {
      const Sym& s = syms[i];
      int f1 = f1s[s.pass * 256 + s.ctx];
      uint32_t slot = x & 4095u;
      uint32_t b = slot >= (4096u - (uint32_t)f1) ? 1u : 0u;
      uint32_t f = b ? (uint32_t)f1 : (4096u - (uint32_t)f1);
      uint32_t c = b ? (4096u - (uint32_t)f1) : 0u;
      x = f * (x >> 12) + slot - c;
      while (x < (1u << 16)) {
        if (p + 1 >= data.size()) return false;
        x = (x << 16) | (uint32_t)data[p] | ((uint32_t)data[p + 1] << 8);
        p += 2;
      }
      bits.push_back(b);
    }
    std::reverse(bits.begin(), bits.end());
    return bits.size() == syms.size();
  }
};

int main(int argc, char** argv) {
  if (argc < 2) { fprintf(stderr, "usage: bench syms.tsv\n"); return 2; }
  auto bands = load(argv[1]);
  size_t total = 0;
  for (auto& b : bands) total += b.size();
  fprintf(stderr, "bands=%zu symbols=%zu\n", bands.size(), total);

  // per-band models precomputed once (not timed): causal adaptation states
  // for the adaptive variant, static tables for the static variant.
  std::vector<std::vector<uint16_t>> fused(bands.size());
  for (size_t bi = 0; bi < bands.size(); ++bi) {
    auto& band = bands[bi];
    std::vector<uint16_t> probs(256, 2048);
    fused[bi].resize(band.size());
    for (size_t i = 0; i < band.size(); ++i) {
      fused[bi][i] = probs[band[i].ctx];
      probs[band[i].ctx] = adapt(probs[band[i].ctx], band[i].bit, shift_for(band[i].pass));
    }
  }
  std::vector<std::vector<uint8_t>> pre_enc(bands.size());

  { // range (reference: current v6 coder)
    uint64_t bytes = 0;
    std::vector<std::vector<uint8_t>> encs(bands.size());
    auto t0 = Clock::now();
    for (auto& band : bands) {
      std::vector<uint8_t> out;
      RangeEnc enc(out);
      std::vector<uint16_t> probs(256, 2047);
      for (auto& s : band) enc.bit(probs[s.ctx], s.bit, shift_for(s.pass));
      enc.flush();
      bytes += out.size();
    }
    auto t1 = Clock::now();
    printf("range      enc bytes=%llu ns/sym=%.2f\n", (unsigned long long)bytes,
           std::chrono::duration<double, std::nano>(t1 - t0).count() / total);
    bool ok = true;
    auto d0 = Clock::now();
    for (auto& band : bands) {
      std::vector<uint8_t> out;
      RangeEnc enc(out);
      std::vector<uint16_t> probs(256, 2047);
      for (auto& s : band) enc.bit(probs[s.ctx], s.bit, shift_for(s.pass));
      enc.flush();
      RangeDec dec(out.data(), out.size());
      std::vector<uint16_t> probs2(256, 2047);
      for (auto& s : band) if (dec.bit(probs2[s.ctx], shift_for(s.pass)) != s.bit) ok = false;
    }
    auto d1 = Clock::now();
    printf("range      dec ok=%d ns/sym=%.2f\n", ok,
           std::chrono::duration<double, std::nano>(d1 - d0).count() / total);
  }

  { // rANS adaptive: same model, LIFO encode, causal forward decode
    uint64_t bytes = 0;
    std::vector<std::vector<uint8_t>> encs(bands.size());
    auto t0 = Clock::now();
    for (size_t bi = 0; bi < bands.size(); ++bi) {
      auto& band = bands[bi];
      std::vector<uint8_t> out;
      RansEnc enc(out);
      for (size_t i = band.size(); i-- > 0;) enc.bit(fused[bi][i], band[i].bit);
      enc.flush();
      encs[bi] = std::move(out);
      bytes += encs[bi].size();
    }
    auto t1 = Clock::now();
    printf("rans-adapt enc bytes=%llu ns/sym=%.2f (coder only)\n", (unsigned long long)bytes,
           std::chrono::duration<double, std::nano>(t1 - t0).count() / total);
    // encode INCLUDING the causal-adaptation pre-pass (integration cost)
    auto p0 = Clock::now();
    for (int rep = 0; rep < 3; ++rep) {
      for (size_t bi = 0; bi < bands.size(); ++bi) {
        auto& band = bands[bi];
        std::vector<uint16_t> probs(256, 2048);
        std::vector<uint16_t> f2(band.size());
        for (size_t i = 0; i < band.size(); ++i) {
          f2[i] = probs[band[i].ctx];
          probs[band[i].ctx] = adapt(probs[band[i].ctx], band[i].bit, shift_for(band[i].pass));
        }
        std::vector<uint8_t> out;
        RansEnc enc(out);
        for (size_t i = band.size(); i-- > 0;) enc.bit(f2[i], band[i].bit);
        enc.flush();
      }
    }
    auto p1 = Clock::now();
    printf("rans-adapt enc ns/sym=%.2f (incl pre-pass)\n",
           std::chrono::duration<double, std::nano>(p1 - p0).count() / 3 / total);
    bool ok = true;
    auto d0 = Clock::now();
    for (size_t bi = 0; bi < bands.size(); ++bi) {
      auto& band = bands[bi];
      RansDec dec(encs[bi].data(), encs[bi].size());
      std::vector<uint16_t> probs(256, 2048);
      for (size_t i = 0; i < band.size(); ++i) {
        const Sym& s = band[i];
        if (dec.bit(probs[s.ctx]) != s.bit) ok = false;
        probs[s.ctx] = adapt(probs[s.ctx], s.bit, shift_for(s.pass));
      }
    }
    auto d1 = Clock::now();
    printf("rans-adapt dec ok=%d ns/sym=%.2f\n", ok,
           std::chrono::duration<double, std::nano>(d1 - d0).count() / total);
  }

  { // static rANS per band (headers included)
    uint64_t bytes = 0, hdr = 0;
    std::vector<std::vector<uint8_t>> encs(bands.size());
    auto t0 = Clock::now();
    for (size_t bi = 0; bi < bands.size(); ++bi) {
      StaticRans t;
      encs[bi] = t.encode(bands[bi]);
      bytes += encs[bi].size();
      hdr += (size_t)t.used.size() * 4 + 1;
    }
    auto t1 = Clock::now();
    printf("rans-static enc bytes=%llu header=%llu ns/sym=%.2f\n", (unsigned long long)bytes, (unsigned long long)hdr,
           std::chrono::duration<double, std::nano>(t1 - t0).count() / total);
    bool ok = true;
    auto d0 = Clock::now();
    for (size_t bi = 0; bi < bands.size(); ++bi) {
      StaticRans t;
      std::vector<uint32_t> bits;
      if (!t.decode(encs[bi], bands[bi], bits)) { ok = false; break; }
      for (size_t i = 0; i < bits.size(); ++i) if (bits[i] != bands[bi][i].bit) { ok = false; break; }
    }
    auto d1 = Clock::now();
    printf("rans-static dec ok=%d ns/sym=%.2f\n", ok,
           std::chrono::duration<double, std::nano>(d1 - d0).count() / total);
  }
  return 0;
}
