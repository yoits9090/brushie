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
  std::cout << "codec tests passed\n";
}
