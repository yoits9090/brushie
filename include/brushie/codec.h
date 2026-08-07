#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace brushie {

struct ImageView {
  const std::uint8_t* rgb = nullptr;  // RGB8 (channels=3) or RGBA8 (channels=4)
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::size_t stride = 0;  // bytes between rows; zero means width * channels.
  std::uint8_t channels = 3;  // 3 = RGB, 4 = RGBA (alpha coded losslessly-ish)
};

struct EncodeOptions {
  std::uint8_t quality = 82;  // 1..100; 100 is integer-transform exact.
  std::uint32_t threads = 8;
  std::uint32_t tile_size = 32;
  // Optional final LL target (normally 32 or 64). Zero uses the historical
  // dimension-based default. The stream stores levels/base dims, so decoder
  // support is automatic and no format flag is needed.
  std::uint32_t base_target = 0;
  // Optional rate-control hints. They select a deterministic operating point
  // before the single pyramid/stream pass; the resulting byte count is still
  // reported by EncodeStats and may differ from the hint.
  std::uint64_t target_bytes = 0;
  double target_lpips = 0.0;  // 0 means unset; LPIPS is not evaluated in-core.
  bool adaptive_tile = false;
};

struct EncodeStats {
  std::uint64_t input_bytes = 0;
  std::uint64_t encoded_bytes = 0;
  std::uint64_t nonzero_coefficients = 0;
  std::uint32_t pyramid_levels = 0;
  std::uint32_t chunks = 0;
  std::uint32_t base_width = 0;
  std::uint32_t base_height = 0;
};

struct EncodedImage {
  std::vector<std::uint8_t> bytes;
  EncodeStats stats;
};

// The input pointer remains owned by the caller and is read synchronously.
// No file-system work is performed by this API.
bool encode(const ImageView& image, const EncodeOptions& options,
            EncodedImage& output, std::string* error = nullptr);

// max_progressive_layer: -1 decodes all layers; 0 is the base LL layer; a
// positive value admits that many coarse-to-fine detail layers. The decoder
// produces exactly output_width * output_height * channels bytes, where
// channels (3 or 4) is read from the stream header.
bool decode(const std::uint8_t* data, std::size_t size,
            std::uint32_t output_width, std::uint32_t output_height,
            std::vector<std::uint8_t>& rgb,
            int max_progressive_layer = -1,
            std::string* error = nullptr);

}  // namespace brushie
