#include "brushie/codec.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>
#include <sys/resource.h>

namespace {

static double peak_megabytes() {
  struct rusage usage{};
  getrusage(RUSAGE_SELF, &usage);
#if defined(__APPLE__)
  return usage.ru_maxrss / (1024.0 * 1024.0);
#else
  return usage.ru_maxrss / 1024.0;
#endif
}

static void make_image(std::uint32_t n, const std::string& pattern,
                       std::vector<std::uint8_t>& rgb) {
  rgb.resize(static_cast<std::size_t>(n) * n * 3);
  for (std::uint32_t y = 0; y < n; ++y) {
    for (std::uint32_t x = 0; x < n; ++x) {
      std::uint8_t r, g, b;
      if (pattern == "checker") {
        const std::uint8_t v = ((x / 16 + y / 16) & 1) ? 240 : 16;
        r = v; g = static_cast<std::uint8_t>(255 - v); b = v;
      } else if (pattern == "lines") {
        const std::uint8_t v = ((x % 5 == 0) || (y % 7 == 0)) ? 255 : 20;
        r = v; g = static_cast<std::uint8_t>((x * 3 + y) & 255); b = static_cast<std::uint8_t>(255 - v);
      } else {
        r = static_cast<std::uint8_t>((x * 255ull) / std::max<std::uint32_t>(1, n - 1));
        g = static_cast<std::uint8_t>((y * 255ull) / std::max<std::uint32_t>(1, n - 1));
        b = static_cast<std::uint8_t>(((x ^ y) * 13u) & 255);
      }
      const std::size_t i = (static_cast<std::size_t>(y) * n + x) * 3;
      rgb[i] = r; rgb[i + 1] = g; rgb[i + 2] = b;
    }
  }
}

static double psnr(const std::vector<std::uint8_t>& a,
                   const std::vector<std::uint8_t>& b) {
  long double mse = 0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    const long double d = static_cast<int>(a[i]) - static_cast<int>(b[i]);
    mse += d * d;
  }
  mse /= std::max<std::size_t>(1, a.size());
  return mse == 0 ? 99.0 : 10.0 * std::log10((255.0 * 255.0) / static_cast<double>(mse));
}

}  // namespace

int main(int argc, char** argv) {
  const std::string csv_path = argc > 1 ? argv[1] : "benchmark_results.csv";
  const std::uint32_t threads = argc > 2 ? static_cast<std::uint32_t>(std::stoul(argv[2])) : 8;
  std::ofstream csv(csv_path);
  csv << "pattern,size,quality,threads,encode_ms,decode_ms,base_decode_ms,bytes,bpp,psnr,peak_mib\n";
  for (std::uint32_t n : {512u, 1024u, 4096u}) {
    for (const std::string pattern : {"gradient", "checker", "lines"}) {
      std::vector<std::uint8_t> source;
      make_image(n, pattern, source);
      brushie::EncodedImage encoded;
      brushie::EncodeOptions options;
      options.quality = 82;
      options.threads = threads;
      const auto e0 = std::chrono::steady_clock::now();
      std::string error;
      if (!brushie::encode({source.data(), n, n, 0}, options, encoded, &error)) {
        std::cerr << "encode failed: " << error << '\n'; return 1;
      }
      const auto e1 = std::chrono::steady_clock::now();
      std::vector<std::uint8_t> decoded;
      const auto d0 = std::chrono::steady_clock::now();
      if (!brushie::decode(encoded.bytes.data(), encoded.bytes.size(), n, n, decoded, -1, &error)) {
        std::cerr << "decode failed: " << error << '\n'; return 1;
      }
      const auto d1 = std::chrono::steady_clock::now();
      std::vector<std::uint8_t> base;
      const auto b0 = std::chrono::steady_clock::now();
      if (!brushie::decode(encoded.bytes.data(), encoded.bytes.size(), n, n, base, 0, &error)) {
        std::cerr << "base decode failed: " << error << '\n'; return 1;
      }
      const auto b1 = std::chrono::steady_clock::now();
      const double ems = std::chrono::duration<double, std::milli>(e1 - e0).count();
      const double dms = std::chrono::duration<double, std::milli>(d1 - d0).count();
      const double bms = std::chrono::duration<double, std::milli>(b1 - b0).count();
      const double bytes = encoded.bytes.size();
      csv << pattern << ',' << n << ',' << static_cast<int>(options.quality) << ',' << threads << ','
          << std::fixed << std::setprecision(3) << ems << ',' << dms << ',' << bms << ','
          << encoded.bytes.size() << ',' << (8.0 * bytes / (n * static_cast<double>(n))) << ','
          << psnr(source, decoded) << ',' << peak_megabytes() << '\n';
      std::cout << pattern << ' ' << n << " encode_ms=" << ems << " decode_ms=" << dms
                << " bytes=" << encoded.bytes.size() << " nonzero=" << encoded.stats.nonzero_coefficients
                << " psnr=" << psnr(source, decoded) << '\n';
    }
  }
  return 0;
}
