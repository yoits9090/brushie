#include "brushie/codec.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

int main() {
  for (const auto dims : {std::pair<unsigned, unsigned>{1, 1}, {17, 19}, {65, 73}, {256, 192}, {512, 512}}) {
    const unsigned w = dims.first, h = dims.second;
    std::vector<std::uint8_t> src(static_cast<std::size_t>(w) * h * 3);
    for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x) {
      const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 3;
      src[i] = static_cast<std::uint8_t>((x * 17 + y * 3) & 255);
      src[i + 1] = static_cast<std::uint8_t>((x * 7 + y * 13) & 255);
      src[i + 2] = static_cast<std::uint8_t>(((x ^ y) * 11) & 255);
    }
    brushie::EncodedImage encoded;
    brushie::EncodeOptions options;
    options.quality = 100;
    options.threads = 4;
    std::string error;
    assert(brushie::encode({src.data(), w, h, 0}, options, encoded, &error));
    std::vector<std::uint8_t> dst;
    assert(brushie::decode(encoded.bytes.data(), encoded.bytes.size(), w, h, dst, -1, &error));
    if (src != dst) {
      std::cerr << "lossless mismatch " << w << "x" << h << " bytes " << src.size() << " " << dst.size() << "\n";
      std::size_t nbad = 0;
      for (std::size_t i = 0; i < src.size(); ++i) if (src[i] != dst[i]) { if (nbad++ < 20) std::cerr << "diff " << i << " " << (int)src[i] << " " << (int)dst[i] << "\n"; }
      std::cerr << "nbad=" << nbad << "\n";
      return 1;
    }
    std::vector<std::uint8_t> scaled;
    assert(brushie::decode(encoded.bytes.data(), encoded.bytes.size(), 31, 23, scaled, 0, &error));
    assert(scaled.size() == 31u * 23u * 3u);
    auto corrupt = encoded.bytes;
    corrupt[0] = 'X';
    assert(!brushie::decode(corrupt.data(), corrupt.size(), w, h, scaled, -1, &error));
  }
  {
    const unsigned w = 512, h = 512;
    std::vector<std::uint8_t> src(static_cast<std::size_t>(w) * h * 3, 127);
    brushie::EncodeOptions options;
    options.target_bytes = 250000;
    options.threads = 4;
    options.adaptive_tile = true;
    brushie::EncodedImage encoded;
    std::string error;
    assert(brushie::encode({src.data(), w, h, 0}, options, encoded, &error));
    assert(encoded.bytes.size() > 64 && encoded.bytes.size() < 400000);
    std::vector<std::uint8_t> dst;
    assert(brushie::decode(encoded.bytes.data(), encoded.bytes.size(), w, h, dst, -1, &error));
    assert(dst.size() == src.size());
  }
  {
    const unsigned w = 256, h = 256;
    std::vector<std::uint8_t> black(static_cast<std::size_t>(w) * h * 3, 0);
    brushie::EncodeOptions options;
    options.quality = 100;
    brushie::EncodedImage encoded;
    std::string error;
    assert(brushie::encode({black.data(), w, h, 0}, options, encoded, &error));
    // A zero image needs only the self-contained header: the decoder's
    // coefficient planes are implicitly zero.
    assert(encoded.bytes.size() == 64);
    assert(encoded.stats.chunks == 0);
    std::vector<std::uint8_t> decoded;
    assert(brushie::decode(encoded.bytes.data(), encoded.bytes.size(), w, h,
                           decoded, -1, &error));
    assert(decoded == black);
  }
  {
    // Lossy round-trip at multiple operating points, including the 4:2:0
    // chroma path (quality < 95) and odd dimensions.
    for (const auto q : {20u, 35u, 50u, 70u, 82u, 90u, 99u, 100u}) {
      const unsigned w = 129, h = 131;
      std::vector<std::uint8_t> src(static_cast<std::size_t>(w) * h * 3);
      for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x) {
        const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 3;
        src[i] = static_cast<std::uint8_t>((x * 17 + y * 3 + q) & 255);
        src[i + 1] = static_cast<std::uint8_t>((x * 7 + y * 13 + 2 * q) & 255);
        src[i + 2] = static_cast<std::uint8_t>(((x ^ y) * 11 + 3 * q) & 255);
      }
      brushie::EncodeOptions options;
      options.quality = static_cast<std::uint8_t>(q);
      options.threads = 4;
      brushie::EncodedImage encoded;
      std::string error;
      assert(brushie::encode({src.data(), w, h, 0}, options, encoded, &error));
      std::vector<std::uint8_t> dst;
      assert(brushie::decode(encoded.bytes.data(), encoded.bytes.size(), w, h, dst, -1, &error));
      assert(dst.size() == src.size());
      if (q == 100 && dst != src) {
        std::cerr << "lossless mismatch at " << w << "x" << h << "\n";
        return 1;
      }
      // Progressive decode must work both from the full stream and from a
      // physically truncated header+directory+selected-payload prefix.
      auto u16 = [](const std::uint8_t* p) -> std::uint16_t {
        return static_cast<std::uint16_t>(p[0] | (static_cast<std::uint16_t>(p[1]) << 8));
      };
      auto u32 = [](const std::uint8_t* p) -> std::uint32_t {
        return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
               (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
      };
      auto u64 = [](const std::uint8_t* p) -> std::uint64_t {
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(p[i]) << (i * 8);
        return v;
      };
      const std::uint32_t chunks = u32(encoded.bytes.data() + 32);
      const std::size_t payload_start = static_cast<std::size_t>(u64(encoded.bytes.data() + 40));
      for (int layer = 0; layer <= static_cast<int>(encoded.stats.pyramid_levels); ++layer) {
        std::vector<std::uint8_t> full_output;
        assert(brushie::decode(encoded.bytes.data(), encoded.bytes.size(), w, h,
                               full_output, layer, &error));
        assert(full_output.size() == src.size());
        std::size_t prefix_size = payload_start;
        for (std::uint32_t ci = 0; ci < chunks; ++ci) {
          const std::uint8_t* d = encoded.bytes.data() + 64 + static_cast<std::size_t>(ci) * 40;
          if (u16(d) > layer) continue;
          const std::size_t end = static_cast<std::size_t>(u64(d + 20)) + u32(d + 28);
          if (end > prefix_size) prefix_size = end;
        }
        std::vector<std::uint8_t> physical_prefix(
            encoded.bytes.begin(), encoded.bytes.begin() + prefix_size);
        std::vector<std::uint8_t> prefix_output;
        assert(brushie::decode(physical_prefix.data(), physical_prefix.size(), w, h,
                               prefix_output, layer, &error));
        assert(prefix_output == full_output);
      }
      // Scaled decode must succeed.
      std::vector<std::uint8_t> scaled;
      assert(brushie::decode(encoded.bytes.data(), encoded.bytes.size(), 31, 23,
                             scaled, -1, &error));
      assert(scaled.size() == 31u * 23u * 3u);
    }
  }
  {
    // Deterministic sanity: the same input and options produce a bit-identical
    // stream (the range coder must not depend on thread scheduling).
    const unsigned w = 256, h = 192;
    std::vector<std::uint8_t> src(static_cast<std::size_t>(w) * h * 3);
    for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x) {
      const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 3;
      src[i] = static_cast<std::uint8_t>((x * 5 + y) & 255);
      src[i + 1] = static_cast<std::uint8_t>((x + y * 9) & 255);
      src[i + 2] = static_cast<std::uint8_t>(((x * 3) ^ (y * 7)) & 255);
    }
    brushie::EncodeOptions options;
    options.quality = 70;
    options.threads = 8;
    brushie::EncodedImage a, b;
    std::string error;
    assert(brushie::encode({src.data(), w, h, 0}, options, a, &error));
    options.threads = 1;
    assert(brushie::encode({src.data(), w, h, 0}, options, b, &error));
    assert(a.bytes == b.bytes);
  }
  {
    // The encoder may compete 32- and 64-base modes without a format flag;
    // levels/base dimensions in the stream make both self-describing.
    const unsigned w = 257, h = 193;
    std::vector<std::uint8_t> src(static_cast<std::size_t>(w) * h * 3);
    for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x) {
      const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 3;
      src[i] = static_cast<std::uint8_t>((x * 3 + y * 7) & 255);
      src[i + 1] = static_cast<std::uint8_t>((x * 11 + y) & 255);
      src[i + 2] = static_cast<std::uint8_t>(((x ^ y) * 9) & 255);
    }
    brushie::EncodeOptions options;
    options.quality = 100;
    options.threads = 4;
    options.base_target = 32;
    brushie::EncodedImage deep, shallow;
    std::string error;
    assert(brushie::encode({src.data(), w, h, 0}, options, deep, &error));
    options.base_target = 64;
    assert(brushie::encode({src.data(), w, h, 0}, options, shallow, &error));
    assert(deep.stats.pyramid_levels > shallow.stats.pyramid_levels);
    assert(deep.stats.base_width < shallow.stats.base_width);
    for (const auto* encoded : {&deep, &shallow}) {
      std::vector<std::uint8_t> decoded;
      assert(brushie::decode(encoded->bytes.data(), encoded->bytes.size(), w, h,
                             decoded, -1, &error));
      assert(decoded == src);
    }
  }
  {
    // q99 must remain a real lossy high-quality point, not collapse to the
    // q100 lossless stream (regression for the harness-v3 .995 cliff fix).
    const unsigned w = 257, h = 193;
    std::vector<std::uint8_t> src(static_cast<std::size_t>(w) * h * 3);
    for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x) {
      const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 3;
      src[i] = static_cast<std::uint8_t>((x * 37 + y * 11) & 255);
      src[i + 1] = static_cast<std::uint8_t>((x * 5 + y * 41) & 255);
      src[i + 2] = static_cast<std::uint8_t>(((x * 13) ^ (y * 29)) & 255);
    }
    brushie::EncodeOptions options;
    options.quality = 99;
    options.threads = 4;
    brushie::EncodedImage q99, q100;
    std::string error;
    assert(brushie::encode({src.data(), w, h, 0}, options, q99, &error));
    options.quality = 100;
    assert(brushie::encode({src.data(), w, h, 0}, options, q100, &error));
    assert(q99.bytes.size() < q100.bytes.size());
    std::vector<std::uint8_t> decoded;
    assert(brushie::decode(q99.bytes.data(), q99.bytes.size(), w, h, decoded, -1, &error));
    assert(decoded.size() == src.size());
  }
  {
    // RGBA lossless round-trip (alpha channel, channel count 4).
    const unsigned w = 97, h = 63;
    std::vector<std::uint8_t> src(static_cast<std::size_t>(w) * h * 4);
    for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x) {
      const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
      src[i] = static_cast<std::uint8_t>((x * 13 + y) & 255);
      src[i + 1] = static_cast<std::uint8_t>((x + y * 11) & 255);
      src[i + 2] = static_cast<std::uint8_t>(((x ^ y) * 5) & 255);
      src[i + 3] = static_cast<std::uint8_t>((x < w / 2) ? 255 : 0);  // hard alpha edge
    }
    brushie::EncodeOptions options;
    options.quality = 100;
    options.threads = 4;
    brushie::EncodedImage encoded;
    std::string error;
    assert(brushie::encode({src.data(), w, h, 0, 4}, options, encoded, &error));
    std::vector<std::uint8_t> dst;
    assert(brushie::decode(encoded.bytes.data(), encoded.bytes.size(), w, h, dst, -1, &error));
    assert(dst.size() == src.size());
    if (dst != src) {
      std::size_t nbad = 0;
      for (std::size_t i = 0; i < src.size(); ++i)
        if (src[i] != dst[i]) { if (nbad++ < 10) std::cerr << "rgba diff " << i << " " << (int)src[i] << " " << (int)dst[i] << "\n"; }
      std::cerr << "rgba nbad=" << nbad << "\n";
      return 1;
    }
    // Lossy RGBA round-trip must succeed with correct output size.
    options.quality = 50;
    assert(brushie::encode({src.data(), w, h, 0, 4}, options, encoded, &error));
    assert(brushie::decode(encoded.bytes.data(), encoded.bytes.size(), w, h, dst, -1, &error));
    assert(dst.size() == src.size());
    assert(encoded.bytes[21] == 4);
  }
  std::cout << "codec tests passed\n";
}
