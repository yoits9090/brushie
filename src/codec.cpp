#include "brushie/codec.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <thread>

namespace brushie {
namespace {

constexpr std::uint16_t kVersion = 2;
constexpr std::uint16_t kVersionLegacy = 1;
constexpr std::size_t kHeaderBytes = 64;
constexpr std::size_t kDirectoryBytes = 40;
constexpr std::uint32_t kMaxDimension = 16384;
constexpr std::uint32_t kMaxChunks = 4'000'000;
constexpr std::uint32_t kMaxTile = 128;

// ---------------------------------------------------------------------------
// Shared scalar helpers
// ---------------------------------------------------------------------------

static void fail(std::string* error, const char* message) {
  if (error) *error = message;
}

static std::int64_t floor_div(std::int64_t a, std::int64_t b) {
  if (b <= 0) return 0;
  if (a >= 0) return a / b;
  return -(((-a) + b - 1) / b);
}

template <class Fn>
static void parallel_for(std::size_t count, std::uint32_t requested,
                         Fn&& fn) {
  if (count == 0) return;
  const std::uint32_t workers = std::max<std::uint32_t>(
      1, std::min<std::uint32_t>(requested == 0 ? 1 : requested,
                                 static_cast<std::uint32_t>(count)));
  if (workers == 1 || count < 128) {
    for (std::size_t i = 0; i < count; ++i) fn(i);
    return;
  }
  std::atomic<std::size_t> next{0};
  std::vector<std::thread> threads;
  threads.reserve(workers);
  for (std::uint32_t t = 0; t < workers; ++t) {
    threads.emplace_back([&]() {
      for (;;) {
        const std::size_t i = next.fetch_add(1, std::memory_order_relaxed);
        if (i >= count) break;
        fn(i);
      }
    });
  }
  for (auto& t : threads) t.join();
}

// ---------------------------------------------------------------------------
// 5/3 lifting transform (shared by v1 decode and v2 encode/decode)
// ---------------------------------------------------------------------------

static void forward_line(const std::int32_t* in, std::int32_t* out,
                         std::uint32_t n) {
  const std::uint32_t low = (n + 1) / 2;
  const std::uint32_t high = n / 2;
  std::vector<std::int32_t> x(in, in + n);
  for (std::uint32_t i = 1; i < n; i += 2) {
    const std::int32_t left = x[i - 1];
    const std::int32_t right = (i + 1 < n) ? x[i + 1] : left;
    x[i] -= static_cast<std::int32_t>(floor_div(static_cast<std::int64_t>(left) + right, 2));
  }
  for (std::uint32_t i = 0; i < n; i += 2) {
    const std::int32_t left = (i > 0) ? x[i - 1] : x[i + 1 < n ? i + 1 : i];
    const std::int32_t right = (i + 1 < n) ? x[i + 1] : left;
    x[i] += static_cast<std::int32_t>(floor_div(static_cast<std::int64_t>(left) + right + 2, 4));
  }
  for (std::uint32_t i = 0; i < low; ++i) out[i] = x[i * 2];
  for (std::uint32_t i = 0; i < high; ++i) out[low + i] = x[i * 2 + 1];
}

static void inverse_line(const std::int32_t* in, std::int32_t* out,
                         std::uint32_t n) {
  const std::uint32_t low = (n + 1) / 2;
  const std::uint32_t high = n / 2;
  std::vector<std::int32_t> x(n);
  for (std::uint32_t i = 0; i < low; ++i) x[i * 2] = in[i];
  for (std::uint32_t i = 0; i < high; ++i) x[i * 2 + 1] = in[low + i];
  for (std::uint32_t i = 0; i < n; i += 2) {
    const std::int32_t left = (i > 0) ? x[i - 1] : x[i + 1 < n ? i + 1 : i];
    const std::int32_t right = (i + 1 < n) ? x[i + 1] : left;
    x[i] -= static_cast<std::int32_t>(floor_div(static_cast<std::int64_t>(left) + right + 2, 4));
  }
  for (std::uint32_t i = 1; i < n; i += 2) {
    const std::int32_t left = x[i - 1];
    const std::int32_t right = (i + 1 < n) ? x[i + 1] : left;
    x[i] += static_cast<std::int32_t>(floor_div(static_cast<std::int64_t>(left) + right, 2));
  }
  std::copy(x.begin(), x.end(), out);
}

// One pyramid level for one channel: three detail bands (H, V, D).
struct BandLevel {
  std::uint32_t w = 0, h = 0, lw = 0, lh = 0;
  std::array<std::vector<std::int32_t>, 3> detail;
};

static void extract_detail(const std::vector<std::int32_t>& packed,
                           std::uint32_t w, std::uint32_t h,
                           std::array<std::vector<std::int32_t>, 3>& out) {
  const std::uint32_t lw = (w + 1) / 2;
  const std::uint32_t lh = (h + 1) / 2;
  const std::uint32_t hw = w / 2;
  const std::uint32_t hh = h / 2;
  out[0].resize(static_cast<std::size_t>(hw) * lh);
  out[1].resize(static_cast<std::size_t>(lw) * hh);
  out[2].resize(static_cast<std::size_t>(hw) * hh);
  for (std::uint32_t y = 0; y < lh; ++y) {
    for (std::uint32_t x = 0; x < hw; ++x)
      out[0][static_cast<std::size_t>(y) * hw + x] = packed[static_cast<std::size_t>(y) * w + lw + x];
  }
  for (std::uint32_t y = 0; y < hh; ++y) {
    for (std::uint32_t x = 0; x < lw; ++x)
      out[1][static_cast<std::size_t>(y) * lw + x] = packed[static_cast<std::size_t>(lh + y) * w + x];
  }
  for (std::uint32_t y = 0; y < hh; ++y) {
    for (std::uint32_t x = 0; x < hw; ++x)
      out[2][static_cast<std::size_t>(y) * hw + x] = packed[static_cast<std::size_t>(lh + y) * w + lw + x];
  }
}

static void inverse_level(const std::vector<std::int32_t>& low,
                          const std::array<std::vector<std::int32_t>, 3>& detail,
                          std::uint32_t w, std::uint32_t h,
                          std::vector<std::int32_t>& output,
                          std::uint32_t threads) {
  const std::uint32_t lw = (w + 1) / 2;
  const std::uint32_t lh = (h + 1) / 2;
  const std::uint32_t hw = w / 2;
  const std::uint32_t hh = h / 2;
  std::vector<std::int32_t> packed(static_cast<std::size_t>(w) * h, 0);
  for (std::uint32_t y = 0; y < lh; ++y) {
    for (std::uint32_t x = 0; x < lw; ++x)
      packed[static_cast<std::size_t>(y) * w + x] = low[static_cast<std::size_t>(y) * lw + x];
    for (std::uint32_t x = 0; x < hw; ++x)
      packed[static_cast<std::size_t>(y) * w + lw + x] = detail[0][static_cast<std::size_t>(y) * hw + x];
  }
  for (std::uint32_t y = 0; y < hh; ++y) {
    for (std::uint32_t x = 0; x < lw; ++x)
      packed[static_cast<std::size_t>(lh + y) * w + x] = detail[1][static_cast<std::size_t>(y) * lw + x];
    for (std::uint32_t x = 0; x < hw; ++x)
      packed[static_cast<std::size_t>(lh + y) * w + lw + x] = detail[2][static_cast<std::size_t>(y) * hw + x];
  }
  // The forward transform applies rows then columns, so inversion must undo
  // columns first and rows second.
  parallel_for(w, threads, [&](std::size_t xx) {
    std::vector<std::int32_t> line(h), restored(h);
    for (std::uint32_t y = 0; y < h; ++y) line[y] = packed[y * w + xx];
    inverse_line(line.data(), restored.data(), h);
    for (std::uint32_t y = 0; y < h; ++y) packed[y * w + xx] = restored[y];
  });
  output.resize(static_cast<std::size_t>(w) * h);
  parallel_for(h, threads, [&](std::size_t yy) {
    inverse_line(packed.data() + yy * w, output.data() + yy * w, w);
  });
}

static void input_planes(const ImageView& image,
                         std::array<std::vector<std::int32_t>, 4>& planes,
                         std::uint32_t threads) {
  const unsigned channels = image.channels == 4 ? 4u : 3u;
  const std::size_t stride = image.stride ? image.stride : static_cast<std::size_t>(image.width) * channels;
  const std::size_t count = static_cast<std::size_t>(image.width) * image.height;
  for (unsigned c = 0; c < 4; ++c) planes[c].resize(count);
  parallel_for(image.height, threads, [&](std::size_t yy) {
    const std::uint8_t* row = image.rgb + yy * stride;
    for (std::uint32_t x = 0; x < image.width; ++x) {
      const int r = row[x * channels + 0];
      const int g = row[x * channels + 1];
      const int b = row[x * channels + 2];
      const int co = r - b;
      const int t = b + static_cast<int>(floor_div(co, 2));
      const int cg = g - t;
      const int y = t + static_cast<int>(floor_div(cg, 2));
      const std::size_t i = yy * image.width + x;
      planes[0][i] = y;
      planes[1][i] = co;
      planes[2][i] = cg;
      planes[3][i] = channels == 4 ? row[x * channels + 3] : 0;
    }
  });
}

static std::uint32_t fnv1a(const std::uint8_t* data, std::size_t size) {
  std::uint32_t h = 2166136261u;
  for (std::size_t i = 0; i < size; ++i) {
    h ^= data[i];
    h *= 16777619u;
  }
  return h;
}

static std::uint8_t clamp_u8(std::int64_t value) {
  return static_cast<std::uint8_t>(std::max<std::int64_t>(0, std::min<std::int64_t>(255, value)));
}

static void output_rgb(const std::array<std::vector<std::int32_t>, 4>& planes,
                       std::uint32_t w, std::uint32_t h,
                       std::uint32_t out_w, std::uint32_t out_h,
                       std::vector<std::uint8_t>& rgb, unsigned channels,
                       std::uint32_t threads) {
  const unsigned bpp = channels == 4 ? 4u : 3u;
  rgb.resize(static_cast<std::size_t>(out_w) * out_h * bpp);
  if (w == out_w && h == out_h) {
    parallel_for(out_h, threads, [&](std::size_t yy) {
      for (std::uint32_t xx = 0; xx < out_w; ++xx) {
        const std::int64_t yv = planes[0][yy * w + xx];
        const std::int64_t co = planes[1][yy * w + xx];
        const std::int64_t cg = planes[2][yy * w + xx];
        const std::int64_t t = yv - floor_div(cg, 2);
        const std::int64_t g = cg + t;
        const std::int64_t b = t - floor_div(co, 2);
        const std::int64_t r = co + b;
        const std::size_t i = (yy * out_w + xx) * bpp;
        rgb[i + 0] = clamp_u8(r);
        rgb[i + 1] = clamp_u8(g);
        rgb[i + 2] = clamp_u8(b);
        if (channels == 4)
          rgb[i + 3] = clamp_u8(planes[3][yy * w + xx]);
      }
    });
    return;
  }
  parallel_for(out_h, threads, [&](std::size_t yy) {
    const double sy = out_h == 1 ? 0.0 : static_cast<double>(yy) * (h - 1) / (out_h - 1);
    const std::uint32_t y0 = static_cast<std::uint32_t>(sy);
    const std::uint32_t y1 = std::min(h - 1, y0 + 1);
    const double fy = sy - y0;
    for (std::uint32_t xx = 0; xx < out_w; ++xx) {
      const double sx = out_w == 1 ? 0.0 : static_cast<double>(xx) * (w - 1) / (out_w - 1);
      const std::uint32_t x0 = static_cast<std::uint32_t>(sx);
      const std::uint32_t x1 = std::min(w - 1, x0 + 1);
      const double fx = sx - x0;
      std::int32_t c[3] = {0, 0, 0};
      std::int32_t ca = 0;
      for (int k = 0; k < 3; ++k) {
        const auto& p = planes[k];
        const double a = p[static_cast<std::size_t>(y0) * w + x0] * (1.0 - fx) +
                         p[static_cast<std::size_t>(y0) * w + x1] * fx;
        const double b = p[static_cast<std::size_t>(y1) * w + x0] * (1.0 - fx) +
                         p[static_cast<std::size_t>(y1) * w + x1] * fx;
        c[k] = static_cast<std::int32_t>(std::llround(a * (1.0 - fy) + b * fy));
      }
      if (channels == 4) {
        const auto& p = planes[3];
        const double a = p[static_cast<std::size_t>(y0) * w + x0] * (1.0 - fx) +
                         p[static_cast<std::size_t>(y0) * w + x1] * fx;
        const double b = p[static_cast<std::size_t>(y1) * w + x0] * (1.0 - fx) +
                         p[static_cast<std::size_t>(y1) * w + x1] * fx;
        ca = static_cast<std::int32_t>(std::llround(a * (1.0 - fy) + b * fy));
      }
      const std::int64_t yv = c[0], co = c[1], cg = c[2];
      const std::int64_t t = yv - floor_div(cg, 2);
      const std::int64_t g = cg + t;
      const std::int64_t b = t - floor_div(co, 2);
      const std::int64_t r = co + b;
      const std::size_t i = (yy * out_w + xx) * bpp;
      rgb[i + 0] = clamp_u8(r);
      rgb[i + 1] = clamp_u8(g);
      rgb[i + 2] = clamp_u8(b);
      if (channels == 4) rgb[i + 3] = clamp_u8(ca);
    }
  });
}

static std::uint32_t safe_base_target(std::uint32_t w, std::uint32_t h) {
  const std::uint32_t shortest = std::min(w, h);
  std::uint32_t target = 32;
  while (target * 2 <= shortest && target < 64) target *= 2;
  return target;
}

static std::uint8_t quality_for_target_bytes(std::uint32_t w, std::uint32_t h,
                                             std::uint64_t target,
                                             std::uint8_t fallback) {
  if (target == 0) return fallback;
  const double ratio = static_cast<double>(target) /
                       static_cast<double>(std::uint64_t{3} * w * h);
  // Coarse monotone operating points calibrated from the v2 Kodak/DIV2K
  // sweep. This intentionally avoids trial encodes and therefore reports the
  // actual byte count instead of claiming exact rate control.
  if (ratio >= 0.40) return 100;
  if (ratio >= 0.12) return 90;
  if (ratio >= 0.085) return 82;
  if (ratio >= 0.070) return 75;
  if (ratio >= 0.055) return 60;
  if (ratio >= 0.042) return 45;
  if (ratio >= 0.032) return 20;
  if (ratio >= 0.026) return 10;
  return 5;
}

static std::uint8_t quality_for_target_lpips(double target) {
  if (!(target > 0.0)) return 100;
  if (target <= 0.03) return 90;
  if (target <= 0.06) return 82;
  if (target <= 0.10) return 70;
  return 50;
}

static std::uint32_t choose_tile(std::uint32_t w, std::uint32_t h,
                                 const EncodeOptions& options) {
  if (!options.adaptive_tile) return options.tile_size;
  const std::uint64_t raw = std::uint64_t{3} * w * h;
  if (options.target_bytes != 0 && options.target_bytes < raw / 10) return 128;
  if (std::min(w, h) >= 2048 ||
      (options.target_bytes != 0 && options.target_bytes < raw / 3)) return 64;
  return 32;
}

// ---------------------------------------------------------------------------
// v1 stream support (modes 0..2 tile entropy coding)
// ---------------------------------------------------------------------------

struct LegacyLevel {
  std::uint32_t w = 0, h = 0, lw = 0, lh = 0;
  std::array<std::array<std::vector<std::int32_t>, 3>, 3> detail;
};

static std::uint64_t unzigzag(std::uint64_t value) {
  return static_cast<std::uint64_t>(value >> 1) ^ -static_cast<std::uint64_t>(value & 1);
}

static bool decode_uvar(const std::uint8_t*& p, const std::uint8_t* end,
                        std::uint64_t& value) {
  value = 0;
  for (unsigned shift = 0; shift < 64; shift += 7) {
    if (p >= end) return false;
    const std::uint8_t b = *p++;
    if (shift == 63 && (b & 0x7e)) return false;
    value |= static_cast<std::uint64_t>(b & 0x7f) << shift;
    if (!(b & 0x80)) return true;
  }
  return false;
}

static bool decode_tile(const std::uint8_t* data, std::size_t size,
                        std::uint32_t count, std::uint16_t step,
                        std::uint16_t mode,
                        std::int32_t* destination, std::uint32_t stride,
                        std::uint32_t tw, std::uint32_t th,
                        std::string* error) {
  if (mode == 2) {
    if (size < 1 || data[0] == 0 || data[0] > 32) {
      fail(error, "invalid bit-packed coefficient width");
      return false;
    }
    const unsigned bits = data[0];
    const std::size_t mask_bytes = (count + 7) / 8;
    if (size < 1 + mask_bytes) {
      fail(error, "truncated bit-packed significance mask");
      return false;
    }
    std::size_t nonzero = 0;
    for (std::uint32_t i = 0; i < count; ++i)
      nonzero += (1u << (i & 7)) & data[1 + i / 8] ? 1 : 0;
    const std::size_t value_bytes = (nonzero * bits + 7) / 8;
    if (1 + mask_bytes + value_bytes != size) {
      fail(error, "invalid bit-packed coefficient size");
      return false;
    }
    std::size_t bit = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
      if (!((1u << (i & 7)) & data[1 + i / 8])) continue;
      std::uint64_t raw = 0;
      for (unsigned b = 0; b < bits; ++b)
        if (data[1 + mask_bytes + (bit + b) / 8] & (1u << ((bit + b) & 7))) raw |= 1ull << b;
      bit += bits;
      std::int64_t q = static_cast<std::int64_t>(raw);
      if ((bits == 32 && (raw & 0x80000000ull)) ||
          (bits < 32 && (raw & (1ull << (bits - 1)))))
        q -= static_cast<std::int64_t>(1ull << bits);
      const std::int64_t value = q * step;
      if (value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max()) {
        fail(error, "bit-packed coefficient overflow");
        return false;
      }
      const std::uint32_t yy = i / tw;
      const std::uint32_t xx = i - yy * tw;
      if (yy >= th) { fail(error, "bit-packed tile bounds exceeded"); return false; }
      destination[static_cast<std::size_t>(yy) * stride + xx] = static_cast<std::int32_t>(value);
    }
    return true;
  }
  const std::uint8_t* p = data;
  const std::uint8_t* end = data + size;
  std::uint32_t pos = 0;
  std::int32_t previous = 0;
  while (pos < count) {
    std::uint64_t run = 0;
    if (!decode_uvar(p, end, run) || run > count - pos) {
      fail(error, "malformed coefficient zero run");
      return false;
    }
    pos += static_cast<std::uint32_t>(run);
    if (pos == count) break;
    std::uint64_t code = 0;
    if (!decode_uvar(p, end, code) || code == 0) {
      fail(error, "malformed coefficient value");
      return false;
    }
    const std::int64_t symbol = static_cast<std::int64_t>(unzigzag(code - 1));
    const std::int64_t q = mode == 1 ? static_cast<std::int64_t>(previous) + symbol : symbol;
    const std::int64_t value = q * step;
    if (value < std::numeric_limits<std::int32_t>::min() ||
        value > std::numeric_limits<std::int32_t>::max()) {
      fail(error, "coefficient overflow");
      return false;
    }
    const std::uint32_t yy = pos / tw;
    const std::uint32_t xx = pos - yy * tw;
    if (yy >= th) {
      fail(error, "coefficient tile bounds exceeded");
      return false;
    }
    destination[static_cast<std::size_t>(yy) * stride + xx] = static_cast<std::int32_t>(value);
    previous = static_cast<std::int32_t>(q);
    ++pos;
  }
  return pos == count;
}

// ---------------------------------------------------------------------------
// v2: 32-bit carryless binary range coder with 11-bit adaptive probabilities.
// ---------------------------------------------------------------------------

class RangeEncoder {
 public:
  explicit RangeEncoder(std::vector<std::uint8_t>& out) : out_(out) {}

  void encode_bit(std::uint16_t& prob, std::uint32_t bit) {
    const std::uint32_t bound =
        static_cast<std::uint32_t>((static_cast<std::uint64_t>(range_) >> 11) * prob);
    if (bit == 0) {
      range_ = bound;
      int p = prob;
      p += (2048 - p) >> 5;
      prob = static_cast<std::uint16_t>(p < 2048 ? p : 2047);
    } else {
      low_ += bound;
      range_ -= bound;
      int p = prob;
      p -= p >> 5;
      prob = static_cast<std::uint16_t>(p > 0 ? p : 1);
    }
    while (range_ < (1u << 24)) {
      range_ <<= 8;
      shift_low();
    }
  }

  void flush() {
    for (int i = 0; i < 5; ++i) shift_low();
  }

 private:
  std::vector<std::uint8_t>& out_;
  std::uint64_t low_ = 0;
  std::uint32_t range_ = 0xFFFFFFFFu;
  std::uint8_t cache_ = 0;
  std::size_t cache_size_ = 1;

  void shift_low() {
    if (low_ < 0xFF000000ull || low_ > 0xFFFFFFFFull) {
      const std::uint8_t carry = static_cast<std::uint8_t>(low_ >> 32);
      out_.push_back(static_cast<std::uint8_t>(cache_ + carry));
      for (std::size_t i = 1; i < cache_size_; ++i)
        out_.push_back(static_cast<std::uint8_t>(0xFF + carry));
      cache_ = static_cast<std::uint8_t>(low_ >> 24);
      cache_size_ = 1;
    } else {
      ++cache_size_;
    }
    low_ = (low_ << 8) & 0xFFFFFFFFull;
  }
};

class RangeDecoder {
 public:
  RangeDecoder(const std::uint8_t* data, std::size_t size)
      : data_(data), size_(size) {
    for (int i = 0; i < 5; ++i) code_ = (code_ << 8) | read_byte();
  }

  std::uint32_t decode_bit(std::uint16_t& prob) {
    const std::uint32_t bound =
        static_cast<std::uint32_t>((static_cast<std::uint64_t>(range_) >> 11) * prob);
    std::uint32_t bit;
    if (code_ < bound) {
      range_ = bound;
      int p = prob;
      p += (2048 - p) >> 5;
      prob = static_cast<std::uint16_t>(p < 2048 ? p : 2047);
      bit = 0;
    } else {
      code_ -= bound;
      range_ -= bound;
      int p = prob;
      p -= p >> 5;
      prob = static_cast<std::uint16_t>(p > 0 ? p : 1);
      bit = 1;
    }
    while (range_ < (1u << 24)) {
      code_ = (code_ << 8) | read_byte();
      range_ <<= 8;
    }
    return bit;
  }

  std::size_t position() const { return pos_; }
  bool truncated() const { return pos_ > size_; }

 private:
  const std::uint8_t* data_;
  std::size_t size_;
  std::size_t pos_ = 0;
  std::uint32_t code_ = 0;
  std::uint32_t range_ = 0xFFFFFFFFu;

  std::uint8_t read_byte() {
    if (pos_ >= size_) {
      ++pos_;
      return 0;
    }
    return data_[pos_++];
  }
};

// ---------------------------------------------------------------------------
// v2: context-adaptive band entropy coding
//
// Symbols per coefficient (raster scan): a significance flag with an 8-state
// causal-neighbour context, a sign bit, then a Golomb-Rice magnitude where the
// unary quotient bits share per-position contexts and the Rice remainder is
// coded with a single adaptive context. The Rice parameter k is written as the
// first payload byte and re-adapted every 64 magnitudes on both sides. The
// base LL band is median-predicted (JPEG-LS style) before coding.
// ---------------------------------------------------------------------------

constexpr unsigned kCtxSig = 8;
constexpr unsigned kCtxSign = 4;
constexpr unsigned kCtxUnary = 14;
constexpr unsigned kCtxRem = 1;
constexpr unsigned kNumCtx = kCtxSig + kCtxSign + kCtxUnary + kCtxRem;

struct BandProbs {
  std::array<std::uint16_t, kNumCtx> p;
  // Initialized to 2047, not 2048: with an 11-bit model, prob == 2048 would
  // let bound == range for range values that are multiples of 2048 and drive
  // the range to zero in the renorm loop. 2047 keeps bound < range always.
  BandProbs() { p.fill(2047); }
};

static std::int32_t median_predict(std::int32_t a, std::int32_t b,
                                   std::int32_t c) {
  // a = left, b = above, c = above-left (JPEG-LS LOCO-I predictor).
  if (c >= std::max(a, b)) return std::min(a, b);
  if (c <= std::min(a, b)) return std::max(a, b);
  return a + b - c;
}

// Quantization steps calibrated from the frozen Kodak/DIV2K sweep. The
// coarsest detail level gets the largest step (few coefficients, lowest
// perceptual weight per bit) and the finest level the smallest; this matches
// the natural energy distribution of the 5/3 pyramid so the RD curve stays
// smooth. The diagonal band is penalized slightly and chroma is penalized 2x
// (it is also subsampled at lossy operating points).
static std::uint16_t quant_step(std::uint8_t quality,
                                std::uint32_t level_from_finest,
                                std::uint32_t num_levels, std::uint8_t band,
                                std::uint8_t channel) {
  const std::uint32_t loss = 100u - std::min<std::uint8_t>(quality, 100);
  if (loss == 0) return 1;
  double root = 1.0 + static_cast<double>(loss) / 6.0;
  // level_from_finest: 0 = finest detail level, num_levels-1 = coarsest.
  // Steps grow 2^(1.25*coarseness), saturating after three levels so the
  // table stays sane for deep pyramids on large images: the sparse coarse
  // detail is quantized hardest while the fine levels stay cheap per
  // coefficient. Calibrated on the frozen Kodak sweep at equal MS-SSIM gates
  // (~25% byte reduction vs the previous 2^(coarseness/2) table).
  const std::uint32_t coarseness = num_levels - 1 - level_from_finest;
  const double weight = std::pow(2.0, 1.25 * std::min<double>(coarseness, 3.0));
  double step = root * weight;
  if (band == 3) step *= 1.5;
  if (channel != 0) step *= 2.0;
  return static_cast<std::uint16_t>(
      std::min<double>(65535.0, std::max<double>(1.0, step)));
}

static std::uint16_t base_quant_step(std::uint8_t quality,
                                     std::uint8_t channel) {
  const std::uint32_t loss = 100u - std::min<std::uint8_t>(quality, 100);
  if (loss == 0) return 1;
  // The base LL is preserved with a finer step (0.5x root): low-frequency
  // structure dominates the SSIM-family gates and is cheap to code.
  double step = 0.5 * (1.0 + static_cast<double>(loss) / 6.0);
  if (channel != 0) step *= 2.0;
  return static_cast<std::uint16_t>(
      std::min<double>(65535.0, std::max<double>(1.0, step)));
}

static void quantize_band(std::vector<std::int32_t>& band, std::uint16_t step,
                          std::uint64_t& nonzero, std::uint64_t& abs_sum) {
  nonzero = 0;
  abs_sum = 0;
  if (step == 1) {
    for (const std::int32_t v : band) {
      if (v != 0) {
        ++nonzero;
        abs_sum += static_cast<std::uint64_t>(
            v < 0 ? -static_cast<std::int64_t>(v) : v);
      }
    }
    return;
  }
  for (std::int32_t& v : band) {
    // Midtread (round-to-nearest) quantization with plain q*step
    // reconstruction. A dead-zone quantizer was measurably worse at equal
    // byte counts on the frozen Kodak sweep because its reconstruction bias
    // penalizes the SSIM-family metrics used for rate control.
    const std::int32_t q = static_cast<std::int32_t>(std::llround(static_cast<double>(v) / step));
    v = q;
    if (q != 0) {
      ++nonzero;
      abs_sum += static_cast<std::uint64_t>(
          q < 0 ? -static_cast<std::int64_t>(q) : q);
    }
  }
}

static unsigned sig_context(const std::int32_t* band, std::uint32_t stride,
                            std::uint32_t x, std::uint32_t y) {
  unsigned ctx = 0;
  if (x > 0 && band[static_cast<std::size_t>(y) * stride + x - 1] != 0) ctx += 1;
  if (y > 0 && band[static_cast<std::size_t>(y - 1) * stride + x] != 0) ctx += 2;
  if (x > 0 && y > 0 && band[static_cast<std::size_t>(y - 1) * stride + x - 1] != 0) ctx += 4;
  return ctx;
}


static unsigned sign_context(const std::int32_t* band, std::uint32_t stride,
                             std::uint32_t x, std::uint32_t y) {
  unsigned ctx = 0;
  const std::int32_t left = x > 0 ? band[static_cast<std::size_t>(y) * stride + x - 1] : 0;
  const std::int32_t above = y > 0 ? band[static_cast<std::size_t>(y - 1) * stride + x] : 0;
  if (left < 0) ctx += 1;
  if (above < 0) ctx += 2;
  return ctx;
}
static void encode_band_arith(const std::int32_t* band, std::uint32_t w,
                              std::uint32_t h, bool use_prediction,
                              std::uint64_t nonzero,
                              std::uint64_t abs_sum,
                              std::vector<std::uint8_t>& out) {
  int k0 = 0;
  if (nonzero != 0) {
    const std::uint64_t v = abs_sum / nonzero;
    while (k0 < 12 && (std::uint64_t{1} << (k0 + 1)) <= v) ++k0;
  }
  out.push_back(static_cast<std::uint8_t>(k0));
  RangeEncoder enc(out);
  BandProbs probs;
  int k = k0;
  std::uint64_t mag_sum = 0;
  unsigned mag_count = 0;
  for (std::uint32_t y = 0; y < h; ++y) {
    for (std::uint32_t x = 0; x < w; ++x) {
      std::int32_t q = band[static_cast<std::size_t>(y) * w + x];
      if (use_prediction) {
        const std::int32_t a = x > 0 ? band[static_cast<std::size_t>(y) * w + x - 1] : 0;
        const std::int32_t b = y > 0 ? band[static_cast<std::size_t>(y - 1) * w + x] : 0;
        const std::int32_t c = (x > 0 && y > 0) ? band[static_cast<std::size_t>(y - 1) * w + x - 1] : 0;
        const std::int32_t p = (y == 0) ? a : (x == 0 ? b : median_predict(a, b, c));
        q -= p;
      }
      const unsigned ctx = sig_context(band, w, x, y);
      const std::uint32_t s = (q != 0) ? 1u : 0u;
      enc.encode_bit(probs.p[kCtxSig + ctx], s);
      if (s) {
        const std::uint32_t sign = q < 0 ? 1u : 0u;
        const unsigned sign_ctx = sign_context(band, w, x, y);
        enc.encode_bit(probs.p[kCtxSig + sign_ctx], sign);
        const std::uint32_t m = static_cast<std::uint32_t>(q < 0 ? -q : q) - 1u;
        const std::uint32_t qq = m >> k;
        const std::uint32_t rr = m & ((1u << k) - 1u);
        for (std::uint32_t i = 0; i < qq; ++i)
          enc.encode_bit(probs.p[kCtxSig + kCtxSign + std::min<std::uint32_t>(i, kCtxUnary - 1)], 0);
        enc.encode_bit(probs.p[kCtxSig + kCtxSign + std::min<std::uint32_t>(qq, kCtxUnary - 1)], 1);
        for (int i = 0; i < k; ++i)
          enc.encode_bit(probs.p[kCtxSig + kCtxSign + kCtxUnary], (rr >> i) & 1u);
        mag_sum += m;
        ++mag_count;
        if (mag_count == 64) {
          const std::uint64_t v = mag_sum / 64;
          int nk = 0;
          while (nk < 12 && (std::uint64_t{1} << (nk + 1)) <= v) ++nk;
          k = nk;
          mag_sum = 0;
          mag_count = 0;
        }
      }
    }
  }
  enc.flush();
}

static bool decode_band_arith(const std::uint8_t* data, std::size_t size,
                              std::uint32_t count, std::uint16_t step,
                              bool use_prediction, std::int32_t* dest,
                              std::uint32_t stride, std::uint32_t tw,
                              std::uint32_t th, std::string* error) {
  if (size < 1) {
    fail(error, "empty arithmetic band payload");
    return false;
  }
  const int k0 = data[0];
  if (k0 < 0 || k0 > 12) {
    fail(error, "invalid arithmetic Rice parameter");
    return false;
  }
  RangeDecoder dec(data + 1, size - 1);
  BandProbs probs;
  int k = k0;
  std::uint64_t mag_sum = 0;
  unsigned mag_count = 0;
  for (std::uint32_t y = 0; y < th; ++y) {
    for (std::uint32_t x = 0; x < tw; ++x) {
      const unsigned ctx = sig_context(dest, stride, x, y);
      const std::uint32_t s = dec.decode_bit(probs.p[kCtxSig + ctx]);
      std::int32_t q = 0;
      if (s) {
        const unsigned sign_ctx = sign_context(dest, stride, x, y);
        const std::uint32_t sign = dec.decode_bit(probs.p[kCtxSig + sign_ctx]);
        std::uint32_t qq = 0;
        for (;;) {
          const std::uint32_t bit = dec.decode_bit(
              probs.p[kCtxSig + kCtxSign + std::min<std::uint32_t>(qq, kCtxUnary - 1)]);
          if (bit) break;
          ++qq;
          if (qq > 65536u) {
            fail(error, "arithmetic magnitude overflow");
            return false;
          }
        }
        std::uint32_t m = qq << k;
        for (unsigned i = 0; i < static_cast<unsigned>(k); ++i)
          m |= dec.decode_bit(probs.p[kCtxSig + kCtxSign + kCtxUnary]) << i;
        q = static_cast<std::int32_t>(m) + 1;
        if (sign) q = -q;
        mag_sum += m;
        ++mag_count;
        if (mag_count == 64) {
          const std::uint64_t v = mag_sum / 64;
          int nk = 0;
          while (nk < 12 && (std::uint64_t{1} << (nk + 1)) <= v) ++nk;
          k = nk;
          mag_sum = 0;
          mag_count = 0;
        }
      }
      if (use_prediction) {
        const std::int32_t a = x > 0 ? dest[static_cast<std::size_t>(y) * stride + x - 1] : 0;
        const std::int32_t b = y > 0 ? dest[static_cast<std::size_t>(y - 1) * stride + x] : 0;
        const std::int32_t c = (x > 0 && y > 0) ? dest[static_cast<std::size_t>(y - 1) * stride + x - 1] : 0;
        const std::int32_t p = (y == 0) ? a : (x == 0 ? b : median_predict(a, b, c));
        q += p;
      }
      dest[static_cast<std::size_t>(y) * stride + x] = q;
    }
  }
  if (dec.truncated()) {
    fail(error, "truncated arithmetic band payload");
    return false;
  }
  if (dec.position() != size - 1) {
    fail(error, "arithmetic band payload size mismatch");
    return false;
  }
  // Dequantize in place: plain q*step (midtread reconstruction).
  for (std::uint32_t i = 0; i < count; ++i) {
    const std::int64_t q = dest[i];
    std::int64_t value = q * step;
    if (value < std::numeric_limits<std::int32_t>::min() ||
        value > std::numeric_limits<std::int32_t>::max()) {
      fail(error, "arithmetic coefficient overflow");
      return false;
    }
    dest[i] = static_cast<std::int32_t>(value);
  }
  return true;
}

// ---------------------------------------------------------------------------
// v2: single-channel pyramid
// ---------------------------------------------------------------------------

struct BandPyramid {
  std::uint32_t width = 0, height = 0;
  std::uint32_t base_width = 0, base_height = 0;
  std::vector<BandLevel> levels;  // finest to coarsest
  std::vector<std::int32_t> base;
};

// Builds one channel's pyramid in place: the caller's plane is consumed
// (moved), so the peak footprint is the pyramid plus the current level only.
static void build_band_pyramid(std::vector<std::int32_t>& plane,
                               std::uint32_t w, std::uint32_t h,
                               std::uint32_t threads, BandPyramid& pyramid) {
  const std::uint32_t target = safe_base_target(w, h);
  std::vector<std::int32_t> cur = std::move(plane);
  while (std::min(w, h) > target && w > 1 && h > 1) {
    BandLevel level;
    level.w = w;
    level.h = h;
    level.lw = (w + 1) / 2;
    level.lh = (h + 1) / 2;
    // Rows and columns are transformed in place; each line is independent.
    parallel_for(h, threads, [&](std::size_t yy) {
      std::vector<std::int32_t> line(w), packed(w);
      std::copy_n(cur.data() + yy * w, w, line.data());
      forward_line(line.data(), packed.data(), w);
      std::copy_n(packed.data(), w, cur.data() + yy * w);
    });
    parallel_for(w, threads, [&](std::size_t xx) {
      std::vector<std::int32_t> line(h), packed(h);
      for (std::uint32_t y = 0; y < h; ++y) line[y] = cur[y * w + xx];
      forward_line(line.data(), packed.data(), h);
      for (std::uint32_t y = 0; y < h; ++y) cur[y * w + xx] = packed[y];
    });
    extract_detail(cur, w, h, level.detail);
    std::vector<std::int32_t> next(static_cast<std::size_t>(level.lw) * level.lh);
    for (std::uint32_t y = 0; y < level.lh; ++y) {
      std::copy_n(cur.data() + static_cast<std::size_t>(y) * w, level.lw,
                  next.data() + static_cast<std::size_t>(y) * level.lw);
    }
    pyramid.levels.push_back(std::move(level));
    cur = std::move(next);
    w = level.lw;
    h = level.lh;
  }
  pyramid.width = w;
  pyramid.height = h;
  pyramid.base_width = w;
  pyramid.base_height = h;
  pyramid.base = std::move(cur);
}

// 2x2 box-average chroma downsample.
static void downsample_2x(std::vector<std::int32_t>& plane, std::uint32_t w,
                          std::uint32_t h, std::uint32_t& cw,
                          std::uint32_t& ch) {
  cw = (w + 1) / 2;
  ch = (h + 1) / 2;
  std::vector<std::int32_t> out(static_cast<std::size_t>(cw) * ch);
  for (std::uint32_t y = 0; y < ch; ++y) {
    for (std::uint32_t x = 0; x < cw; ++x) {
      std::int64_t sum = 0;
      unsigned n = 0;
      for (unsigned dy = 0; dy < 2; ++dy) {
        for (unsigned dx = 0; dx < 2; ++dx) {
          const std::uint32_t yy = 2 * y + dy;
          const std::uint32_t xx = 2 * x + dx;
          if (yy < h && xx < w) {
            sum += plane[static_cast<std::size_t>(yy) * w + xx];
            ++n;
          }
        }
      }
      out[static_cast<std::size_t>(y) * cw + x] =
          static_cast<std::int32_t>((sum + n / 2) / n);
    }
  }
  plane.swap(out);
}

// Bilinear upsample of a half-resolution chroma plane to full resolution.
static void upsample_plane(const std::vector<std::int32_t>& in, std::uint32_t w,
                           std::uint32_t h, std::uint32_t ow, std::uint32_t oh,
                           std::vector<std::int32_t>& out) {
  out.resize(static_cast<std::size_t>(ow) * oh);
  for (std::uint32_t yy = 0; yy < oh; ++yy) {
    const double sy = h == 1 ? 0.0 : static_cast<double>(yy) * (h - 1) / (oh - 1);
    const std::uint32_t y0 = static_cast<std::uint32_t>(sy);
    const std::uint32_t y1 = std::min(h - 1, y0 + 1);
    const double fy = sy - y0;
    for (std::uint32_t xx = 0; xx < ow; ++xx) {
      const double sx = w == 1 ? 0.0 : static_cast<double>(xx) * (w - 1) / (ow - 1);
      const std::uint32_t x0 = static_cast<std::uint32_t>(sx);
      const std::uint32_t x1 = std::min(w - 1, x0 + 1);
      const double fx = sx - x0;
      const double a = in[static_cast<std::size_t>(y0) * w + x0] * (1.0 - fx) +
                       in[static_cast<std::size_t>(y0) * w + x1] * fx;
      const double b = in[static_cast<std::size_t>(y1) * w + x0] * (1.0 - fx) +
                       in[static_cast<std::size_t>(y1) * w + x1] * fx;
      out[static_cast<std::size_t>(yy) * ow + xx] =
          static_cast<std::int32_t>(std::llround(a * (1.0 - fy) + b * fy));
    }
  }
}

// ---------------------------------------------------------------------------
// v2: stream container
// ---------------------------------------------------------------------------

struct Chunk {
  std::uint16_t layer = 0;
  std::uint8_t band = 0;
  std::uint8_t channel = 0;
  std::uint32_t x = 0, y = 0;
  std::uint16_t w = 0, h = 0, step = 1;
  std::uint16_t mode = 3;
  std::vector<std::uint8_t> payload;
  std::uint32_t count = 0;
  std::uint32_t checksum = 0;
};

static void put_u16(std::vector<std::uint8_t>& out, std::size_t off,
                    std::uint16_t value) {
  out[off + 0] = static_cast<std::uint8_t>(value);
  out[off + 1] = static_cast<std::uint8_t>(value >> 8);
}
static void put_u32(std::vector<std::uint8_t>& out, std::size_t off,
                    std::uint32_t value) {
  for (int i = 0; i < 4; ++i) out[off + i] = static_cast<std::uint8_t>(value >> (i * 8));
}
static void put_u64(std::vector<std::uint8_t>& out, std::size_t off,
                    std::uint64_t value) {
  for (int i = 0; i < 8; ++i) out[off + i] = static_cast<std::uint8_t>(value >> (i * 8));
}
static std::uint16_t get_u16(const std::uint8_t* p) {
  return static_cast<std::uint16_t>(p[0] | (static_cast<std::uint16_t>(p[1]) << 8));
}
static std::uint32_t get_u32(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
         (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
static std::uint64_t get_u64(const std::uint8_t* p) {
  std::uint64_t v = 0;
  for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(p[i]) << (i * 8);
  return v;
}

static void append_directory(std::vector<std::uint8_t>& out, const Chunk& c,
                             std::uint64_t offset) {
  const std::size_t off = out.size();
  out.resize(off + kDirectoryBytes, 0);
  put_u16(out, off + 0, c.layer);
  out[off + 2] = c.band;
  out[off + 3] = c.channel;
  put_u32(out, off + 4, c.x);
  put_u32(out, off + 8, c.y);
  put_u16(out, off + 12, c.w);
  put_u16(out, off + 14, c.h);
  put_u16(out, off + 16, c.step);
  put_u16(out, off + 18, c.mode);
  put_u64(out, off + 20, offset);
  put_u32(out, off + 28, static_cast<std::uint32_t>(c.payload.size()));
  put_u32(out, off + 32, c.count);
  put_u32(out, off + 36, c.checksum);
}


#ifdef BRUSHIE_TEST_HOOKS
// Test-only entry points for the band entropy coder.
static bool test_encode_band(const std::int32_t* band, std::uint32_t w,
                             std::uint32_t h, std::uint16_t step,
                             bool use_prediction, std::vector<std::uint8_t>& out) {
  std::uint64_t nonzero = 0, abs_sum = 0;
  std::vector<std::int32_t> tmp(band, band + static_cast<std::size_t>(w) * h);
  quantize_band(tmp, step, nonzero, abs_sum);
  if (nonzero == 0) return false;
  encode_band_arith(tmp.data(), w, h, use_prediction, nonzero, abs_sum, out);
  return true;
}
static bool test_decode_band(const std::uint8_t* data, std::size_t size,
                             std::uint32_t count, std::uint16_t step,
                             bool use_prediction, std::int32_t* dest,
                             std::uint32_t stride, std::uint32_t tw,
                             std::uint32_t th) {
  (void)0;
  return decode_band_arith(data, size, count, step, use_prediction, dest,
                           stride, tw, th, nullptr);
}
static std::int64_t floor_div_for_test(std::int64_t a, std::int64_t b) {
  return floor_div(a, b);
}
#endif

}  // namespace

// ---------------------------------------------------------------------------
// encode
// ---------------------------------------------------------------------------

bool encode(const ImageView& image, const EncodeOptions& options,
            EncodedImage& output, std::string* error) {
  EncodeOptions effective = options;
  if (!image.rgb || image.width == 0 || image.height == 0) {
    fail(error, "invalid image dimensions or null RGB pointer");
    return false;
  }
  if (image.width > kMaxDimension || image.height > kMaxDimension) {
    fail(error, "image dimensions exceed the format limit");
    return false;
  }
  if (options.target_bytes != 0)
    effective.quality = quality_for_target_bytes(image.width, image.height,
                                                 options.target_bytes, effective.quality);
  if (options.target_lpips > 0.0) {
    const std::uint8_t lpips_quality = quality_for_target_lpips(options.target_lpips);
    effective.quality = std::min(effective.quality, lpips_quality);
  }
  effective.tile_size = choose_tile(image.width, image.height, effective);
  if (effective.tile_size == 0 || effective.tile_size > kMaxTile) {
    fail(error, "tile size must be in 1..128");
    return false;
  }
  const bool subsample_chroma = effective.quality < 95;
  const bool has_alpha = image.channels == 4;

  std::array<std::vector<std::int32_t>, 4> planes;
  input_planes(image, planes, effective.threads);
  std::uint32_t cw = image.width, ch = image.height;
  if (subsample_chroma) {
    downsample_2x(planes[1], image.width, image.height, cw, ch);
    downsample_2x(planes[2], image.width, image.height, cw, ch);
  }

  BandPyramid pyr0, pyr1, pyr2, pyr3;
  build_band_pyramid(planes[0], image.width, image.height, effective.threads, pyr0);
  build_band_pyramid(planes[1], cw, ch, effective.threads, pyr1);
  build_band_pyramid(planes[2], cw, ch, effective.threads, pyr2);
  if (has_alpha)
    build_band_pyramid(planes[3], image.width, image.height, effective.threads, pyr3);

  // Collect one band reference per (layer, band, channel) in coarse-to-fine
  // order so that a decoder can stop after any progressive layer.
  struct BandRef {
    std::vector<std::int32_t>* data;
    std::uint32_t w = 0, h = 0;
    std::uint16_t layer = 0;
    std::uint8_t band = 0, channel = 0;
    std::uint16_t step = 1;
    bool predict = false;
  };
  std::vector<BandRef> refs;
  refs.reserve(1 + 3 * (pyr0.levels.size() + pyr1.levels.size() + pyr2.levels.size()));
  auto add_base = [&](std::vector<std::int32_t>& base, std::uint32_t bw,
                      std::uint32_t bh, std::uint8_t channel) {
    BandRef r;
    r.data = &base;
    r.w = bw;
    r.h = bh;
    r.layer = 0;
    r.band = 0;
    r.channel = channel;
    r.step = base_quant_step(effective.quality, channel);
    r.predict = true;
    refs.push_back(r);
  };
  add_base(pyr0.base, pyr0.base_width, pyr0.base_height, 0);
  add_base(pyr1.base, pyr1.base_width, pyr1.base_height, 1);
  add_base(pyr2.base, pyr2.base_width, pyr2.base_height, 2);
  if (has_alpha) add_base(pyr3.base, pyr3.base_width, pyr3.base_height, 3);

  const std::uint32_t L = static_cast<std::uint32_t>(pyr0.levels.size());
  const std::uint32_t CL = static_cast<std::uint32_t>(pyr1.levels.size());
  const std::uint32_t max_layers = std::max(L, CL);
  for (std::uint32_t k = 1; k <= max_layers; ++k) {
    auto add_details = [&](BandPyramid& pyr, std::uint8_t channel) {
      if (k > pyr.levels.size()) return;
      const std::uint32_t idx = static_cast<std::uint32_t>(pyr.levels.size()) - k;
      BandLevel& lev = pyr.levels[idx];
      const std::uint32_t from_finest = idx;
      const std::uint32_t num_levels = static_cast<std::uint32_t>(pyr.levels.size());
      BandRef r;
      r.layer = static_cast<std::uint16_t>(k);
      r.channel = channel;
      r.predict = false;
      r.band = 1;
      r.w = lev.w / 2;
      r.h = lev.lh;
      r.step = quant_step(effective.quality, from_finest, num_levels, 1, channel);
      r.data = &lev.detail[0];
      refs.push_back(r);
      r.band = 2;
      r.w = lev.lw;
      r.h = lev.h / 2;
      r.step = quant_step(effective.quality, from_finest, num_levels, 2, channel);
      r.data = &lev.detail[1];
      refs.push_back(r);
      r.band = 3;
      r.w = lev.w / 2;
      r.h = lev.h / 2;
      r.step = quant_step(effective.quality, from_finest, num_levels, 3, channel);
      r.data = &lev.detail[2];
      refs.push_back(r);
    };
    add_details(pyr0, 0);
    add_details(pyr1, 1);
    add_details(pyr2, 2);
    if (has_alpha) add_details(pyr3, 3);
  }

  std::vector<Chunk> chunks(refs.size());
  std::vector<std::uint8_t> active(refs.size(), 0);
  std::atomic<std::uint64_t> nonzero{0};
  parallel_for(refs.size(), effective.threads, [&](std::size_t i) {
    const BandRef& r = refs[i];
    std::uint64_t nz = 0, abs_sum = 0;
    quantize_band(*r.data, r.step, nz, abs_sum);
    if (nz == 0) return;
    Chunk& c = chunks[i];
    c.layer = r.layer;
    c.band = r.band;
    c.channel = r.channel;
    c.x = 0;
    c.y = 0;
    c.w = static_cast<std::uint16_t>(r.w);
    c.h = static_cast<std::uint16_t>(r.h);
    c.step = r.step;
    c.mode = 3;
    c.count = r.w * r.h;
    encode_band_arith(r.data->data(), r.w, r.h, r.predict, nz, abs_sum,
                      c.payload);
    c.checksum = fnv1a(c.payload.data(), c.payload.size());
    active[i] = 1;
    nonzero += nz;
  });

  std::vector<Chunk> ordered;
  ordered.reserve(refs.size());
  for (std::size_t i = 0; i < refs.size(); ++i)
    if (active[i]) ordered.push_back(std::move(chunks[i]));
  if (ordered.size() > kMaxChunks) {
    fail(error, "too many coefficient chunks");
    return false;
  }
  const std::uint64_t directory_bytes =
      static_cast<std::uint64_t>(ordered.size()) * kDirectoryBytes;
  const std::uint64_t data_offset = kHeaderBytes + directory_bytes;
  if (data_offset > std::numeric_limits<std::uint32_t>::max() * 16ull) {
    fail(error, "encoded stream is too large");
    return false;
  }
  output.bytes.assign(kHeaderBytes, 0);
  output.bytes[0] = 'C'; output.bytes[1] = 'A'; output.bytes[2] = 'P'; output.bytes[3] = 'S';
  put_u16(output.bytes, 4, kVersion);
  put_u16(output.bytes, 6, subsample_chroma ? 1u : 0u);
  put_u32(output.bytes, 8, image.width);
  put_u32(output.bytes, 12, image.height);
  put_u16(output.bytes, 16, static_cast<std::uint16_t>(pyr0.levels.size()));
  put_u16(output.bytes, 18, 0);  // v2 bands are whole-band streams; tile is unused
  output.bytes[20] = effective.quality;
  output.bytes[21] = has_alpha ? 4 : 3;
  put_u32(output.bytes, 24, pyr0.base_width);
  put_u32(output.bytes, 28, pyr0.base_height);
  put_u32(output.bytes, 32, static_cast<std::uint32_t>(ordered.size()));
  put_u32(output.bytes, 36, static_cast<std::uint32_t>(directory_bytes));
  put_u64(output.bytes, 40, data_offset);
  put_u32(output.bytes, 48, pyr1.base_width);
  put_u32(output.bytes, 52, pyr1.base_height);
  put_u32(output.bytes, 56, fnv1a(output.bytes.data(), 56));
  put_u32(output.bytes, 60, 0);
  std::uint64_t payload_offset = data_offset;
  for (const Chunk& chunk : ordered) {
    append_directory(output.bytes, chunk, payload_offset);
    payload_offset += chunk.payload.size();
  }
  for (const Chunk& chunk : ordered)
    output.bytes.insert(output.bytes.end(), chunk.payload.begin(), chunk.payload.end());
  output.stats.input_bytes = static_cast<std::uint64_t>(image.width) * image.height * 3;
  output.stats.encoded_bytes = output.bytes.size();
  output.stats.nonzero_coefficients = nonzero;
  output.stats.pyramid_levels = static_cast<std::uint32_t>(pyr0.levels.size());
  output.stats.chunks = static_cast<std::uint32_t>(ordered.size());
  output.stats.base_width = pyr0.base_width;
  output.stats.base_height = pyr0.base_height;
  return true;
}

// ---------------------------------------------------------------------------
// decode
// ---------------------------------------------------------------------------

static bool decode_v1(const std::uint8_t* data, std::size_t size,
                      std::uint32_t output_width, std::uint32_t output_height,
                      std::vector<std::uint8_t>& rgb, int max_progressive_layer,
                      std::string* error) {
  const std::uint32_t src_w = get_u32(data + 8), src_h = get_u32(data + 12);
  const std::uint16_t levels = get_u16(data + 16), tile = get_u16(data + 18);
  const std::uint32_t base_w = get_u32(data + 24), base_h = get_u32(data + 28);
  const std::uint32_t chunk_count = get_u32(data + 32), dir_bytes = get_u32(data + 36);
  const std::uint64_t data_offset = get_u64(data + 40);
  if (src_w == 0 || src_h == 0 || src_w > kMaxDimension || src_h > kMaxDimension ||
      tile == 0 || tile > kMaxTile || chunk_count > kMaxChunks ||
      dir_bytes != static_cast<std::uint64_t>(chunk_count) * kDirectoryBytes ||
      data_offset != kHeaderBytes + dir_bytes || data_offset > size) {
    fail(error, "invalid CAPS directory bounds");
    return false;
  }
  std::vector<LegacyLevel> shape(levels);
  std::uint32_t w = src_w, h = src_h;
  for (std::uint16_t i = 0; i < levels; ++i) {
    shape[i].w = w; shape[i].h = h; shape[i].lw = (w + 1) / 2; shape[i].lh = (h + 1) / 2;
    w = shape[i].lw; h = shape[i].lh;
  }
  if (w != base_w || h != base_h) {
    fail(error, "base dimensions do not match pyramid");
    return false;
  }
  std::array<std::vector<std::int32_t>, 3> base;
  for (auto& p : base) p.assign(static_cast<std::size_t>(base_w) * base_h, 0);
  for (auto& lev : shape)
    for (int c = 0; c < 3; ++c) {
      lev.detail[c][0].assign(static_cast<std::size_t>(lev.w / 2) * lev.lh, 0);
      lev.detail[c][1].assign(static_cast<std::size_t>(lev.lw) * (lev.h / 2), 0);
      lev.detail[c][2].assign(static_cast<std::size_t>(lev.w / 2) * (lev.h / 2), 0);
    }
  const int highest_layer = max_progressive_layer < 0 ? static_cast<int>(levels) : max_progressive_layer;
  for (std::uint32_t ci = 0; ci < chunk_count; ++ci) {
    const std::uint8_t* d = data + kHeaderBytes + static_cast<std::size_t>(ci) * kDirectoryBytes;
    const std::uint16_t layer = get_u16(d + 0);
    const std::uint8_t band = d[2], channel = d[3];
    const std::uint32_t x = get_u32(d + 4), y = get_u32(d + 8);
    const std::uint32_t tw = get_u16(d + 12), th = get_u16(d + 14), step = get_u16(d + 16);
    const std::uint16_t mode = get_u16(d + 18);
    const std::uint64_t offset = get_u64(d + 20);
    const std::uint32_t payload_size = get_u32(d + 28), count = get_u32(d + 32);
    const std::uint32_t checksum = get_u32(d + 36);
    if (channel >= 3 || band > 3 || mode > 2 || step == 0 || tw == 0 || th == 0 ||
        tw > tile || th > tile || count != static_cast<std::uint64_t>(tw) * th ||
        offset < data_offset || offset > size || payload_size > size - offset ||
        layer > levels) {
      fail(error, "invalid CAPS chunk metadata");
      return false;
    }
    if (layer > static_cast<std::uint16_t>(std::max(0, highest_layer))) continue;
    const std::uint8_t* payload = data + offset;
    if (fnv1a(payload, payload_size) != checksum) {
      fail(error, "coefficient chunk checksum mismatch");
      return false;
    }
    std::int32_t* destination = nullptr;
    std::uint32_t stride = 0, band_w = 0, band_h = 0;
    if (layer == 0) {
      if (band != 0 || x + tw > base_w || y + th > base_h) {
        fail(error, "base chunk out of bounds");
        return false;
      }
      destination = base[channel].data() + static_cast<std::size_t>(y) * base_w + x;
      stride = base_w; band_w = base_w; band_h = base_h;
    } else {
      const std::size_t idx = levels - layer;
      if (idx >= shape.size() || band == 0) {
        fail(error, "detail layer is invalid");
        return false;
      }
      const LegacyLevel& lev = shape[idx];
      band_w = band == 1 ? lev.w / 2 : lev.lw;
      band_h = band == 1 ? lev.lh : (band == 2 ? lev.h / 2 : lev.h / 2);
      if (band == 3) { band_w = lev.w / 2; band_h = lev.h / 2; }
      if (x + tw > band_w || y + th > band_h) {
        fail(error, "detail chunk out of bounds");
        return false;
      }
      destination = shape[idx].detail[channel][band - 1].data() + static_cast<std::size_t>(y) * band_w + x;
      stride = band_w;
    }
    if (!decode_tile(payload, payload_size, count, static_cast<std::uint16_t>(step), mode,
                     destination, stride, tw, th, error)) return false;
  }
  std::array<std::vector<std::int32_t>, 3> reconstructed;
  reconstructed = std::move(base);
  std::uint32_t cur_w = base_w, cur_h = base_h;
  for (std::size_t ri = levels; ri-- > 0;) {
    const LegacyLevel& lev = shape[ri];
    std::array<std::vector<std::int32_t>, 3> next;
    for (int c = 0; c < 3; ++c)
      inverse_level(reconstructed[c], lev.detail[c], lev.w, lev.h, next[c], 8);
    reconstructed = std::move(next);
    cur_w = lev.w; cur_h = lev.h;
    if (cur_w >= output_width && cur_h >= output_height) break;
  }
  std::array<std::vector<std::int32_t>, 4> rec4;
  rec4[0] = std::move(reconstructed[0]);
  rec4[1] = std::move(reconstructed[1]);
  rec4[2] = std::move(reconstructed[2]);
  rec4[3].assign(static_cast<std::size_t>(cur_w) * cur_h, 0);
  output_rgb(rec4, cur_w, cur_h, output_width, output_height, rgb, 3, 8);
  return true;
}

static bool decode_v2(const std::uint8_t* data, std::size_t size,
                      std::uint32_t output_width, std::uint32_t output_height,
                      std::vector<std::uint8_t>& rgb, int max_progressive_layer,
                      std::string* error) {
  const std::uint16_t flags = get_u16(data + 6);
  const bool subsampled = (flags & 1u) != 0;
  const std::uint32_t src_w = get_u32(data + 8), src_h = get_u32(data + 12);
  const std::uint16_t levels = get_u16(data + 16);
  const std::uint32_t base_w = get_u32(data + 24), base_h = get_u32(data + 28);
  const std::uint32_t chunk_count = get_u32(data + 32), dir_bytes = get_u32(data + 36);
  const std::uint64_t data_offset = get_u64(data + 40);
  const std::uint32_t c_base_w = get_u32(data + 48), c_base_h = get_u32(data + 52);
  const unsigned channels = data[21] == 4 ? 4u : 3u;
  if (src_w == 0 || src_h == 0 || src_w > kMaxDimension || src_h > kMaxDimension ||
      levels > 16 || chunk_count > kMaxChunks ||
      dir_bytes != static_cast<std::uint64_t>(chunk_count) * kDirectoryBytes ||
      data_offset != kHeaderBytes + dir_bytes || data_offset > size) {
    fail(error, "invalid CAPS directory bounds");
    return false;
  }
  // Per-channel pyramid geometry and detail storage. Each channel owns its
  // own arrays so bands never clobber each other. Channels 1 and 2 share the
  // chroma walk (identical to the luma walk at 4:4:4) but not the arrays.
  auto walk_shapes = [](std::uint32_t w, std::uint32_t h, std::uint16_t levels,
                        std::vector<BandLevel>& out) -> std::pair<std::uint32_t, std::uint32_t> {
    out.resize(levels);
    std::uint32_t cw = w, chh = h;
    for (std::uint16_t i = 0; i < levels; ++i) {
      out[i].w = cw;
      out[i].h = chh;
      out[i].lw = (cw + 1) / 2;
      out[i].lh = (chh + 1) / 2;
      cw = out[i].lw;
      chh = out[i].lh;
    }
    return {cw, chh};
  };
  std::vector<BandLevel> shapes0, shapes1, shapes2, shapes3;
  std::uint32_t c_in_w = src_w, c_in_h = src_h;
  const auto luma_base = walk_shapes(src_w, src_h, levels, shapes0);
  if (channels == 4) walk_shapes(src_w, src_h, levels, shapes3);
  if (luma_base.first != base_w || luma_base.second != base_h) {
    fail(error, "base dimensions do not match pyramid");
    return false;
  }
  std::uint32_t ch_base_w = base_w, ch_base_h = base_h;
  if (subsampled) {
    c_in_w = (src_w + 1) / 2;
    c_in_h = (src_h + 1) / 2;
    const std::uint32_t target = safe_base_target(c_in_w, c_in_h);
    std::uint32_t w = c_in_w, h = c_in_h;
    std::uint16_t cl = 0;
    while (std::min(w, h) > target && w > 1 && h > 1) {
      ++cl;
      w = (w + 1) / 2;
      h = (h + 1) / 2;
    }
    if (w != c_base_w || h != c_base_h) {
      fail(error, "chroma base dimensions do not match pyramid");
      return false;
    }
    // Chroma has its own (possibly shorter) level stack; chunks for channel
    // 1/2 at layer k reference chroma level (cl - k).
    w = c_in_w;
    h = c_in_h;
    for (std::uint16_t i = 0; i < cl; ++i) {
      BandLevel lev;
      lev.w = w;
      lev.h = h;
      lev.lw = (w + 1) / 2;
      lev.lh = (h + 1) / 2;
      shapes1.push_back(lev);
      shapes2.push_back(lev);
      w = lev.lw;
      h = lev.lh;
    }
    ch_base_w = c_base_w;
    ch_base_h = c_base_h;
  } else {
    // 4:4:4: chroma geometry equals luma geometry, but keep separate arrays.
    walk_shapes(src_w, src_h, levels, shapes1);
    walk_shapes(src_w, src_h, levels, shapes2);
  }
  std::array<std::vector<std::int32_t>, 4> base;
  base[0].assign(static_cast<std::size_t>(base_w) * base_h, 0);
  base[1].assign(static_cast<std::size_t>(ch_base_w) * ch_base_h, 0);
  base[2].assign(static_cast<std::size_t>(ch_base_w) * ch_base_h, 0);
  base[3].assign(static_cast<std::size_t>(base_w) * base_h, 0);
  auto allocate_levels = [](std::vector<BandLevel>& vec) {
    for (auto& lev : vec) {
      lev.detail[0].assign(static_cast<std::size_t>(lev.w / 2) * lev.lh, 0);
      lev.detail[1].assign(static_cast<std::size_t>(lev.lw) * (lev.h / 2), 0);
      lev.detail[2].assign(static_cast<std::size_t>(lev.w / 2) * (lev.h / 2), 0);
    }
  };
  allocate_levels(shapes0);
  allocate_levels(shapes1);
  allocate_levels(shapes2);
  if (channels == 4) allocate_levels(shapes3);

  const int highest_layer = max_progressive_layer < 0 ? static_cast<int>(levels) : max_progressive_layer;
  for (std::uint32_t ci = 0; ci < chunk_count; ++ci) {
    const std::uint8_t* d = data + kHeaderBytes + static_cast<std::size_t>(ci) * kDirectoryBytes;
    const std::uint16_t layer = get_u16(d + 0);
    const std::uint8_t band = d[2], channel = d[3];
    const std::uint32_t x = get_u32(d + 4), y = get_u32(d + 8);
    const std::uint32_t tw = get_u16(d + 12), th = get_u16(d + 14);
    const std::uint16_t step = get_u16(d + 16);
    const std::uint16_t mode = get_u16(d + 18);
    const std::uint64_t offset = get_u64(d + 20);
    const std::uint32_t payload_size = get_u32(d + 28), count = get_u32(d + 32);
    const std::uint32_t checksum = get_u32(d + 36);
    if (channel >= channels || band > 3 || mode != 3 || step == 0 || tw == 0 || th == 0 ||
        count != static_cast<std::uint64_t>(tw) * th ||
        offset < data_offset || offset > size || payload_size > size - offset ||
        layer > levels) {
      fail(error, "invalid CAPS chunk metadata");
      return false;
    }
    if (layer > static_cast<std::uint16_t>(std::max(0, highest_layer))) continue;
    std::vector<BandLevel>& shapes =
        channel == 0 ? shapes0 : (channel == 1 ? shapes1 : (channel == 2 ? shapes2 : shapes3));
    std::int32_t* destination = nullptr;
    std::uint32_t stride = 0, band_w = 0, band_h = 0;
    if (layer == 0) {
      // Channel 3 (alpha) is coded at full resolution like channel 0.
      const std::uint32_t b_w = channel <= 1 || channel == 3 ? base_w : ch_base_w;
      const std::uint32_t b_h = channel <= 1 || channel == 3 ? base_h : ch_base_h;
      if (band != 0 || x != 0 || y != 0 || tw != b_w || th != b_h) {
        fail(error, "base chunk out of bounds");
        return false;
      }
      destination = base[channel].data();
      stride = b_w;
      band_w = b_w;
      band_h = b_h;
    } else {
      if (band == 0 || layer > shapes.size()) {
        fail(error, "detail layer is invalid");
        return false;
      }
      const std::uint32_t idx = static_cast<std::uint32_t>(shapes.size()) - layer;
      BandLevel& lev = shapes[idx];
      band_w = band == 1 ? lev.w / 2 : lev.lw;
      band_h = band == 1 ? lev.lh : (band == 2 ? lev.h / 2 : lev.h / 2);
      if (band == 3) { band_w = lev.w / 2; band_h = lev.h / 2; }
      if (x != 0 || y != 0 || tw != band_w || th != band_h) {
        fail(error, "detail chunk out of bounds");
        return false;
      }
      destination = lev.detail[band - 1].data();
      stride = band_w;
    }
    const std::uint8_t* payload = data + offset;
    if (fnv1a(payload, payload_size) != checksum) {
      fail(error, "coefficient chunk checksum mismatch");
      return false;
    }
    if (!decode_band_arith(payload, payload_size, count, step, band == 0,
                           destination, stride, tw, th, error))
      return false;
  }

  std::array<std::vector<std::int32_t>, 4> rec;
  rec[0] = std::move(base[0]);
  rec[1] = std::move(base[1]);
  rec[2] = std::move(base[2]);
  rec[3] = std::move(base[3]);
  for (std::size_t ri = shapes0.size(); ri-- > 0;) {
    const BandLevel& lev = shapes0[ri];
    std::vector<std::int32_t> next;
    inverse_level(rec[0], lev.detail, lev.w, lev.h, next, 8);
    rec[0] = std::move(next);
  }
  for (std::size_t ri = shapes1.size(); ri-- > 0;) {
    const BandLevel& lev = shapes1[ri];
    std::vector<std::int32_t> n1;
    inverse_level(rec[1], lev.detail, lev.w, lev.h, n1, 8);
    rec[1] = std::move(n1);
  }
  for (std::size_t ri = shapes2.size(); ri-- > 0;) {
    const BandLevel& lev = shapes2[ri];
    std::vector<std::int32_t> n2;
    inverse_level(rec[2], lev.detail, lev.w, lev.h, n2, 8);
    rec[2] = std::move(n2);
  }
  if (channels == 4) {
    for (std::size_t ri = shapes3.size(); ri-- > 0;) {
      const BandLevel& lev = shapes3[ri];
      std::vector<std::int32_t> n3;
      inverse_level(rec[3], lev.detail, lev.w, lev.h, n3, 8);
      rec[3] = std::move(n3);
    }
  }
  if (subsampled) {
    std::vector<std::int32_t> up1, up2;
    upsample_plane(rec[1], c_in_w, c_in_h, src_w, src_h, up1);
    upsample_plane(rec[2], c_in_w, c_in_h, src_w, src_h, up2);
    rec[1] = std::move(up1);
    rec[2] = std::move(up2);
  }
  output_rgb(rec, src_w, src_h, output_width, output_height, rgb, channels, 8);
  return true;
}

bool decode(const std::uint8_t* data, std::size_t size,
            std::uint32_t output_width, std::uint32_t output_height,
            std::vector<std::uint8_t>& rgb, int max_progressive_layer,
            std::string* error) {
  if (!data || size < kHeaderBytes || output_width == 0 || output_height == 0 ||
      output_width > kMaxDimension * 2 || output_height > kMaxDimension * 2) {
    fail(error, "invalid stream or output dimensions");
    return false;
  }
  if (std::memcmp(data, "CAPS", 4) != 0) {
    fail(error, "unsupported CAPS stream");
    return false;
  }
  if (get_u32(data + 56) != fnv1a(data, 56)) {
    fail(error, "header checksum mismatch");
    return false;
  }
  const std::uint16_t version = get_u16(data + 4);
  if (version == kVersion) return decode_v2(data, size, output_width, output_height, rgb, max_progressive_layer, error);
  if (version == kVersionLegacy) return decode_v1(data, size, output_width, output_height, rgb, max_progressive_layer, error);
  fail(error, "unsupported CAPS stream version");
  return false;
}

}  // namespace brushie
