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

constexpr std::uint16_t kVersion = 5;
constexpr std::uint16_t kVersionBandV2 = 2;
constexpr std::uint16_t kVersionLegacy = 1;
constexpr std::size_t kHeaderBytes = 64;
constexpr std::size_t kDirectoryBytesLegacy = 40;
constexpr std::size_t kDirectoryBytes = 16;  // v5: compact entry without checksum
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

// Entropy layouts. The mode byte and stream version select the decoder
// path; historical streams keep their exact context indices so they stay
// decodable:
//   v2 (version 2):  sig 8..15, sign 8..11 (overlaps sig), unary 12..25, rem 26..37
//   v3 (version 3, mode 3): sig 8..15, sign 8..11 (overlaps), unary 12..35, rem 36..48
//   v4 mode 3:       sig 8..15, sign 16..19 (fixed separation), unary 20..43, rem 44..56
//   v4 mode 4:       parent sig+sign (16/8), unary 24 + class*6 + pos, rem 48..60
//   v4 mode 5:       parent sig only (16),   unary 20 + pos,            rem 44..56
//   v4 mode 6:       no parent (8/4),        unary 24 + class*6 + pos,  rem 48..60
constexpr unsigned kCtxBlk = 4;  // block-flag neighbour contexts (mode 12)
constexpr unsigned kNumCtx = 65;

static bool legacy_layout(std::uint16_t version, std::uint8_t mode) {
  return version < 4 && mode == 3;
}
static unsigned sig_idx(std::uint16_t version, std::uint8_t mode, unsigned ctx8,
                        bool parent_sig) {
  if (legacy_layout(version, mode)) return 8u + ctx8;
  if (mode == 3 || mode == 6 || mode == 11) return ctx8;  // no parent context
  return ctx8 + (parent_sig ? 8u : 0u);     // v4 parent significance (4/5)
}
static unsigned sign_idx(std::uint16_t version, std::uint8_t mode, unsigned ctx4,
                         bool parent_neg) {
  if (legacy_layout(version, mode)) return 8u + ctx4;   // v3 historical overlap
  if (mode == 4) return 16u + ctx4 + (parent_neg ? 4u : 0u);
  return 16u + ctx4;
}
static unsigned unary_idx(std::uint16_t version, std::uint8_t mode,
                          std::uint32_t pos, unsigned mclass) {
  if (legacy_layout(version, mode)) return 12u + std::min<std::uint32_t>(pos, 23u);
  if (mode == 4 || mode == 6) return 24u + mclass * 6u + std::min<std::uint32_t>(pos, 5u);
  return 20u + std::min<std::uint32_t>(pos, 23u);
}
static unsigned rem_idx(std::uint16_t version, std::uint8_t mode, unsigned i) {
  if (legacy_layout(version, mode)) return 36u + i;
  return (mode == 4 || mode == 6) ? 48u + i : 44u + i;
}

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

// Gradient-adjusted predictor (CALIC GAP). Reads from the quantized band via
// an accessor so encode (original values) and decode (reconstructed values)
// observe the same causal neighbourhood.
static std::int32_t gap_predict(const std::int32_t* band, std::uint32_t stride,
                                std::uint32_t x, std::uint32_t y) {
  const auto at = [&](std::int32_t dx, std::int32_t dy) -> std::int32_t {
    const std::int32_t xx = static_cast<std::int32_t>(x) + dx;
    const std::int32_t yy = static_cast<std::int32_t>(y) + dy;
    if (xx < 0 || yy < 0) return 0;
    return band[static_cast<std::size_t>(yy) * stride + static_cast<std::uint32_t>(xx)];
  };
  if (x == 0 && y == 0) return 0;
  if (y == 0) return at(-1, 0);          // first row: left neighbour
  if (x == 0) return at(0, -1);          // first column: above neighbour
  const std::int32_t w = at(-1, 0), n = at(0, -1);
  const std::int32_t ww = at(-2, 0), nn = at(0, -2);
  const std::int32_t nw = at(-1, -1), ne = at(1, -1);
  const std::int32_t dh = std::abs(w - ww) + std::abs(n - nw) + std::abs(n - ne);
  const std::int32_t dv = std::abs(w - nw) + std::abs(n - nn) + std::abs(ne - n);
  std::int32_t p;
  const std::int32_t d = dv - dh;
  if (d > 80) {
    p = w;
  } else if (d < -80) {
    p = n;
  } else {
    p = (w + n) / 2 + (ne - nw) / 4;
    if (d > 32) p = (p + w) / 2;
    else if (d > 8) p = (3 * p + w) / 4;
    else if (d < -32) p = (p + n) / 2;
    else if (d < -8) p = (3 * p + n) / 4;
  }
  return p;
}

// Quantization steps calibrated from the frozen Kodak/DIV2K sweep. The
// coarsest detail level gets the largest step (few coefficients, lowest
// perceptual weight per bit) and the finest level the smallest; this matches
// the natural energy distribution of the 5/3 pyramid so the RD curve stays
// smooth. The diagonal band is penalized slightly and chroma is penalized 2x
// (it is also subsampled at lossy operating points).
//
// Tunables (env BRUSHIE_QPARAMS="rd,elo,ehi,dlo,dhi,clo,chi,bm" — one sweep
// hook for the recursive eval; defaults are the calibrated v3 table):
//   rd   root divisor          (root = 1 + loss/rd)
//   elo  coarse-level exponent below q95
//   ehi  coarse-level exponent at q95+
//   dlo  diagonal multiplier below q95
//   dhi  diagonal multiplier at q95+
//   clo  chroma multiplier below q95
//   chi  chroma multiplier at q95+
//   bm   base LL multiplier
static void quant_params(double& root_div, double& exp_lo, double& exp_hi,
                         double& diag_lo, double& diag_hi, double& chroma_lo,
                         double& chroma_hi, double& base_mul) {
  root_div = 6.0; exp_lo = 1.25; exp_hi = 0.8; diag_lo = 1.8; diag_hi = 1.2;
  chroma_lo = 2.5; chroma_hi = 2.0; base_mul = 0.4;  // sweep winner
  const char* e = std::getenv("BRUSHIE_QPARAMS");
  if (!e) return;
  double v[8];
  int n = 0;
  const char* p = e;
  while (*p && n < 8) {
    v[n++] = std::atof(p);
    while (*p && *p != ',') ++p;
    if (*p == ',') ++p;
  }
  if (n >= 1 && v[0] > 0) root_div = v[0];
  if (n >= 2 && v[1] > 0) exp_lo = v[1];
  if (n >= 3 && v[2] > 0) exp_hi = v[2];
  if (n >= 4 && v[3] > 0) diag_lo = v[3];
  if (n >= 5 && v[4] > 0) diag_hi = v[4];
  if (n >= 6 && v[5] > 0) chroma_lo = v[5];
  if (n >= 7 && v[6] > 0) chroma_hi = v[6];
  if (n >= 8 && v[7] > 0) base_mul = v[7];
}
static std::uint16_t quant_step(std::uint8_t quality,
                                std::uint32_t level_from_finest,
                                std::uint32_t num_levels, std::uint8_t band,
                                std::uint8_t channel) {
  const std::uint32_t loss = 100u - std::min<std::uint8_t>(quality, 100);
  if (loss == 0) return 1;
  double root_div, exp_lo, exp_hi, diag_lo, diag_hi, chroma_lo, chroma_hi, base_mul;
  quant_params(root_div, exp_lo, exp_hi, diag_lo, diag_hi, chroma_lo, chroma_hi, base_mul);
  double root = 1.0 + static_cast<double>(loss) / root_div;
  // level_from_finest: 0 = finest detail level, num_levels-1 = coarsest.
  // Steps grow 2^(1.25*coarseness), saturating after three levels so the
  // table stays sane for deep pyramids on large images: the sparse coarse
  // detail is quantized hardest while the fine levels stay cheap per
  // coefficient. Calibrated on the frozen Kodak sweep at equal MS-SSIM gates
  // (~25% byte reduction vs the previous 2^(coarseness/2) table).
  const std::uint32_t coarseness = num_levels - 1 - level_from_finest;
  // The corrected local-window metric exposed a visually-lossless rate
  // cliff: q99 still used very coarse low-frequency detail steps, then q100
  // jumped to full lossless. Preserve the delivery-optimized exponent below
  // q95, but use a gentler high-quality allocation at q95..99. This leaves
  // .970/.985 operating points unchanged and avoids the lossless fallback.
  const double exponent = quality < 95 ? exp_lo : exp_hi;
  const double weight = std::pow(2.0, exponent * std::min<double>(coarseness, 3.0));
  double step = root * weight;
  // Corrected-metric allocation: delivery qualities can mask diagonal and
  // chroma error more aggressively; q95+ relaxes both for the visually-
  // lossless tier. Alpha (channel 3) follows luma, never chroma weighting.
  if (band == 3) step *= quality < 95 ? diag_lo : diag_hi;
  if (channel == 1 || channel == 2) step *= quality < 95 ? chroma_lo : chroma_hi;
  return static_cast<std::uint16_t>(
      std::min<double>(65535.0, std::max<double>(1.0, step)));
}

static std::uint16_t base_quant_step(std::uint8_t quality,
                                     std::uint8_t channel) {
  const std::uint32_t loss = 100u - std::min<std::uint8_t>(quality, 100);
  if (loss == 0) return 1;
  double root_div, exp_lo, exp_hi, diag_lo, diag_hi, chroma_lo, chroma_hi, base_mul;
  quant_params(root_div, exp_lo, exp_hi, diag_lo, diag_hi, chroma_lo, chroma_hi, base_mul);
  // The base LL is preserved with a finer step (0.5x root): low-frequency
  // structure dominates the SSIM-family gates and is cheap to code.
  double step = base_mul * (1.0 + static_cast<double>(loss) / root_div);
  if (channel == 1 || channel == 2) step *= quality < 95 ? chroma_lo : chroma_hi;
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

static unsigned mag_class(std::int32_t v) {
  const std::uint32_t a = static_cast<std::uint32_t>(v < 0 ? -v : v);
  return a == 0 ? 0u : (a == 1 ? 1u : (a <= 3 ? 2u : 3u));
}
static void encode_band_arith(const std::int32_t* band, std::uint32_t w,
                              std::uint32_t h, bool use_prediction,
                              std::uint64_t nonzero,
                              std::uint64_t abs_sum,
                              const std::int32_t* parent,
                              std::uint32_t parent_stride,
                              std::uint8_t mode,
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
  const bool use_parent = parent != nullptr;
  if (mode == 12) {
    // Block significance flags (16x16): a zero block costs one context bit
    // and skips every coefficient symbol inside it (EBCOT-style codeblocks).
    static const std::uint32_t kB = []() {
      const char* e = std::getenv("BRUSHIE_BLOCK");
      if (!e) return 16u;
      const int v = std::atoi(e);
      return (v == 8 || v == 32 || v == 64) ? static_cast<std::uint32_t>(v) : 16u;
    }();
    const std::uint32_t bw = (w + kB - 1) / kB;
    const std::uint32_t bh = (h + kB - 1) / kB;
    std::vector<std::uint8_t> block_nz(bw * bh, 0);
    for (std::uint32_t by = 0; by < bh; ++by) {
      for (std::uint32_t bx = 0; bx < bw; ++bx) {
        bool nz = false;
        for (std::uint32_t yy = by * kB; yy < std::min(h, (by + 1) * kB) && !nz; ++yy)
          for (std::uint32_t xx = bx * kB; xx < std::min(w, (bx + 1) * kB); ++xx)
            if (band[static_cast<std::size_t>(yy) * w + xx] != 0) { nz = true; break; }
        block_nz[by * bw + bx] = nz ? 1 : 0;
        unsigned bctx = 0;
        if (bx > 0 && block_nz[by * bw + bx - 1]) bctx += 1;
        if (by > 0 && block_nz[(by - 1) * bw + bx]) bctx += 2;
        enc.encode_bit(probs.p[kCtxBlk + bctx], nz ? 1u : 0u);
      }
    }
    for (std::uint32_t by = 0; by < bh; ++by) {
      for (std::uint32_t bx = 0; bx < bw; ++bx) {
        if (!block_nz[by * bw + bx]) continue;
        for (std::uint32_t yy = by * kB; yy < std::min(h, (by + 1) * kB); ++yy) {
          for (std::uint32_t xx = bx * kB; xx < std::min(w, (bx + 1) * kB); ++xx) {
            const std::uint32_t x = xx, y = yy;
            std::int32_t q = band[static_cast<std::size_t>(y) * w + x];
            if (use_prediction) {
              const std::int32_t a = x > 0 ? band[static_cast<std::size_t>(y) * w + x - 1] : 0;
              const std::int32_t b = y > 0 ? band[static_cast<std::size_t>(y - 1) * w + x] : 0;
              const std::int32_t c = (x > 0 && y > 0) ? band[static_cast<std::size_t>(y - 1) * w + x - 1] : 0;
              const std::int32_t p = (y == 0) ? a : (x == 0 ? b : median_predict(a, b, c));
              q -= p;
            }
            const std::int32_t pv = use_parent
                ? parent[static_cast<std::size_t>(y / 2) * parent_stride + x / 2] : 0;
            const unsigned ctx = sig_idx(4, mode, sig_context(band, w, x, y), use_parent && pv != 0);
            const std::uint32_t s = (q != 0) ? 1u : 0u;
            enc.encode_bit(probs.p[ctx], s);
            if (s) {
              const std::uint32_t sign = q < 0 ? 1u : 0u;
              const unsigned sign_ctx = sign_idx(4, mode, sign_context(band, w, x, y), use_parent && pv < 0);
              enc.encode_bit(probs.p[sign_ctx], sign);
              const std::uint32_t m_orig = static_cast<std::uint32_t>(q < 0 ? -q : q) - 1u;
              std::uint32_t m = m_orig;
              const std::int32_t lv8 = x > 0 ? band[static_cast<std::size_t>(y) * w + x - 1] : 0;
              const std::int32_t av8 = y > 0 ? band[static_cast<std::size_t>(y - 1) * w + x] : 0;
              std::uint32_t ml = static_cast<std::uint32_t>(lv8 < 0 ? -lv8 : lv8);
              std::uint32_t ma = static_cast<std::uint32_t>(av8 < 0 ? -av8 : av8);
              const std::uint32_t mean1 = (ml + ma) / 2 + 1u;
              int nk = 0;
              while (nk < 12 && (std::uint32_t{1} << (nk + 1)) <= mean1) ++nk;
              const int kk = std::max(nk, k);
              const std::uint32_t qq = m >> kk;
              const std::uint32_t rr = m & ((1u << kk) - 1u);
              for (std::uint32_t i = 0; i < qq; ++i)
                enc.encode_bit(probs.p[unary_idx(4, mode, i, 0)], 0);
              enc.encode_bit(probs.p[unary_idx(4, mode, qq, 0)], 1);
              for (unsigned i = 0; i < static_cast<unsigned>(kk); ++i)
                enc.encode_bit(probs.p[rem_idx(4, mode, i)], (rr >> i) & 1u);
              mag_sum += m_orig;
              ++mag_count;
              if (mag_count == 64) {
                const std::uint64_t v = mag_sum / 64;
                int nk2 = 0;
                while (nk2 < 12 && (std::uint64_t{1} << (nk2 + 1)) <= v) ++nk2;
                k = nk2;
                mag_sum = 0;
                mag_count = 0;
              }
            }
          }
        }
      }
    }
    enc.flush();
    return;
  }
  for (std::uint32_t y = 0; y < h; ++y) {
    for (std::uint32_t x = 0; x < w; ++x) {
      std::int32_t q = band[static_cast<std::size_t>(y) * w + x];
      if (use_prediction) {
        const std::int32_t p = (mode == 7) ? gap_predict(band, w, x, y)
                                           : median_predict(
                                                 x > 0 ? band[static_cast<std::size_t>(y) * w + x - 1] : 0,
                                                 y > 0 ? band[static_cast<std::size_t>(y - 1) * w + x] : 0,
                                                 (x > 0 && y > 0) ? band[static_cast<std::size_t>(y - 1) * w + x - 1] : 0);
        q -= p;
      }
      const std::int32_t pv = use_parent
          ? parent[static_cast<std::size_t>(y / 2) * parent_stride + x / 2] : 0;
      const unsigned ctx = sig_context(band, w, x, y);
      std::uint32_t s;
      if (mode == 11 && use_parent) {
        if (pv == 0) {
          // Parent-gated significance: a zero parent makes this coefficient
          // very likely zero, so only a cheap "still zero" bit is coded
          // (zerotree-style hard skip). Nonzero escapes code sign+magnitude.
          const std::uint32_t z = (q != 0) ? 1u : 0u;
          enc.encode_bit(probs.p[ctx], z);
          s = z;
        } else {
          const std::uint32_t sg = sig_idx(4, mode, ctx, false);
          s = (q != 0) ? 1u : 0u;
          enc.encode_bit(probs.p[sg], s);
        }
      } else {
        const unsigned sg = sig_idx(4, mode, ctx, use_parent && pv != 0);
        s = (q != 0) ? 1u : 0u;
        enc.encode_bit(probs.p[sg], s);
      }
      if (s) {
        const std::uint32_t sign = q < 0 ? 1u : 0u;
        const unsigned sign_ctx = sign_idx(4, mode, sign_context(band, w, x, y),
                                           use_parent && pv < 0);
        enc.encode_bit(probs.p[sign_ctx], sign);
        const std::uint32_t m_orig = static_cast<std::uint32_t>(q < 0 ? -q : q) - 1u;
        std::uint32_t m = m_orig;
        std::uint32_t qq, rr;
        int kk = k;
        if (mode == 9) {
          // Magnitude prediction: the local min(|left|,|above|) predicts the
          // magnitude; the zigzag residual keeps Rice coding non-negative and
          // peaks at zero for edges/regions with smooth magnitude fields.
          const std::int32_t lv9 = x > 0 ? band[static_cast<std::size_t>(y) * w + x - 1] : 0;
          const std::int32_t av9 = y > 0 ? band[static_cast<std::size_t>(y - 1) * w + x] : 0;
          std::uint32_t ml = static_cast<std::uint32_t>(lv9 < 0 ? -lv9 : lv9);
          std::uint32_t ma = static_cast<std::uint32_t>(av9 < 0 ? -av9 : av9);
          const std::uint32_t mp = std::min(ml, ma);
          const std::int64_t d = static_cast<std::int64_t>(m_orig) - static_cast<std::int64_t>(mp);
          const std::uint32_t z = d >= 0 ? static_cast<std::uint32_t>(2 * d)
                                         : static_cast<std::uint32_t>(-2 * d - 1);
          m = z;
        }
        if (mode == 8 || mode == 9) {
          // Per-coefficient Rice parameter from causal neighbour magnitudes:
          // the local scale of already-coded coefficients predicts this one
          // (text edges and texture produce strongly correlated magnitudes).
          const std::int32_t lv8 = x > 0 ? band[static_cast<std::size_t>(y) * w + x - 1] : 0;
          const std::int32_t av8 = y > 0 ? band[static_cast<std::size_t>(y - 1) * w + x] : 0;
          std::uint32_t ml = static_cast<std::uint32_t>(lv8 < 0 ? -lv8 : lv8);
          std::uint32_t ma = static_cast<std::uint32_t>(av8 < 0 ? -av8 : av8);
          const std::uint32_t mean1 = (ml + ma) / 2 + 1u;
          int nk = 0;
          while (nk < 12 && (std::uint32_t{1} << (nk + 1)) <= mean1) ++nk;
          kk = std::max(nk, k);   // band-adapted k is the floor
          qq = m >> kk;
          rr = m & ((1u << kk) - 1u);
        } else {
          qq = m >> k;
          rr = m & ((1u << k) - 1u);
        }
        const std::int32_t lv = x > 0 ? band[static_cast<std::size_t>(y) * w + x - 1] : 0;
        const std::int32_t av = y > 0 ? band[static_cast<std::size_t>(y - 1) * w + x] : 0;
        const unsigned mclass = std::min(mag_class(lv), mag_class(av));
        for (std::uint32_t i = 0; i < qq; ++i)
          enc.encode_bit(probs.p[unary_idx(4, mode, i, mclass)], 0);
        enc.encode_bit(probs.p[unary_idx(4, mode, qq, mclass)], 1);
        for (int i = 0; i < kk; ++i)
          enc.encode_bit(probs.p[rem_idx(4, mode, static_cast<unsigned>(i))], (rr >> i) & 1u);

        
        mag_sum += m_orig;
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
                              std::uint32_t th, std::string* error,
                              const std::int32_t* parent = nullptr,
                              std::uint32_t parent_stride = 0,
                              bool v2_entropy_layout = false,
                              std::uint8_t mode = 3,
                              std::uint16_t version = 4) {
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
  const bool use_parent = parent != nullptr;
  if (mode == 12) {
    static const std::uint32_t kB = []() {
      const char* e = std::getenv("BRUSHIE_BLOCK");
      if (!e) return 16u;
      const int v = std::atoi(e);
      return (v == 8 || v == 32 || v == 64) ? static_cast<std::uint32_t>(v) : 16u;
    }();
    const std::uint32_t bw = (tw + kB - 1) / kB;
    const std::uint32_t bh = (th + kB - 1) / kB;
    std::vector<std::uint8_t> block_nz(bw * bh, 0);
    for (std::uint32_t by = 0; by < bh; ++by) {
      for (std::uint32_t bx = 0; bx < bw; ++bx) {
        unsigned bctx = 0;
        if (bx > 0 && block_nz[by * bw + bx - 1]) bctx += 1;
        if (by > 0 && block_nz[(by - 1) * bw + bx]) bctx += 2;
        block_nz[by * bw + bx] = dec.decode_bit(probs.p[kCtxBlk + bctx]) ? 1 : 0;
      }
    }
    for (std::uint32_t by = 0; by < bh; ++by) {
      for (std::uint32_t bx = 0; bx < bw; ++bx) {
        if (!block_nz[by * bw + bx]) continue;
        for (std::uint32_t yy = by * kB; yy < std::min(th, (by + 1) * kB); ++yy) {
          for (std::uint32_t xx = bx * kB; xx < std::min(tw, (bx + 1) * kB); ++xx) {
            const std::uint32_t x = xx, y = yy;
            const std::int32_t pv = use_parent
                ? parent[static_cast<std::size_t>(y / 2) * parent_stride + x / 2] : 0;
            const unsigned raw_ctx = sig_context(dest, stride, x, y);
            const std::uint32_t s = dec.decode_bit(
                probs.p[sig_idx(version, mode, raw_ctx, use_parent && pv != 0)]);
            std::int32_t q = 0;
            if (s) {
              const unsigned sign_ctx = sign_idx(version, mode,
                                                 sign_context(dest, stride, x, y),
                                                 use_parent && pv < 0);
              const std::uint32_t sign = dec.decode_bit(probs.p[sign_ctx]);
              std::uint32_t qq = 0;
              int kk = k;
              {
                const std::int32_t lv8 = x > 0 ? dest[static_cast<std::size_t>(y) * stride + x - 1] : 0;
                const std::int32_t av8 = y > 0 ? dest[static_cast<std::size_t>(y - 1) * stride + x] : 0;
                std::uint32_t ml = static_cast<std::uint32_t>(lv8 < 0 ? -lv8 : lv8);
                std::uint32_t ma = static_cast<std::uint32_t>(av8 < 0 ? -av8 : av8);
                const std::uint32_t mean1 = (ml + ma) / 2 + 1u;
                int nk = 0;
                while (nk < 12 && (std::uint32_t{1} << (nk + 1)) <= mean1) ++nk;
                kk = std::max(nk, k);
              }
              for (;;) {
                const std::uint32_t uctx = unary_idx(version, mode, qq, 0);
                const std::uint32_t bit = dec.decode_bit(probs.p[uctx]);
                if (bit) break;
                ++qq;
                if (qq > 65536u) {
                  fail(error, "arithmetic magnitude overflow");
                  return false;
                }
              }
              std::uint32_t m = qq << kk;
              for (unsigned i = 0; i < static_cast<unsigned>(kk); ++i) {
                m |= dec.decode_bit(probs.p[rem_idx(version, mode, i)]) << i;
              }
              q = static_cast<std::int32_t>(m) + 1;
              if (sign) q = -q;
              mag_sum += m;
              ++mag_count;
              if (mag_count == 64) {
                const std::uint64_t v = mag_sum / 64;
                int nk2 = 0;
                while (nk2 < 12 && (std::uint64_t{1} << (nk2 + 1)) <= v) ++nk2;
                k = nk2;
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
    for (std::uint32_t i = 0; i < count; ++i) {
      const std::int64_t q = dest[i];
      const std::int64_t value = q * step;
      if (value < std::numeric_limits<std::int32_t>::min() ||
          value > std::numeric_limits<std::int32_t>::max()) {
        fail(error, "arithmetic coefficient overflow");
        return false;
      }
      dest[i] = static_cast<std::int32_t>(value);
    }
    return true;
  }
  for (std::uint32_t y = 0; y < th; ++y) {
    for (std::uint32_t x = 0; x < tw; ++x) {
      const std::int32_t pv = use_parent
          ? parent[static_cast<std::size_t>(y / 2) * parent_stride + x / 2] : 0;
      const unsigned raw_ctx = sig_context(dest, stride, x, y);
      std::uint32_t s;
      if (mode == 11 && use_parent) {
        if (pv == 0) {
          s = dec.decode_bit(probs.p[raw_ctx]);
        } else {
          s = dec.decode_bit(probs.p[sig_idx(version, mode, raw_ctx, false)]);
        }
      } else {
        s = dec.decode_bit(probs.p[sig_idx(version, mode, raw_ctx,
                                           use_parent && pv != 0)]);
      }
      std::int32_t q = 0;
      if (s) {
        const unsigned sign_ctx = sign_idx(version, mode, sign_context(dest, stride, x, y),
                                           use_parent && pv < 0);
        const std::uint32_t sign = dec.decode_bit(probs.p[sign_ctx]);
        std::uint32_t qq = 0;
        int kk = k;
        if (mode == 8 || mode == 9) {
          const std::int32_t lv8 = x > 0 ? dest[static_cast<std::size_t>(y) * stride + x - 1] : 0;
          const std::int32_t av8 = y > 0 ? dest[static_cast<std::size_t>(y - 1) * stride + x] : 0;
          std::uint32_t ml = static_cast<std::uint32_t>(lv8 < 0 ? -lv8 : lv8);
          std::uint32_t ma = static_cast<std::uint32_t>(av8 < 0 ? -av8 : av8);
          const std::uint32_t mean1 = (ml + ma) / 2 + 1u;
          int nk = 0;
          while (nk < 12 && (std::uint32_t{1} << (nk + 1)) <= mean1) ++nk;
          kk = std::max(nk, k);   // band-adapted k is the floor
        }
        for (;;) {
          std::uint32_t uctx;
          if (v2_entropy_layout) {
            uctx = 12u + std::min<std::uint32_t>(qq, 13u);
          } else if (mode == 4 || mode == 6) {
            const std::int32_t lv = x > 0 ? dest[static_cast<std::size_t>(y) * stride + x - 1] : 0;
            const std::int32_t av = y > 0 ? dest[static_cast<std::size_t>(y - 1) * stride + x] : 0;
            const unsigned mclass = std::min(mag_class(lv), mag_class(av));
            uctx = unary_idx(version, mode, qq, mclass);
          } else {
            uctx = unary_idx(version, mode, qq, 0);
          }
          const std::uint32_t bit = dec.decode_bit(probs.p[uctx]);
          if (bit) break;
          ++qq;
          if (qq > 65536u) {
            fail(error, "arithmetic magnitude overflow");
            return false;
          }
        }
        std::uint32_t m = qq << kk;
        for (unsigned i = 0; i < static_cast<unsigned>(kk); ++i) {
          // Historical v2 quirk: ALL remainder bits share one context (26).
          const unsigned rem_context = v2_entropy_layout
              ? 26u
              : rem_idx(version, mode, i);
          m |= dec.decode_bit(probs.p[rem_context]) << i;
        }
        if (mode == 9) {
          const std::int32_t lv9 = x > 0 ? dest[static_cast<std::size_t>(y) * stride + x - 1] : 0;
          const std::int32_t av9 = y > 0 ? dest[static_cast<std::size_t>(y - 1) * stride + x] : 0;
          std::uint32_t ml = static_cast<std::uint32_t>(lv9 < 0 ? -lv9 : lv9);
          std::uint32_t ma = static_cast<std::uint32_t>(av9 < 0 ? -av9 : av9);
          const std::uint32_t mp = std::min(ml, ma);
          const std::uint32_t z = m;
          const std::uint32_t d = (z + (z & 1u)) >> 1;
          const bool neg = (z & 1u) != 0;
          m = neg ? mp - d : mp + d;
        }
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
        const std::int32_t p = (mode == 7) ? gap_predict(dest, stride, x, y)
                                           : median_predict(
                                                 x > 0 ? dest[static_cast<std::size_t>(y) * stride + x - 1] : 0,
                                                 y > 0 ? dest[static_cast<std::size_t>(y - 1) * stride + x] : 0,
                                                 (x > 0 && y > 0) ? dest[static_cast<std::size_t>(y - 1) * stride + x - 1] : 0);
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
  // Dequantize in place: plain q*step (midtread reconstruction), with an
  // optional decode-side damping factor for detail bands (Wiener-style
  // shrinkage; the base band is kept exact since it anchors the image).
  static const double damp = []() {
    const char* e = std::getenv("BRUSHIE_DAMP");
    if (!e) return 1.0;
    const double v = std::atof(e);
    return (v > 0.0 && v <= 1.0) ? v : 1.0;
  }();
  const bool apply_damp = damp < 1.0 && !use_prediction;
  for (std::uint32_t i = 0; i < count; ++i) {
    const std::int64_t q = dest[i];
    double value = static_cast<double>(q) * step;
    if (apply_damp) value *= damp;
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
                               std::uint32_t threads, std::uint32_t requested_target,
                               BandPyramid& pyramid) {
  const std::uint32_t target = requested_target ? requested_target : safe_base_target(w, h);
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

// Catmull-Rom cubic upsample of a half-resolution chroma plane to full
// resolution (sharper than bilinear at equal coded bytes; decoder-side only).
static void upsample_plane(const std::vector<std::int32_t>& in, std::uint32_t w,
                           std::uint32_t h, std::uint32_t ow, std::uint32_t oh,
                           std::vector<std::int32_t>& out) {
  out.resize(static_cast<std::size_t>(ow) * oh);
  auto tap = [&](std::int32_t x, std::int32_t y) -> double {
    const std::int32_t xx = std::max<std::int32_t>(0, std::min<std::int32_t>(static_cast<std::int32_t>(w) - 1, x));
    const std::int32_t yy = std::max<std::int32_t>(0, std::min<std::int32_t>(static_cast<std::int32_t>(h) - 1, y));
    return in[static_cast<std::size_t>(yy) * w + static_cast<std::uint32_t>(xx)];
  };
  for (std::uint32_t yy = 0; yy < oh; ++yy) {
    const double sy = h == 1 ? 0.0 : static_cast<double>(yy) * (h - 1) / (oh - 1);
    const std::int32_t y0 = static_cast<std::int32_t>(sy);
    const double fy = sy - y0;
    const double wy[4] = {
        -0.5 * fy * (1.0 - fy) * (1.0 - fy),
        1.0 - 2.5 * fy * fy + 1.5 * fy * fy * fy,
        0.5 * fy * (1.0 + 4.0 * fy - 3.0 * fy * fy),
        -0.5 * fy * fy * (1.0 - fy)};
    for (std::uint32_t xx = 0; xx < ow; ++xx) {
      const double sx = w == 1 ? 0.0 : static_cast<double>(xx) * (w - 1) / (ow - 1);
      const std::int32_t x0 = static_cast<std::int32_t>(sx);
      const double fx = sx - x0;
      const double wx[4] = {
          -0.5 * fx * (1.0 - fx) * (1.0 - fx),
          1.0 - 2.5 * fx * fx + 1.5 * fx * fx * fx,
          0.5 * fx * (1.0 + 4.0 * fx - 3.0 * fx * fx),
          -0.5 * fx * fx * (1.0 - fx)};
      double acc = 0.0;
      for (int j = 0; j < 4; ++j) {
        double row = 0.0;
        for (int i = 0; i < 4; ++i)
          row += wx[i] * tap(x0 - 1 + i, y0 - 1 + j);
        acc += wy[j] * row;
      }
      out[static_cast<std::size_t>(yy) * ow + xx] =
          static_cast<std::int32_t>(std::llround(acc));
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
                             std::uint64_t) {
  // v5 compact entry (16B): whole-band x/y are zero, payload offsets are
  // cumulative in directory order, coefficient count is w*h, and the
  // checksum is dropped (saves 4B x chunk count on every stream).
  const std::size_t off = out.size();
  out.resize(off + kDirectoryBytes, 0);
  out[off + 0] = static_cast<std::uint8_t>(c.layer);
  out[off + 1] = c.band;
  out[off + 2] = c.channel;
  out[off + 3] = static_cast<std::uint8_t>(c.mode);
  put_u16(out, off + 4, c.w);
  put_u16(out, off + 6, c.h);
  put_u16(out, off + 8, c.step);
  put_u32(out, off + 12, static_cast<std::uint32_t>(c.payload.size()));
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
  encode_band_arith(tmp.data(), w, h, use_prediction, nonzero, abs_sum,
                     nullptr, 0, 3, out);
  return true;
}
static bool test_decode_band(const std::uint8_t* data, std::size_t size,
                             std::uint32_t count, std::uint16_t step,
                             bool use_prediction, std::int32_t* dest,
                             std::uint32_t stride, std::uint32_t tw,
                             std::uint32_t th) {
  (void)0;
  return decode_band_arith(data, size, count, step, use_prediction, dest,
                           stride, tw, th, nullptr, nullptr, 0, false, 3);
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
  if (effective.base_target != 0 &&
      (effective.base_target < 16 || effective.base_target > 128)) {
    fail(error, "base target must be zero or in 16..128");
    return false;
  }
  static const int subsample_q = []() {
    const char* e = std::getenv("BRUSHIE_444_Q");
    return e ? std::atoi(e) : 95;
  }();
  const bool subsample_chroma = effective.quality < subsample_q;
  const bool has_alpha = image.channels == 4;

  std::array<std::vector<std::int32_t>, 4> planes;
  input_planes(image, planes, effective.threads);
  std::uint32_t cw = image.width, ch = image.height;
  if (subsample_chroma) {
    downsample_2x(planes[1], image.width, image.height, cw, ch);
    downsample_2x(planes[2], image.width, image.height, cw, ch);
  }

  BandPyramid pyr0, pyr1, pyr2, pyr3;
  build_band_pyramid(planes[0], image.width, image.height, effective.threads, effective.base_target, pyr0);
  build_band_pyramid(planes[1], cw, ch, effective.threads, effective.base_target, pyr1);
  build_band_pyramid(planes[2], cw, ch, effective.threads, effective.base_target, pyr2);
  if (has_alpha)
    build_band_pyramid(planes[3], image.width, image.height, effective.threads, effective.base_target, pyr3);

  // Collect one band reference per (layer, band, channel) in coarse-to-fine
  // order so that a decoder can stop after any progressive layer. v4 numbers
  // layers coarse-first: layer 1 is the coarsest detail level, which also
  // makes the coarser band always available as an entropy-coding parent.
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
    // v4 keeps the v3 coarse-first layer numbering: layer k is the k-th
    // coarsest detail level (idx = levels - k).
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
  std::vector<std::uint64_t> nz_list(refs.size(), 0);
  std::vector<std::uint64_t> as_list(refs.size(), 0);

  // ---- Closed-loop per-level rate allocation (PCRD-lite) ----------------
  // Raw (unquantized) copies so candidates can be re-quantized freely.
  std::vector<std::vector<std::int32_t>> raw(refs.size());
  for (std::size_t i = 0; i < refs.size(); ++i) raw[i] = *refs[i].data;

  static const int rdo_enabled = []() {
    const char* e = std::getenv("BRUSHIE_RDO");
    return e ? std::atoi(e) : 0;  // metric-calibrated search lost at gates
  }();
  static const std::uint8_t detail_mode = []() {
    const char* e = std::getenv("BRUSHIE_ENTROPY");
    if (!e) return static_cast<std::uint8_t>(8);  // local-k won A/B
    const int v = std::atoi(e);
    return (v == 3 || v == 4 || v == 5 || v == 6 || v == 7 || v == 8 ||
            v == 9 || v == 10 || v == 11 || v == 12)
               ? static_cast<std::uint8_t>(v)
               : static_cast<std::uint8_t>(8);
  }();
  static const bool use_gap = []() {
    const char* e = std::getenv("BRUSHIE_GAP");
    return e && std::atoi(e) == 1;
  }();

  // Parent ref of a detail band: same channel/band, one layer coarser
  // (layer 1's parent is the base band of that channel).
  auto parent_of = [&](std::size_t i) -> std::size_t {
    const BandRef& r = refs[i];
    for (std::size_t j = 0; j < refs.size(); ++j) {
      const BandRef& p = refs[j];
      if (r.layer == 1) {
        // parent is the base band (band 0) of the same channel
        if (p.channel == r.channel && p.layer == 0) return j;
      } else if (p.channel == r.channel && p.band == r.band &&
                 p.layer == r.layer - 1) {
        return j;
      }
    }
    return refs.size();
  };

  std::vector<std::uint16_t> chosen_step(refs.size());
  std::vector<std::uint64_t> band_bytes(refs.size(), 0);
  std::vector<std::uint8_t> band_active(refs.size(), 0);
  std::vector<std::vector<std::int32_t>> quant(refs.size());
  for (std::size_t i = 0; i < refs.size(); ++i) chosen_step[i] = refs[i].step;

  auto encode_state = [&](std::size_t i) {
    const BandRef& r = refs[i];
    const std::int32_t* parent = nullptr;
    std::uint32_t parent_stride = 0;
    if (r.layer > 0) {
      const std::size_t pi = parent_of(i);
      parent = quant[pi].data();
      parent_stride = refs[pi].w;
    }
    std::vector<std::uint8_t> payload;
    encode_band_arith(quant[i].data(), r.w, r.h, r.predict, nz_list[i],
                      as_list[i], parent, parent_stride,
                      r.layer == 0 ? (use_gap ? 7 : 3) : detail_mode, payload);
    band_bytes[i] = payload.size();
    band_active[i] = 1;
    return payload;
  };

  auto quantize_state = [&](std::size_t i) {
    quant[i] = raw[i];
    std::uint64_t nz = 0, as = 0;
    quantize_band(quant[i], chosen_step[i], nz, as);
    nz_list[i] = nz;
    as_list[i] = as;
    band_active[i] = nz != 0 ? 1 : 0;
    band_bytes[i] = 0;
  };

  // Per-group metric sensitivities fitted empirically against the actual
  // windowed MS-SSIM gate (base-luma normalized to 1.0). The windowed metric
  // is ~5000x more sensitive to base-LL error than to the finest chroma
  // detail, so the naive scale-weight table badly mis-allocates bits.
  static const double sens_luma[8] = {1.0,     // layer 0 base
                                      0.1712,  // layer 1 (coarsest detail)
                                      0.0197,  // layer 2
                                      0.0009,  // layer 3
                                      0.00005, 0.00001, 0.000005, 0.000002};
  static const double sens_chroma[8] = {0.2919, 0.0082, 0.0002,
                                        0.00002, 0.000005, 0.000002,
                                        0.000001, 0.0000005};
  auto band_weight = [&](const BandRef& r) -> double {
    const bool chroma = r.channel == 1 || r.channel == 2;
    const bool alpha = r.channel == 3;
    if (alpha) return sens_luma[0];  // alpha follows luma
    const std::uint32_t idx = std::min<std::uint32_t>(r.layer, 7);
    return chroma ? sens_chroma[idx] : sens_luma[idx];
  };
  auto total_distortion = [&]() -> double {
    double d = 0.0;
    for (std::size_t i = 0; i < refs.size(); ++i) {
      const double step = chosen_step[i];
      d += band_weight(refs[i]) * (step * step) *
           static_cast<double>(refs[i].w * refs[i].h) / 12.0;
    }
    return d;
  };
  auto total_bytes = [&]() -> std::uint64_t {
    std::uint64_t t = 0;
    for (std::size_t i = 0; i < refs.size(); ++i) t += band_bytes[i];
    return t;
  };

  // Groups: [base luma(+alpha), base chroma], then per layer [luma, chroma].
  std::vector<std::vector<std::size_t>> groups;
  {
    std::vector<std::size_t> g;
    for (std::size_t i = 0; i < refs.size(); ++i)
      if (refs[i].layer == 0 && refs[i].channel == 0) g.push_back(i);
    if (has_alpha)
      for (std::size_t i = 0; i < refs.size(); ++i)
        if (refs[i].layer == 0 && refs[i].channel == 3) g.push_back(i);
    groups.push_back(g);
    g.clear();
    for (std::size_t i = 0; i < refs.size(); ++i)
      if (refs[i].layer == 0 && (refs[i].channel == 1 || refs[i].channel == 2))
        g.push_back(i);
    groups.push_back(g);
    for (std::uint32_t k = 1; k <= max_layers; ++k) {
      g.clear();
      for (std::size_t i = 0; i < refs.size(); ++i)
        if (refs[i].layer == k && refs[i].channel == 0) g.push_back(i);
      groups.push_back(g);
      g.clear();
      for (std::size_t i = 0; i < refs.size(); ++i)
        if (refs[i].layer == k && (refs[i].channel == 1 || refs[i].channel == 2))
          g.push_back(i);
      groups.push_back(g);
    }
  }

  // Baseline: quantize + encode with the base table.
  for (std::size_t i = 0; i < refs.size(); ++i) {
    quantize_state(i);
    if (band_active[i]) encode_state(i);
  }
  const double d0 = total_distortion();
  const std::uint64_t b0 = total_bytes();

  // BRUSHIE_STEPMUL="g:m,g:m,..." forces per-group step multipliers on the
  // baseline table (bypasses the search) for sensitivity calibration.
  static const bool stepmul_override = []() {
    const char* e = std::getenv("BRUSHIE_STEPMUL");
    return e && *e;
  }();
  if (stepmul_override) {
    const char* e = std::getenv("BRUSHIE_STEPMUL");
    while (e && *e) {
      const int gi = std::atoi(e);
      while (*e && *e != ':') ++e;
      if (*e != ':') break;
      ++e;
      const double mm = std::atof(e);
      while (*e && *e != ',') ++e;
      if (*e == ',') ++e;
      if (gi >= 0 && gi < static_cast<int>(groups.size())) {
        for (std::size_t ri : groups[static_cast<std::size_t>(gi)]) {
          chosen_step[ri] = static_cast<std::uint16_t>(std::min<std::uint32_t>(
              65535, static_cast<std::uint32_t>(std::max<double>(
                         1.0, std::llround(static_cast<double>(refs[ri].step) * mm)))));
        }
      }
    }
    for (std::size_t i = 0; i < refs.size(); ++i) {
      quantize_state(i);
      band_bytes[i] = 0;
      band_active[i] = 0;
    }
    for (std::size_t i = 0; i < refs.size(); ++i)
      if (nz_list[i]) encode_state(i);
  }

  if (rdo_enabled && b0 > 0 && !stepmul_override) {
    // Score is scale-free: relative distortion + lambda * relative bytes.
    const double lambdas[3] = {2.0, 1.0, 0.5};
    std::vector<std::uint16_t> best_steps;
    std::uint64_t best_total = 0;
    double best_dev = 1e9;
    for (double lambda : lambdas) {
      // restore baseline state
      for (std::size_t i = 0; i < refs.size(); ++i) {
        chosen_step[i] = refs[i].step;
        quantize_state(i);
        band_bytes[i] = 0;
        band_active[i] = 0;
      }
      for (std::size_t i = 0; i < refs.size(); ++i)
        if (nz_list[i]) encode_state(i);
      for (const auto& g : groups) {
        if (g.empty()) continue;
        double best_score = 1e300;
        std::vector<std::uint16_t> best_cand_steps;
        std::vector<std::uint64_t> best_bytes;
        std::vector<std::uint8_t> best_act;
        std::vector<std::vector<std::int32_t>> best_quant;
        const double mults[3] = {0.89, 1.0, 1.12};
        for (double mm : mults) {
          std::vector<std::uint16_t> cand_steps = chosen_step;
          for (std::size_t ri : g) {
            const BandRef& r = refs[ri];
            std::uint32_t s = static_cast<std::uint32_t>(std::max<double>(
                1.0, static_cast<double>(std::llround(static_cast<double>(r.step) * mm))));
            s = std::min<std::uint32_t>(s, 65535);
            cand_steps[ri] = static_cast<std::uint16_t>(s);
          }
          // re-quantize + encode the group with candidate steps
          std::vector<std::uint64_t> cand_bytes = band_bytes;
          std::vector<std::uint8_t> cand_act = band_active;
          std::vector<std::vector<std::int32_t>> cand_quant = quant;
          std::vector<std::uint64_t> cand_nz = nz_list;
          std::vector<std::uint64_t> cand_as = as_list;
          for (std::size_t ri : g) {
            cand_quant[ri] = raw[ri];
            std::uint64_t nz = 0, as = 0;
            quantize_band(cand_quant[ri], cand_steps[ri], nz, as);
            cand_nz[ri] = nz;
            cand_as[ri] = as;
            if (nz == 0) {
              cand_act[ri] = 0;
              cand_bytes[ri] = 0;
              continue;
            }
            const BandRef& r = refs[ri];
            const std::int32_t* parent = nullptr;
            std::uint32_t parent_stride = 0;
            if (r.layer > 0) {
              const std::size_t pi = parent_of(ri);
              parent = cand_quant[pi].data();
              parent_stride = refs[pi].w;
            }
            std::vector<std::uint8_t> payload;
            encode_band_arith(cand_quant[ri].data(), r.w, r.h, r.predict, nz, as,
                              parent, parent_stride,
                              r.layer == 0 ? (use_gap ? 7 : 3) : detail_mode,
                              payload);
            cand_bytes[ri] = payload.size();
            cand_act[ri] = 1;
          }
          std::uint64_t total = 0;
          for (std::size_t i = 0; i < refs.size(); ++i) {
            bool in_group = false;
            for (std::size_t ri : g) if (ri == i) { in_group = true; break; }
            total += in_group ? cand_bytes[i] : band_bytes[i];
          }
          double d_cand = 0.0;
          for (std::size_t i = 0; i < refs.size(); ++i) {
            const double step = (std::find(g.begin(), g.end(), i) != g.end())
                                    ? static_cast<double>(cand_steps[i])
                                    : static_cast<double>(chosen_step[i]);
            d_cand += band_weight(refs[i]) * (step * step) *
                      static_cast<double>(refs[i].w * refs[i].h) / 12.0;
          }
          const double score = d_cand / d0 + lambda * static_cast<double>(total) /
                                static_cast<double>(b0);
          if (score < best_score) {
            best_score = score;
            best_cand_steps = cand_steps;
            best_bytes = cand_bytes;
            best_act = cand_act;
            best_quant = cand_quant;
          }
        }
        // commit the group's best candidate
        chosen_step = best_cand_steps;
        band_bytes = best_bytes;
        band_active = best_act;
        quant = best_quant;
        for (std::size_t ri : g) {
          nz_list[ri] = 0;
          as_list[ri] = 0;
          if (band_active[ri]) {
            // recompute nz/as for the committed quantized array
            for (const std::int32_t v : quant[ri])
              if (v != 0) {
                ++nz_list[ri];
                as_list[ri] += static_cast<std::uint64_t>(v < 0 ? -v : v);
              }
          }
        }
      }
      const std::uint64_t total = total_bytes();
      // Keep the lambda pass whose byte total is closest to the baseline
      // (preferring to stay at or under it): the metric-calibrated weights
      // then re-distribute the same budget to where MS-SSIM is sensitive.
      const double dev = static_cast<double>(total) / static_cast<double>(b0) - 1.0;
      if (best_steps.empty() || std::abs(dev) < std::abs(best_dev)) {
        best_dev = dev;
        best_total = total;
        best_steps = chosen_step;
      }
    }
    if (!best_steps.empty()) {
      chosen_step = best_steps;
      // final state: quantize + encode everything with chosen steps
      for (std::size_t i = 0; i < refs.size(); ++i) {
        quantize_state(i);
        band_bytes[i] = 0;
        band_active[i] = 0;
      }
      for (std::size_t i = 0; i < refs.size(); ++i)
        if (nz_list[i]) encode_state(i);
    }
  }

  std::atomic<std::uint64_t> nonzero{0};
  for (std::size_t i = 0; i < refs.size(); ++i) {
    if (!band_active[i]) continue;
    Chunk& c = chunks[i];
    c.layer = refs[i].layer;
    c.band = refs[i].band;
    c.channel = refs[i].channel;
    c.x = 0;
    c.y = 0;
    c.w = static_cast<std::uint16_t>(refs[i].w);
    c.h = static_cast<std::uint16_t>(refs[i].h);
    c.step = chosen_step[i];
    c.mode = refs[i].layer == 0 ? (use_gap ? 7 : 3) : detail_mode;
    // Per-band block mode: 16x16 block flags pay off when the band is
    // block-sparse (synthetic/flat content), and are pure overhead on dense
    // photo bands.
    if (c.mode != 3 && c.mode != 7 && refs[i].w >= 16 && refs[i].h >= 16) {
      static const std::uint32_t kB = []() {
        const char* e = std::getenv("BRUSHIE_BLOCK");
        if (!e) return 16u;
        const int v = std::atoi(e);
        return (v == 8 || v == 32 || v == 64) ? static_cast<std::uint32_t>(v) : 16u;
      }();
      const std::uint32_t bw = (refs[i].w + kB - 1) / kB;
      const std::uint32_t bh = (refs[i].h + kB - 1) / kB;
      std::uint32_t nz_blocks = 0;
      for (std::uint32_t by = 0; by < bh; ++by) {
        for (std::uint32_t bx = 0; bx < bw; ++bx) {
          bool nz = false;
          for (std::uint32_t yy = by * kB; yy < std::min(refs[i].h, (by + 1) * kB) && !nz; ++yy)
            for (std::uint32_t xx = bx * kB; xx < std::min(refs[i].w, (bx + 1) * kB); ++xx)
              if (quant[i][static_cast<std::size_t>(yy) * refs[i].w + xx] != 0) {
                nz = true;
                break;
              }
          if (nz) ++nz_blocks;
        }
      }
      if (nz_blocks * 10 <= bw * bh * 5) c.mode = 12;
    }
    c.count = refs[i].w * refs[i].h;
    // Re-encode the committed quantized state into the chunk payload.
    std::vector<std::uint8_t> payload;
    const BandRef& r = refs[i];
    const std::int32_t* parent = nullptr;
    std::uint32_t parent_stride = 0;
    if (r.layer > 0) {
      const std::size_t pi = parent_of(i);
      parent = quant[pi].data();
      parent_stride = refs[pi].w;
    }
    encode_band_arith(quant[i].data(), r.w, r.h, r.predict, nz_list[i],
                      as_list[i], parent, parent_stride, c.mode, payload);
    c.payload = std::move(payload);
    active[i] = 1;
    nonzero += nz_list[i];
  }

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
      dir_bytes != static_cast<std::uint64_t>(chunk_count) * kDirectoryBytesLegacy ||
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
  std::uint64_t expected_payload_offset = data_offset;
  for (std::uint32_t ci = 0; ci < chunk_count; ++ci) {
    const std::uint8_t* d = data + kHeaderBytes + static_cast<std::size_t>(ci) * kDirectoryBytesLegacy;
    const std::uint16_t layer = get_u16(d + 0);
    const std::uint8_t band = d[2], channel = d[3];
    const std::uint32_t x = get_u32(d + 4), y = get_u32(d + 8);
    const std::uint32_t tw = get_u16(d + 12), th = get_u16(d + 14), step = get_u16(d + 16);
    const std::uint16_t mode = get_u16(d + 18);
    const std::uint64_t offset = get_u64(d + 20);
    const std::uint32_t payload_size = get_u32(d + 28), count = get_u32(d + 32);
    const std::uint32_t checksum = get_u32(d + 36);
    if (offset != expected_payload_offset ||
        offset > std::numeric_limits<std::uint64_t>::max() - payload_size) {
      fail(error, "invalid CAPS payload layout");
      return false;
    }
    expected_payload_offset = offset + payload_size;
    if (channel >= 3 || band > 3 || mode > 2 || step == 0 || tw == 0 || th == 0 ||
        tw > tile || th > tile || count != static_cast<std::uint64_t>(tw) * th ||
        layer > levels) {
      fail(error, "invalid CAPS chunk metadata");
      return false;
    }
    if (layer > static_cast<std::uint16_t>(std::max(0, highest_layer))) continue;
    if (offset < data_offset || offset > size || payload_size > size - offset) {
      fail(error, "truncated selected-layer payload");
      return false;
    }
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
  if (highest_layer >= static_cast<int>(levels) && expected_payload_offset != size) {
    fail(error, "trailing or missing CAPS payload bytes");
    return false;
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
  const std::uint16_t version = get_u16(data + 4);
  const std::size_t directory_entry_bytes =
      version >= 5 ? kDirectoryBytes : (version >= 3 ? 20u : kDirectoryBytesLegacy);
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
      dir_bytes != static_cast<std::uint64_t>(chunk_count) * directory_entry_bytes ||
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
    // Chroma level count is self-described by its stored base dimensions.
    // Do not re-run the encoder's historical target heuristic: v2 streams
    // may select a 32- or 64-base mode independently per image.
    std::uint32_t w = c_in_w, h = c_in_h;
    while (w != c_base_w || h != c_base_h) {
      if (w <= 1 || h <= 1 || shapes1.size() >= 16) {
        fail(error, "chroma base dimensions do not match pyramid");
        return false;
      }
      BandLevel lev;
      lev.w = w;
      lev.h = h;
      lev.lw = (w + 1) / 2;
      lev.lh = (h + 1) / 2;
      if (lev.lw < c_base_w || lev.lh < c_base_h) {
        fail(error, "chroma base dimensions do not match pyramid");
        return false;
      }
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
  std::uint64_t expected_payload_offset = data_offset;
  for (std::uint32_t ci = 0; ci < chunk_count; ++ci) {
    const std::uint8_t* d = data + kHeaderBytes + static_cast<std::size_t>(ci) * directory_entry_bytes;
    std::uint16_t layer = 0, mode = 0, step = 0;
    std::uint8_t band = 0, channel = 0;
    std::uint32_t x = 0, y = 0, tw = 0, th = 0, payload_size = 0,
                  count = 0, checksum = 0;
    std::uint64_t offset = expected_payload_offset;
    if (version >= 3) {
      layer = d[0];
      band = d[1];
      channel = d[2];
      mode = d[3];
      tw = get_u16(d + 4);
      th = get_u16(d + 6);
      step = get_u16(d + 8);
      payload_size = get_u32(d + 12);
      checksum = version >= 5 ? 0 : get_u32(d + 16);
      count = tw * th;
    } else {
      layer = get_u16(d + 0);
      band = d[2];
      channel = d[3];
      x = get_u32(d + 4);
      y = get_u32(d + 8);
      tw = get_u16(d + 12);
      th = get_u16(d + 14);
      step = get_u16(d + 16);
      mode = get_u16(d + 18);
      offset = get_u64(d + 20);
      payload_size = get_u32(d + 28);
      count = get_u32(d + 32);
      checksum = get_u32(d + 36);
    }
    if (offset != expected_payload_offset ||
        offset > std::numeric_limits<std::uint64_t>::max() - payload_size) {
      fail(error, "invalid CAPS payload layout");
      return false;
    }
    expected_payload_offset = offset + payload_size;
    if (channel >= channels || band > 3 || mode < 3 || mode > 12 || step == 0 || tw == 0 || th == 0 ||
        count != static_cast<std::uint64_t>(tw) * th || layer > levels) {
      fail(error, "invalid CAPS chunk metadata");
      return false;
    }
    // A progressive physical prefix contains the complete header+directory
    // but only payloads through the requested layer. Later-layer payload
    // offsets can legitimately lie beyond `size`; skip before validating them.
    if (layer > static_cast<std::uint16_t>(std::max(0, highest_layer))) continue;
    if (offset < data_offset || offset > size || payload_size > size - offset) {
      fail(error, "truncated selected-layer payload");
      return false;
    }
    std::vector<BandLevel>& shapes =
        channel == 0 ? shapes0 : (channel == 1 ? shapes1 : (channel == 2 ? shapes2 : shapes3));
    std::int32_t* destination = nullptr;
    std::uint32_t stride = 0, band_w = 0, band_h = 0;
    if (layer == 0) {
      // Channel 3 (alpha) is coded at full resolution like channel 0.
      const std::uint32_t b_w = channel == 0 || channel == 3 ? base_w : ch_base_w;
      const std::uint32_t b_h = channel == 0 || channel == 3 ? base_h : ch_base_h;
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
      // v3/v4 both number layers coarse-first: layer k is the k-th coarsest
      // detail level (idx = levels - layer).
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
    if (version < 5 && fnv1a(payload, payload_size) != checksum) {
      fail(error, "coefficient chunk checksum mismatch");
      return false;
    }
    const std::int32_t* parent = nullptr;
    std::uint32_t parent_stride = 0;
    if (mode != 3 && layer > 0) {
      if (layer == 1) {
        parent = base[channel].data();
        parent_stride = (channel == 0 || channel == 3) ? base_w : ch_base_w;
      } else {
        BandLevel& pl = shapes[static_cast<std::size_t>(shapes.size()) -
                               (static_cast<std::size_t>(layer) - 1)];
        parent = pl.detail[band - 1].data();
        parent_stride = band == 1 ? pl.w / 2 : (band == 2 ? pl.lw : pl.w / 2);
      }
    }
    {
      std::string derr;
      if (!decode_band_arith(payload, payload_size, count, step, band == 0,
                             destination, stride, tw, th, &derr, parent,
                             parent_stride, version == kVersionBandV2, mode,
                             version)) {
        if (error) *error = derr;
        return false;
      }
    }
  }

  if (highest_layer >= static_cast<int>(levels) && expected_payload_offset != size) {
    fail(error, "trailing or missing CAPS payload bytes");
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
  if (version == kVersion || version == 4 || version == 3 || version == kVersionBandV2)
    return decode_v2(data, size, output_width, output_height, rgb,
                     max_progressive_layer, error);
  if (version == kVersionLegacy) return decode_v1(data, size, output_width, output_height, rgb, max_progressive_layer, error);
  fail(error, "unsupported CAPS stream version");
  return false;
}

}  // namespace brushie
