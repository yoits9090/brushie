#include <cstdint>
#include <cstdio>
#include <vector>
struct Enc {
  std::vector<uint8_t>& out; uint32_t x = 1u << 16;
  std::vector<uint16_t> chunks;
  Enc(std::vector<uint8_t>& o):out(o){}
  void out16(){ chunks.push_back((uint16_t)(x & 0xFFFF)); x >>= 16; }
  void bit(uint16_t& f1, uint32_t b, unsigned shift){
    uint32_t f = (b ? f1 : (4096u-f1)) * 4u;
    uint32_t c = b ? (4096u-f1)*4u : 0u;
    while (x >= (f << 8)) out16();
    x = (x / f) * 65536u + (x % f) + c;
    int p = f1;
    if (b) { p -= p >> shift; if (p<1) p=1; } else { p += (4096-p)>>shift; if (p>4095) p=4095; }
    f1 = (uint16_t)p;
  }
  void flush(){
    out.push_back((uint8_t)(x>>24)); out.push_back((uint8_t)(x>>16));
    out.push_back((uint8_t)(x>>8)); out.push_back((uint8_t)x);
    for (size_t i = chunks.size(); i-- > 0;) {
      out.push_back((uint8_t)(chunks[i] & 0xFF));
      out.push_back((uint8_t)(chunks[i] >> 8));
    }
  }
};
struct Dec {
  const uint8_t* d; size_t size, pos=0; uint32_t x=0;
  Dec(const uint8_t* dd, size_t s):d(dd),size(s){ for(int i=0;i<4;++i) x=(x<<8)|read(); }
  uint8_t read(){ if (pos>=size){++pos; return 0;} return d[pos++]; }
  uint32_t bit(uint16_t& f1, unsigned shift){
    uint32_t slot = x & 0xFFFFu;
    uint32_t b = slot >= (4096u-f1)*4u ? 1u : 0u;
    uint32_t f = (b ? f1 : (4096u-f1))*4u;
    uint32_t c = b ? (4096u-f1)*4u : 0u;
    x = f * (x >> 16) + slot - c;
    while (x < (1u<<16)) x = (x<<16) | ((uint32_t)read()<<8) | read();
    int p = f1;
    if (b) { p -= p >> shift; if (p<1) p=1; } else { p += (4096-p)>>shift; if (p>4095) p=4095; }
    f1 = (uint16_t)p;
    return b;
  }
};
int main(){
  std::vector<uint8_t> out;
  Enc enc(out);
  uint16_t p = 2048;
  std::vector<uint32_t> bits = {1,0,1,1,0,0,0,1,0,1,0,0,1,1,0,1};
  for (auto b : bits) enc.bit(p, b, 5);
  enc.flush();
  printf("bytes=%zu\n", out.size());
  Dec dec(out.data(), out.size());
  uint16_t p2 = 2048;
  for (size_t i = 0; i < bits.size(); ++i) {
    uint32_t d = dec.bit(p2, 5);
    if (d != bits[i]) printf("MISMATCH %zu want %u got %u\n", i, bits[i], d);
  }
  printf("done\n");
  return 0;
}
