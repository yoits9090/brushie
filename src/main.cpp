#include "brushie/codec.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef BRUSHIE_HAVE_PNG
#include <png.h>
#endif

namespace {

struct Ppm {
  std::uint32_t w = 0, h = 0;
  unsigned channels = 3;
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

#ifdef BRUSHIE_HAVE_PNG
namespace {
bool read_png(const std::string& path, Ppm& image, std::string& error) {
  FILE* fp = std::fopen(path.c_str(), "rb");
  if (!fp) { error = "cannot open PNG input"; return false; }
  png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  png_infop info = png_create_info_struct(png);
  if (!png || !info) { error = "png init failed"; if (png) png_destroy_read_struct(&png, nullptr, nullptr); std::fclose(fp); return false; }
  if (setjmp(png_jmpbuf(png))) { error = "corrupt or unsupported PNG"; png_destroy_read_struct(&png, &info, nullptr); std::fclose(fp); return false; }
  png_init_io(png, fp);
  png_read_info(png, info);
  image.w = png_get_image_width(png, info);
  image.h = png_get_image_height(png, info);
  const png_byte color_type = png_get_color_type(png, info);
  const png_byte bit_depth = png_get_bit_depth(png, info);
  if (bit_depth == 16) png_set_strip_16(png);
  if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
  if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
  if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
  if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);
  png_read_update_info(png, info);
  const unsigned channels = png_get_channels(png, info);
  image.rgb.resize(static_cast<std::size_t>(image.w) * image.h * (channels == 4 ? 4 : 3));
  std::vector<png_bytep> rows(image.h);
  for (std::uint32_t y = 0; y < image.h; ++y)
    rows[y] = image.rgb.data() + static_cast<std::size_t>(y) * image.w * (channels == 4 ? 4 : 3);
  png_read_image(png, rows.data());
  png_destroy_read_struct(&png, &info, nullptr);
  std::fclose(fp);
  return true;
}

bool write_png(const std::string& path, std::uint32_t w, std::uint32_t h,
               const std::vector<std::uint8_t>& rgb, unsigned channels,
               std::string& error) {
  FILE* fp = std::fopen(path.c_str(), "wb");
  if (!fp) { error = "cannot open PNG output"; return false; }
  png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
  png_infop info = png_create_info_struct(png);
  if (!png || !info) { error = "png init failed"; if (png) png_destroy_write_struct(&png, nullptr); std::fclose(fp); return false; }
  if (setjmp(png_jmpbuf(png))) { error = "png write failed"; png_destroy_write_struct(&png, &info); std::fclose(fp); return false; }
  png_init_io(png, fp);
  const unsigned bpp = channels == 4 ? 4u : 3u;
  png_set_IHDR(png, info, w, h, 8,
               channels == 4 ? PNG_COLOR_TYPE_RGBA : PNG_COLOR_TYPE_RGB,
               PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
  png_write_info(png, info);
  std::vector<png_bytep> rows(h);
  for (std::uint32_t y = 0; y < h; ++y)
    rows[y] = const_cast<png_bytep>(rgb.data() + static_cast<std::size_t>(y) * w * bpp);
  png_write_image(png, rows.data());
  png_write_end(png, nullptr);
  png_destroy_write_struct(&png, &info);
  std::fclose(fp);
  return true;
}
}  // namespace
#endif

static bool is_png_path(const std::string& p) {
  const std::size_t dot = p.find_last_of('.');
  return dot != std::string::npos && p.compare(dot, 5, ".png") == 0;
}

void usage() {
  std::cerr << "brushie encode input.ppm output.brbr [quality=82] [threads=8] [tile=64]\n"
            << "                 [--quality Q] [--threads N] [--tile T] [--adaptive-tile]\n"
            << "                 [--target-bytes N] [--target-lpips X]\n"
            << "brushie decode input.brbr output.ppm width height [layer=-1]\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) { usage(); return 2; }
  std::string error;
  if (std::string(argv[1]) == "encode") {
    if (argc < 4) { usage(); return 2; }
    Ppm image;
#ifdef BRUSHIE_HAVE_PNG
    if (is_png_path(argv[2])) {
      if (!read_png(argv[2], image, error)) { std::cerr << error << '\n'; return 1; }
      image.channels = image.rgb.size() == static_cast<std::size_t>(image.w) * image.h * 4 ? 4u : 3u;
    } else
#endif
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
    if (!brushie::encode({image.rgb.data(), image.w, image.h, 0, static_cast<std::uint8_t>(image.channels)}, options, encoded, &error)) {
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
    const unsigned out_channels = rgb.size() == static_cast<std::size_t>(w) * h * 4 ? 4u : 3u;
    bool written = false;
#ifdef BRUSHIE_HAVE_PNG
    if (is_png_path(argv[3])) written = write_png(argv[3], w, h, rgb, out_channels, error);
#endif
    if (!written) written = write_ppm(argv[3], w, h, rgb, error);
    if (!written) { std::cerr << error << '\n'; return 1; }
    std::cout << "decode_ms=" << elapsed << " pixels=" << (w * static_cast<std::uint64_t>(h)) << '\n';
    return 0;
  }
  usage();
  return 2;
}
