#include "brushie/codec.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

struct Ppm {
  std::uint32_t w = 0, h = 0;
  std::vector<std::uint8_t> rgb;
};

bool read_ppm(const std::string& path, Ppm& image, std::string& error) {
  std::ifstream f(path, std::ios::binary);
  if (!f) { error = "cannot open input"; return false; }
  auto token = [&]() -> std::string {
    std::string s;
    char c;
    while (f.get(c)) {
      if (c == '#') { while (f.get(c) && c != '\n') {} continue; }
      if (c > ' ') { s.push_back(c); break; }
    }
    while (f.get(c) && c > ' ') s.push_back(c);
    return s;
  };
  if (token() != "P6") { error = "only binary P6 PPM is accepted"; return false; }
  try { image.w = static_cast<std::uint32_t>(std::stoul(token())); image.h = static_cast<std::uint32_t>(std::stoul(token())); }
  catch (...) { error = "malformed PPM dimensions"; return false; }
  if (token() != "255") { error = "PPM max value must be 255"; return false; }
  image.rgb.resize(static_cast<std::size_t>(image.w) * image.h * 3);
  f.read(reinterpret_cast<char*>(image.rgb.data()), image.rgb.size());
  if (f.gcount() != static_cast<std::streamsize>(image.rgb.size())) { error = "truncated PPM"; return false; }
  return true;
}

bool write_ppm(const std::string& path, std::uint32_t w, std::uint32_t h,
               const std::vector<std::uint8_t>& rgb, std::string& error) {
  std::ofstream f(path, std::ios::binary);
  if (!f) { error = "cannot open output"; return false; }
  f << "P6\n" << w << ' ' << h << "\n255\n";
  f.write(reinterpret_cast<const char*>(rgb.data()), rgb.size());
  if (!f) { error = "write failed"; return false; }
  return true;
}

void usage() {
  std::cerr << "brushie encode input.ppm output.caps [quality=82] [threads=8] [tile=32]\n"
            << "                 [--quality Q] [--threads N] [--tile T] [--adaptive-tile]\n"
            << "                 [--target-bytes N] [--target-lpips X]\n"
            << "brushie decode input.caps output.ppm width height [layer=-1]\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) { usage(); return 2; }
  std::string error;
  if (std::string(argv[1]) == "encode") {
    if (argc < 4) { usage(); return 2; }
    Ppm image;
    if (!read_ppm(argv[2], image, error)) { std::cerr << error << '\n'; return 1; }
    brushie::EncodeOptions options;
    int positional = 0;
    try {
      for (int i = 4; i < argc; ++i) {
        const std::string arg = argv[i];
        auto need = [&](const char* name) -> std::string {
          if (i + 1 >= argc) throw std::invalid_argument(std::string("missing ") + name);
          return argv[++i];
        };
        if (arg == "--quality") options.quality = static_cast<std::uint8_t>(std::stoul(need("--quality")));
        else if (arg == "--threads") options.threads = static_cast<std::uint32_t>(std::stoul(need("--threads")));
        else if (arg == "--tile") options.tile_size = static_cast<std::uint32_t>(std::stoul(need("--tile")));
        else if (arg == "--target-bytes") options.target_bytes = std::stoull(need("--target-bytes"));
        else if (arg == "--target-lpips") options.target_lpips = std::stod(need("--target-lpips"));
        else if (arg == "--adaptive-tile") options.adaptive_tile = true;
        else if (!arg.empty() && arg[0] != '-') {
          if (positional == 0) options.quality = static_cast<std::uint8_t>(std::stoul(arg));
          else if (positional == 1) options.threads = static_cast<std::uint32_t>(std::stoul(arg));
          else if (positional == 2) options.tile_size = static_cast<std::uint32_t>(std::stoul(arg));
          else throw std::invalid_argument("too many positional encode arguments");
          ++positional;
        } else throw std::invalid_argument("unknown encode option: " + arg);
      }
    } catch (const std::exception& e) {
      std::cerr << e.what() << '\n'; usage(); return 2;
    }
    if (options.target_bytes != 0 || options.target_lpips > 0.0) options.adaptive_tile = true;
    brushie::EncodedImage encoded;
    const auto start = std::chrono::steady_clock::now();
    if (!brushie::encode({image.rgb.data(), image.w, image.h, 0}, options, encoded, &error)) {
      std::cerr << error << '\n'; return 1;
    }
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::ofstream out(argv[3], std::ios::binary);
    out.write(reinterpret_cast<const char*>(encoded.bytes.data()), encoded.bytes.size());
    std::cout << "encode_ms=" << elapsed << " bytes=" << encoded.bytes.size()
              << " bpp=" << (8.0 * encoded.bytes.size() / (image.w * static_cast<double>(image.h)))
              << " levels=" << encoded.stats.pyramid_levels << " chunks=" << encoded.stats.chunks
              << " quality=" << static_cast<unsigned>(encoded.bytes[20])
              << " tile=" << (encoded.bytes[18] | (static_cast<unsigned>(encoded.bytes[19]) << 8));
    if (options.target_bytes != 0) std::cout << " target_bytes=" << options.target_bytes;
    if (options.target_lpips > 0.0) std::cout << " target_lpips_unverified=" << options.target_lpips;
    std::cout << '\n';
    return out ? 0 : 1;
  }
  if (std::string(argv[1]) == "decode") {
    if (argc < 6) { usage(); return 2; }
    std::ifstream in(argv[2], std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), {});
    if (!in) { std::cerr << "cannot read stream\n"; return 1; }
    const auto w = static_cast<std::uint32_t>(std::stoul(argv[4]));
    const auto h = static_cast<std::uint32_t>(std::stoul(argv[5]));
    const int layer = argc > 6 ? std::stoi(argv[6]) : -1;
    std::vector<std::uint8_t> rgb;
    const auto start = std::chrono::steady_clock::now();
    if (!brushie::decode(bytes.data(), bytes.size(), w, h, rgb, layer, &error)) {
      std::cerr << error << '\n'; return 1;
    }
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    if (!write_ppm(argv[3], w, h, rgb, error)) { std::cerr << error << '\n'; return 1; }
    std::cout << "decode_ms=" << elapsed << " pixels=" << (w * static_cast<std::uint64_t>(h)) << '\n';
    return 0;
  }
  usage();
  return 2;
}
