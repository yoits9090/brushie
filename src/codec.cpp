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

constexpr std::uint16_t kVersion = 1;
constexpr std::size_t kHeaderBytes = 64;
constexpr std::size_t kDirectoryBytes = 40;
constexpr std::uint32_t kMaxDimension = 16384;
constexpr std::uint32_t kMaxChunks = 4'000'000;
constexpr std::uint32_t kMaxTile = 128;

struct Level {
  std::uint32_t w = 0, h = 0, lw = 0, lh = 0;
  // detail[channel][band], band 0 = horizontal, 1 = vertical, 2 = diagonal.
  std::array<std::array<std::vector<std::int32_t>, 3>, 3> detail;
};

struct Pyramid {
  std::uint32_t width = 0, height = 0;
  std::uint32_t base_width = 0, base_height = 0;
  std::vector<Level> levels;  // finest to coarsest
  std::array<std::vector<std::int32_t>, 3> base;
};

struct Chunk {
  std::uint16_t layer = 0;
  std::uint8_t band = 0;
  std::uint8_t channel = 0;
  std::uint32_t x = 0, y = 0;
  std::uint16_t w = 0, h = 0, step = 1;
  std::uint16_t mode = 0;  // 0=absolute, 1=delta, 2=significance-mask bitpack
  std::vector<std::uint8_t> payload;
  std::uint32_t count = 0;
  std::uint32_t checksum = 0;
};

static void fail(std::string* error, const char* message) {
  if (error) *error = message;
}

static std::int64_t floor_div(std::int64_t a, std::int64_t b) {
  if (b <= 0) return 0;
  if (a >= 0) return a / b;
  return -(((-a) + b - 1) / b);
}

static std::int32_t round_div(std::int32_t a, std::int32_t b) {
  if (a >= 0) return static_cast<std::int32_t>((a + b / 2) / b);
  return static_cast<std::int32_t>(-((-a + b / 2) / b));
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

static void extract_detail(const std::vector<std::int32_t>& packed,
                           const Level& level,
                           std::array<std::vector<std::int32_t>, 3>& out) {
  const std::uint32_t hw = level.w / 2;
  const std::uint32_t hh = level.h / 2;
  out[0].resize(static_cast<std::size_t>(hw) * level.lh);
  out[1].resize(static_cast<std::size_t>(level.lw) * hh);
  out[2].resize(static_cast<std::size_t>(hw) * hh);
  for (std::uint32_t y = 0; y < level.lh; ++y) {
    for (std::uint32_t x = 0; x < hw; ++x)
      out[0][static_cast<std::size_t>(y) * hw + x] = packed[static_cast<std::size_t>(y) * level.w + level.lw + x];
  }
  for (std::uint32_t y = 0; y < hh; ++y) {
    for (std::uint32_t x = 0; x < level.lw; ++x)
      out[1][static_cast<std::size_t>(y) * level.lw + x] = packed[static_cast<std::size_t>(level.lh + y) * level.w + x];
  }
  for (std::uint32_t y = 0; y < hh; ++y) {
    for (std::uint32_t x = 0; x < hw; ++x)
      out[2][static_cast<std::size_t>(y) * hw + x] = packed[static_cast<std::size_t>(level.lh + y) * level.w + level.lw + x];
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
  std::vector<std::int32_t> vertical(static_cast<std::size_t>(w) * h);
  parallel_for(w, threads, [&](std::size_t xx) {
    std::vector<std::int32_t> line(h), restored(h);
    for (std::uint32_t y = 0; y < h; ++y) line[y] = packed[y * w + xx];
    inverse_line(line.data(), restored.data(), h);
    for (std::uint32_t y = 0; y < h; ++y) vertical[y * w + xx] = restored[y];
  });
  output.resize(static_cast<std::size_t>(w) * h);
  parallel_for(h, threads, [&](std::size_t yy) {
    inverse_line(vertical.data() + yy * w, output.data() + yy * w, w);
  });
}

static void input_planes(const ImageView& image,
                         std::array<std::vector<std::int32_t>, 3>& planes,
                         std::uint32_t threads) {
  const std::size_t stride = image.stride ? image.stride : static_cast<std::size_t>(image.width) * 3;
  const std::size_t count = static_cast<std::size_t>(image.width) * image.height;
  for (auto& p : planes) p.resize(count);
  parallel_for(image.height, threads, [&](std::size_t yy) {
    const std::uint8_t* row = image.rgb + yy * stride;
    for (std::uint32_t x = 0; x < image.width; ++x) {
      const int r = row[x * 3 + 0];
      const int g = row[x * 3 + 1];
      const int b = row[x * 3 + 2];
      const int co = r - b;
      const int t = b + static_cast<int>(floor_div(co, 2));
      const int cg = g - t;
      const int y = t + static_cast<int>(floor_div(cg, 2));
      const std::size_t i = yy * image.width + x;
      planes[0][i] = y;
      planes[1][i] = co;
      planes[2][i] = cg;
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

static void append_uvar(std::vector<std::uint8_t>& out, std::uint64_t value) {
  while (value >= 0x80) {
    out.push_back(static_cast<std::uint8_t>(value) | 0x80);
    value >>= 7;
  }
  out.push_back(static_cast<std::uint8_t>(value));
}

static std::uint64_t zigzag(std::int64_t value) {
  return (static_cast<std::uint64_t>(value) << 1) ^ static_cast<std::uint64_t>(value >> 63);
}

static std::int64_t unzigzag(std::uint64_t value) {
  return static_cast<std::int64_t>(value >> 1) ^ -static_cast<std::int64_t>(value & 1);
}

static std::uint16_t quant_step(std::uint8_t quality, std::uint32_t detail_level,
                                bool base, std::uint8_t channel = 0) {
  const std::uint32_t loss = 100u - std::min<std::uint8_t>(quality, 100);
  if (loss == 0) return 1;
  // A dead-zone of roughly four integer units at the default quality removes
  // the dense low-amplitude lifting noise that otherwise defeats entropy
  // coding on smooth gradients. Quality 100 remains exactly lossless.
  const std::uint32_t root = std::max<std::uint32_t>(1, 1 + loss / 6);
  // Chroma is deliberately quantized more coarsely at lossy operating
  // points.  This is an analytical rate allocation, not an optimizer: the
  // reversible transform keeps the luma channel at full priority while the
  // two chroma channels use a bounded 2x step.
  const std::uint32_t chroma_root = channel == 0 ? root : std::min<std::uint32_t>(65535, root * 2);
  if (base) return static_cast<std::uint16_t>(chroma_root);
  const std::uint32_t scale = 1u << std::min<std::uint32_t>(4, detail_level / 2);
  return static_cast<std::uint16_t>(std::min<std::uint32_t>(65535, chroma_root * scale));
}

static unsigned signed_bits(std::int32_t value) {
  for (unsigned bits = 1; bits <= 32; ++bits) {
    if (bits == 32) return 32;
    const std::int64_t limit = static_cast<std::int64_t>(1) << (bits - 1);
    if (value >= -limit && value < limit) return bits;
  }
  return 32;
}

static std::vector<std::uint8_t> encode_tile(const std::int32_t* values,
                                             std::uint32_t stride,
                                             std::uint32_t w, std::uint32_t h,
                                             std::uint16_t step,
                                             std::uint16_t& mode,
                                             std::uint64_t& nonzero) {
  const std::size_t count = static_cast<std::size_t>(w) * h;
  std::vector<std::int32_t> q(count);
  std::size_t tile_nonzero = 0;
  unsigned bits = 1;
  // Pass 1: quantize once and collect the metadata needed by all coders.
  for (std::uint32_t y = 0; y < h; ++y) {
    for (std::uint32_t x = 0; x < w; ++x) {
      const std::int32_t value = round_div(values[static_cast<std::size_t>(y) * stride + x], step);
      q[static_cast<std::size_t>(y) * w + x] = value;
      if (value != 0) { ++tile_nonzero; bits = std::max(bits, signed_bits(value)); }
    }
  }
  // Coefficient planes are initialized to zero by the decoder, so an
  // all-zero tile needs neither a directory entry nor a one-byte terminal
  // run. The caller omits empty payloads from the stream.
  if (tile_nonzero == 0) {
    mode = 0;
    return {};
  }
  std::vector<std::uint8_t> absolute, delta;
  absolute.reserve(count / 2 + 8); delta.reserve(count / 2 + 8);
  std::uint32_t run_absolute = 0, run_delta = 0;
  std::int32_t previous = 0;
  const std::size_t mask_bytes = (count + 7) / 8;
  const std::size_t value_bytes = (tile_nonzero * bits + 7) / 8;
  std::vector<std::uint8_t> bitpack(1 + mask_bytes + value_bytes, 0);
  bitpack[0] = static_cast<std::uint8_t>(bits);
  std::size_t bit = 0;
  const std::uint64_t bit_mask = bits == 32 ? 0xffffffffull : ((1ull << bits) - 1);
  // Pass 2: emit all candidates while walking the quantized tile once.
  for (std::size_t i = 0; i < count; ++i) {
    const std::int32_t value = q[i];
    if (value == 0) { ++run_absolute; ++run_delta; continue; }
    append_uvar(absolute, run_absolute);
    append_uvar(absolute, zigzag(value) + 1);
    append_uvar(delta, run_delta);
    append_uvar(delta, zigzag(value - previous) + 1);
    run_absolute = run_delta = 0;
    previous = value;
    bitpack[1 + i / 8] |= static_cast<std::uint8_t>(1u << (i & 7));
    const std::uint64_t raw = static_cast<std::uint64_t>(static_cast<std::int64_t>(value)) & bit_mask;
    for (unsigned b = 0; b < bits; ++b)
      if (raw & (1ull << b)) bitpack[1 + mask_bytes + (bit + b) / 8] |=
          static_cast<std::uint8_t>(1u << ((bit + b) & 7));
    bit += bits;
  }
  append_uvar(absolute, run_absolute);
  append_uvar(delta, run_delta);
  mode = 0;
  const std::vector<std::uint8_t>* selected = &absolute;
  if (delta.size() < selected->size()) { mode = 1; selected = &delta; }
  if (bitpack.size() < selected->size()) { mode = 2; selected = &bitpack; }
  nonzero += tile_nonzero;
  return *selected;
}

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
  // Coarse monotone operating points calibrated from the frozen Kodak/DIV2K
  // sweep. This intentionally avoids trial encodes and therefore reports the
  // actual byte count instead of claiming exact rate control.
  if (ratio >= 0.80) return 100;
  if (ratio >= 0.60) return 90;
  if (ratio >= 0.28) return 82;
  if (ratio >= 0.21) return 70;
  if (ratio >= 0.15) return 50;
  if (ratio >= 0.10) return 35;
  return 20;
}

static std::uint8_t quality_for_target_lpips(double target) {
  if (!(target > 0.0)) return 100;
  // LPIPS is intentionally not linked into the deterministic core. These
  // conservative mappings expose the requested interface while the reports
  // mark the quality as unverified until an external LPIPS audit is supplied.
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

static bool build_pyramid(const ImageView& image, const EncodeOptions& options,
                          Pyramid& pyramid, std::string* error) {
  if (!image.rgb || image.width == 0 || image.height == 0 ||
      image.width > kMaxDimension || image.height > kMaxDimension) {
    fail(error, "invalid image dimensions or null RGB pointer");
    return false;
  }
  std::array<std::vector<std::int32_t>, 3> planes;
  input_planes(image, planes, options.threads);
  const std::uint32_t target = safe_base_target(image.width, image.height);
  std::uint32_t w = image.width, h = image.height;
  while (std::min(w, h) > target && w > 1 && h > 1) {
    Level level;
    level.w = w;
    level.h = h;
    level.lw = (w + 1) / 2;
    level.lh = (h + 1) / 2;
    for (int c = 0; c < 3; ++c) {
      std::vector<std::int32_t> work(planes[c]);
      parallel_for(h, options.threads, [&](std::size_t yy) {
        std::vector<std::int32_t> line(w), packed(w);
        std::copy_n(work.data() + yy * w, w, line.data());
        forward_line(line.data(), packed.data(), w);
        std::copy_n(packed.data(), w, work.data() + yy * w);
      });
      parallel_for(w, options.threads, [&](std::size_t xx) {
        std::vector<std::int32_t> line(h), packed(h);
        for (std::uint32_t y = 0; y < h; ++y) line[y] = work[y * w + xx];
        forward_line(line.data(), packed.data(), h);
        for (std::uint32_t y = 0; y < h; ++y) work[y * w + xx] = packed[y];
      });
      std::array<std::vector<std::int32_t>, 3> details;
      extract_detail(work, level, details);
      level.detail[c] = std::move(details);
      planes[c].assign(static_cast<std::size_t>(level.lw) * level.lh, 0);
      for (std::uint32_t y = 0; y < level.lh; ++y) {
        std::copy_n(work.data() + static_cast<std::size_t>(y) * w,
                    level.lw, planes[c].data() + static_cast<std::size_t>(y) * level.lw);
      }
    }
    pyramid.levels.push_back(std::move(level));
    w = (w + 1) / 2;
    h = (h + 1) / 2;
  }
  pyramid.width = image.width;
  pyramid.height = image.height;
  pyramid.base_width = w;
  pyramid.base_height = h;
  pyramid.base = std::move(planes);
  return true;
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

static void append_band_chunks(const std::vector<std::int32_t>& band,
                               std::uint32_t width, std::uint32_t height,
                               std::uint16_t layer, std::uint8_t band_id,
                               std::uint8_t channel, std::uint16_t step,
                               std::uint32_t tile, std::vector<Chunk>& chunks,
                               std::uint64_t& nonzero) {
  for (std::uint32_t y = 0; y < height; y += tile) {
    for (std::uint32_t x = 0; x < width; x += tile) {
      const std::uint32_t tw = std::min(tile, width - x);
      const std::uint32_t th = std::min(tile, height - y);
      Chunk chunk;
      chunk.layer = layer;
      chunk.band = band_id;
      chunk.channel = channel;
      chunk.x = x;
      chunk.y = y;
      chunk.w = static_cast<std::uint16_t>(tw);
      chunk.h = static_cast<std::uint16_t>(th);
      chunk.step = step;
      chunk.count = tw * th;
      // Encode directly from the pyramid band. Avoiding a tile copy keeps the
      // post-pyramid work to the two candidate-selection traversals.
      chunk.payload = encode_tile(band.data() + static_cast<std::size_t>(y) * width + x,
                                   width, tw, th, step, chunk.mode, nonzero);
      if (chunk.payload.empty()) continue;
      chunk.checksum = fnv1a(chunk.payload.data(), chunk.payload.size());
      chunks.push_back(std::move(chunk));
    }
  }
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
    const std::int64_t symbol = unzigzag(code - 1);
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

static std::uint8_t clamp_u8(std::int64_t value) {
  return static_cast<std::uint8_t>(std::max<std::int64_t>(0, std::min<std::int64_t>(255, value)));
}

static void output_rgb(const std::array<std::vector<std::int32_t>, 3>& planes,
                       std::uint32_t w, std::uint32_t h,
                       std::uint32_t out_w, std::uint32_t out_h,
                       std::vector<std::uint8_t>& rgb,
                       std::uint32_t threads) {
  rgb.resize(static_cast<std::size_t>(out_w) * out_h * 3);
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
        const std::size_t i = (yy * out_w + xx) * 3;
        rgb[i + 0] = clamp_u8(r);
        rgb[i + 1] = clamp_u8(g);
        rgb[i + 2] = clamp_u8(b);
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
      for (int k = 0; k < 3; ++k) {
        const auto& p = planes[k];
        const double a = p[static_cast<std::size_t>(y0) * w + x0] * (1.0 - fx) +
                         p[static_cast<std::size_t>(y0) * w + x1] * fx;
        const double b = p[static_cast<std::size_t>(y1) * w + x0] * (1.0 - fx) +
                         p[static_cast<std::size_t>(y1) * w + x1] * fx;
        c[k] = static_cast<std::int32_t>(std::llround(a * (1.0 - fy) + b * fy));
      }
      const std::int64_t yv = c[0], co = c[1], cg = c[2];
      const std::int64_t t = yv - floor_div(cg, 2);
      const std::int64_t g = cg + t;
      const std::int64_t b = t - floor_div(co, 2);
      const std::int64_t r = co + b;
      const std::size_t i = (yy * out_w + xx) * 3;
      rgb[i + 0] = clamp_u8(r);
      rgb[i + 1] = clamp_u8(g);
      rgb[i + 2] = clamp_u8(b);
    }
  });
}

}  // namespace

bool encode(const ImageView& image, const EncodeOptions& options,
            EncodedImage& output, std::string* error) {
  EncodeOptions effective = options;
  if (!image.rgb || image.width == 0 || image.height == 0) {
    fail(error, "invalid image dimensions or null RGB pointer");
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
  Pyramid pyramid;
  if (!build_pyramid(image, effective, pyramid, error)) return false;
  std::vector<Chunk> chunks;
  std::uint64_t nonzero = 0;
  // Layer 0 is always the coarse LL base.
  for (std::uint8_t c = 0; c < 3; ++c) {
    append_band_chunks(pyramid.base[c], pyramid.base_width, pyramid.base_height,
                       0, 0, c, quant_step(effective.quality, 0, true, c),
                       effective.tile_size, chunks, nonzero);
  }
  // Coarse-to-fine layer order, while pyramid storage is finest-to-coarsest.
  std::uint16_t layer = 1;
  for (std::size_t ri = pyramid.levels.size(); ri-- > 0;) {
    const Level& lev = pyramid.levels[ri];
    for (std::uint8_t c = 0; c < 3; ++c) {
      const std::uint16_t step = quant_step(effective.quality,
                                            static_cast<std::uint32_t>(ri), false, c);
      append_band_chunks(lev.detail[c][0], lev.w / 2, lev.lh, layer, 1, c,
                         step, effective.tile_size, chunks, nonzero);
      append_band_chunks(lev.detail[c][1], lev.lw, lev.h / 2, layer, 2, c,
                         step, effective.tile_size, chunks, nonzero);
      append_band_chunks(lev.detail[c][2], lev.w / 2, lev.h / 2, layer, 3, c,
                         step, effective.tile_size, chunks, nonzero);
    }
    ++layer;
  }
  if (chunks.size() > kMaxChunks) {
    fail(error, "too many coefficient chunks");
    return false;
  }
  const std::uint64_t directory_bytes = static_cast<std::uint64_t>(chunks.size()) * kDirectoryBytes;
  const std::uint64_t data_offset = kHeaderBytes + directory_bytes;
  if (data_offset > std::numeric_limits<std::uint32_t>::max() * 16ull) {
    fail(error, "encoded stream is too large");
    return false;
  }
  output.bytes.assign(kHeaderBytes, 0);
  output.bytes[0] = 'C'; output.bytes[1] = 'A'; output.bytes[2] = 'P'; output.bytes[3] = 'S';
  put_u16(output.bytes, 4, kVersion);
  put_u16(output.bytes, 6, 0);
  put_u32(output.bytes, 8, image.width);
  put_u32(output.bytes, 12, image.height);
  put_u16(output.bytes, 16, static_cast<std::uint16_t>(pyramid.levels.size()));
  put_u16(output.bytes, 18, static_cast<std::uint16_t>(effective.tile_size));
  output.bytes[20] = effective.quality;
  output.bytes[21] = 3;
  put_u32(output.bytes, 24, pyramid.base_width);
  put_u32(output.bytes, 28, pyramid.base_height);
  put_u32(output.bytes, 32, static_cast<std::uint32_t>(chunks.size()));
  put_u32(output.bytes, 36, static_cast<std::uint32_t>(directory_bytes));
  put_u64(output.bytes, 40, data_offset);
  put_u64(output.bytes, 48, 0);
  put_u32(output.bytes, 56, fnv1a(output.bytes.data(), 56));
  put_u32(output.bytes, 60, 0);
  std::uint64_t payload_offset = data_offset;
  for (const Chunk& chunk : chunks) {
    append_directory(output.bytes, chunk, payload_offset);
    payload_offset += chunk.payload.size();
  }
  for (const Chunk& chunk : chunks)
    output.bytes.insert(output.bytes.end(), chunk.payload.begin(), chunk.payload.end());
  output.stats.input_bytes = static_cast<std::uint64_t>(image.width) * image.height * 3;
  output.stats.encoded_bytes = output.bytes.size();
  output.stats.nonzero_coefficients = nonzero;
  output.stats.pyramid_levels = static_cast<std::uint32_t>(pyramid.levels.size());
  output.stats.chunks = static_cast<std::uint32_t>(chunks.size());
  output.stats.base_width = pyramid.base_width;
  output.stats.base_height = pyramid.base_height;
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
  if (std::memcmp(data, "CAPS", 4) != 0 || get_u16(data + 4) != kVersion) {
    fail(error, "unsupported CAPS stream");
    return false;
  }
  if (get_u32(data + 56) != fnv1a(data, 56)) {
    fail(error, "header checksum mismatch");
    return false;
  }
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
  std::vector<Level> shape(levels);
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
        layer > levels || layer > static_cast<std::uint16_t>(std::max(0, highest_layer))) {
      if (layer > static_cast<std::uint16_t>(std::max(0, highest_layer))) continue;
      fail(error, "invalid CAPS chunk metadata");
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
      const Level& lev = shape[idx];
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
    const Level& lev = shape[ri];
    std::array<std::vector<std::int32_t>, 3> next;
    for (int c = 0; c < 3; ++c)
      inverse_level(reconstructed[c], lev.detail[c], lev.w, lev.h, next[c], 8);
    reconstructed = std::move(next);
    cur_w = lev.w; cur_h = lev.h;
    if (cur_w >= output_width && cur_h >= output_height) break;
  }
  // If the requested output is larger than the source, all levels were used.
  if (cur_w < output_width || cur_h < output_height) {
    for (std::size_t ri = 0; ri < levels; ++ri) {
      // The loop above reaches the source dimensions for any legal source.
      // This branch only documents the invariant and cannot execute.
      (void)ri;
    }
  }
  output_rgb(reconstructed, cur_w, cur_h, output_width, output_height, rgb, 8);
  return true;
}

}  // namespace brushie
