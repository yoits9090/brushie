#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <unordered_map>
using namespace std;
struct Sym { uint32_t pass; uint32_t ctx; uint32_t bit; };
static vector<vector<Sym>> load(const char* path) {
  FILE* f = fopen(path, "r");
  vector<vector<Sym>> bands;
  unordered_map<string, int> tag2band;
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
struct Enc {
  std::vector<uint8_t>& out; uint32_t x = 1u << 16; std::vector<uint16_t> chunks;
  uint64_t n_emit = 0;
  Enc(std::vector<uint8_t>& o):out(o){}
  void bit(uint16_t f1, uint32_t b){
    uint32_t f = (b ? f1 : (4096u-f1)) * 16u;
    uint32_t c = b ? (4096u-f1)*16u : 0u;
    while (x >= (f << 16)) { chunks.push_back((uint16_t)(x & 0xFFFF)); x >>= 16; ++n_emit; }
    x = (x / f) * 65536u + (x % f) + c;
  }
  void flush(){
    out.push_back((uint8_t)(x>>24)); out.push_back((uint8_t)(x>>16));
    out.push_back((uint8_t)(x>>8)); out.push_back((uint8_t)x);
    for (size_t i = chunks.size(); i-- > 0;) {
      out.push_back((uint8_t)(chunks[i] & 0xFF)); out.push_back((uint8_t)(chunks[i] >> 8));
    }
  }
};
static uint16_t adapt(uint16_t f1, uint32_t b, unsigned shift){
  int p = f1;
  if (b) { p -= p >> shift; if (p<1) p=1; } else { p += (4096-p)>>shift; if (p>4095) p=4095; }
  return (uint16_t)p;
}
int main(){
  auto bands = load("/tmp/syms.tsv");
  uint64_t tot = 0;
  for (size_t bi = 0; bi < bands.size(); ++bi) {
    auto& band = bands[bi];
    vector<uint16_t> probs(256, 2048);
    vector<uint16_t> fused(band.size());
    for (size_t i = 0; i < band.size(); ++i) {
      fused[i] = probs[band[i].ctx];
      probs[band[i].ctx] = adapt(probs[band[i].ctx], band[i].bit, band[i].pass==2?4:5);
    }
    vector<uint8_t> out;
    Enc enc(out);
    for (size_t i = band.size(); i-- > 0;) enc.bit(fused[i], band[i].bit);
    enc.flush();
    tot += out.size();
    if (out.size() > 4000)
      printf("band %zu: symbols=%zu bytes=%zu emits=%llu (BIG)\n", bi, band.size(), out.size(), (unsigned long long)enc.n_emit);
    else
      printf("band %zu: symbols=%zu bytes=%zu\n", bi, band.size(), out.size());
  }
  printf("TOTAL %llu\n", (unsigned long long)tot);
  return 0;
}
