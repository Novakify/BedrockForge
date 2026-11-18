// FiguraBridge Lua API: FiguraTexture (`Texture`) + TextureAPI (`textures`) with real in-memory pixel buffers
// (NativeImage RGBA = ABGR ints), a small PNG decoder/encoder (zlib inflate / stored deflate) and base64.
// Bedrock can't upload dynamic textures yet: everything is kept in memory and read back exactly like Figura.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>

#include "figura/core/Avatar.h"
#include "figura/core/LuaApi.h"
#include "figura/core/LuaBind.h"
#include "figura/core/LuaTex.h"
#include "lyrics/core/Json.h"

namespace bf::figura {

using namespace lua;
namespace json = lyrics::json;

uint32_t tTexture = 0;

// =====================================================================================================================
// base64 (java.util.Base64 basic)
// =====================================================================================================================
static char const kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string b64Encode(std::string const& in) {
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        uint32_t v = uint8_t(in[i]) << 16 | uint8_t(in[i + 1]) << 8 | uint8_t(in[i + 2]);
        out += kB64[v >> 18];
        out += kB64[v >> 12 & 63];
        out += kB64[v >> 6 & 63];
        out += kB64[v & 63];
    }
    if (i + 1 == in.size()) {
        uint32_t v = uint8_t(in[i]) << 16;
        out += kB64[v >> 18];
        out += kB64[v >> 12 & 63];
        out += "==";
    } else if (i + 2 == in.size()) {
        uint32_t v = uint8_t(in[i]) << 16 | uint8_t(in[i + 1]) << 8;
        out += kB64[v >> 18];
        out += kB64[v >> 12 & 63];
        out += kB64[v >> 6 & 63];
        out += '=';
    }
    return out;
}

bool b64Decode(std::string const& in, std::string& out, std::string* err) {
    out.clear();
    uint32_t acc = 0;
    int      n = 0;
    size_t   i = 0;
    for (; i < in.size(); i++) {
        unsigned char c = static_cast<unsigned char>(in[i]);
        if (c == '=') break;
        char const* p = std::strchr(kB64, c);
        if (!p || c == 0) {
            char b[64];
            std::snprintf(b, sizeof b, "Illegal base64 character %x", unsigned(c));
            if (err) *err = b;
            return false;
        }
        acc = acc << 6 | uint32_t(p - kB64);
        if (++n == 4) {
            out += char(acc >> 16 & 255);
            out += char(acc >> 8 & 255);
            out += char(acc & 255);
            acc = 0;
            n   = 0;
        }
    }
    if (n == 1) {
        if (err) *err = "Last unit does not have enough valid bits";
        return false;
    }
    if (n == 2) out += char(acc >> 4 & 255);
    if (n == 3) {
        out += char(acc >> 10 & 255);
        out += char(acc >> 2 & 255);
    }
    // padding: only '=' may follow
    for (; i < in.size(); i++)
        if (in[i] != '=') {
            if (err) *err = "Input byte array has incorrect ending byte at " + std::to_string(i);
            return false;
        }
    return true;
}

// =====================================================================================================================
// inflate (RFC 1951, puff-style)
// =====================================================================================================================
namespace {
struct InflateError {};
struct Inflater {
    uint8_t const* in;
    size_t         len, pos = 0;
    uint32_t       bitbuf = 0;
    int            bitcnt = 0;
    std::string&   out;
    Inflater(uint8_t const* d, size_t n, std::string& o) : in(d), len(n), out(o) {}
    int bits(int need) {
        uint32_t val = bitbuf;
        while (bitcnt < need) {
            if (pos >= len) throw InflateError{};
            val |= uint32_t(in[pos++]) << bitcnt;
            bitcnt += 8;
        }
        bitbuf = need >= 32 ? 0 : val >> need;
        bitcnt -= need;
        return int(val & ((1u << need) - 1));
    }
    struct Huff {
        short count[16];
        short symbol[320];
    };
    static int build(Huff& h, short const* length, int n) {
        std::memset(h.count, 0, sizeof h.count);
        for (int s = 0; s < n; s++) h.count[length[s]]++;
        if (h.count[0] == n) return 0;
        int left = 1;
        for (int l = 1; l < 16; l++) {
            left <<= 1;
            left -= h.count[l];
            if (left < 0) return left;
        }
        short offs[16];
        offs[1] = 0;
        for (int l = 1; l < 15; l++) offs[l + 1] = short(offs[l] + h.count[l]);
        for (int s = 0; s < n; s++)
            if (length[s] != 0) h.symbol[offs[length[s]]++] = short(s);
        return left;
    }
    int decode(Huff const& h) {
        int code = 0, first = 0, index = 0;
        for (int l = 1; l < 16; l++) {
            code |= bits(1);
            int count = h.count[l];
            if (code - count < first) return h.symbol[index + (code - first)];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        throw InflateError{};
    }
    void codes(Huff const& lencode, Huff const& distcode) {
        static short const lbase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static short const lext[29]  = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static short const dbase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
        static short const dext[30]  = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        for (;;) {
            int sym = decode(lencode);
            if (sym < 256) out += char(sym);
            else if (sym == 256) return;
            else {
                sym -= 257;
                if (sym >= 29) throw InflateError{};
                int l = lbase[sym] + bits(lext[sym]);
                int ds = decode(distcode);
                if (ds >= 30) throw InflateError{};
                size_t d = size_t(dbase[ds] + bits(dext[ds]));
                if (d > out.size()) throw InflateError{};
                size_t from = out.size() - d;
                for (int k = 0; k < l; k++) out += out[from + size_t(k)];
            }
        }
    }
    void stored() {
        bitbuf = 0;
        bitcnt = 0;
        if (pos + 4 > len) throw InflateError{};
        unsigned n = in[pos] | in[pos + 1] << 8;
        unsigned c = in[pos + 2] | in[pos + 3] << 8;
        pos += 4;
        if (n != (~c & 0xffff) || pos + n > len) throw InflateError{};
        out.append(reinterpret_cast<char const*>(in + pos), n);
        pos += n;
    }
    void fixed() {
        struct Fixed {
            Huff lc, dc;
            Fixed() {
                short lengths[288];
                int   k = 0;
                for (; k < 144; k++) lengths[k] = 8;
                for (; k < 256; k++) lengths[k] = 9;
                for (; k < 280; k++) lengths[k] = 7;
                for (; k < 288; k++) lengths[k] = 8;
                build(lc, lengths, 288);
                for (k = 0; k < 30; k++) lengths[k] = 5;
                build(dc, lengths, 30);
            }
        };
        static Fixed const f; // thread-safe static init
        codes(f.lc, f.dc);
    }
    void dynamic() {
        static short const order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        int nlen = bits(5) + 257, ndist = bits(5) + 1, ncode = bits(4) + 4;
        if (nlen > 286 || ndist > 30) throw InflateError{};
        short lengths[320];
        int   idx = 0;
        for (; idx < ncode; idx++) lengths[order[idx]] = short(bits(3));
        for (; idx < 19; idx++) lengths[order[idx]] = 0;
        Huff lc, dc;
        if (build(lc, lengths, 19) != 0) throw InflateError{};
        idx = 0;
        while (idx < nlen + ndist) {
            int sym = decode(lc);
            if (sym < 16) lengths[idx++] = short(sym);
            else {
                int l = 0;
                if (sym == 16) {
                    if (idx == 0) throw InflateError{};
                    l   = lengths[idx - 1];
                    sym = 3 + bits(2);
                } else if (sym == 17) sym = 3 + bits(3);
                else sym = 11 + bits(7);
                if (idx + sym > nlen + ndist) throw InflateError{};
                while (sym--) lengths[idx++] = short(l);
            }
        }
        if (lengths[256] == 0) throw InflateError{};
        int e = build(lc, lengths, nlen);
        if (e < 0 || (e > 0 && nlen - lc.count[0] != 1)) throw InflateError{};
        e = build(dc, lengths + nlen, ndist);
        if (e < 0 || (e > 0 && ndist - dc.count[0] != 1)) throw InflateError{};
        codes(lc, dc);
    }
    void run() {
        int last;
        do {
            last     = bits(1);
            int type = bits(2);
            if (type == 0) stored();
            else if (type == 1) fixed();
            else if (type == 2) dynamic();
            else throw InflateError{};
        } while (!last);
    }
};

uint32_t crc32(uint8_t const* d, size_t n, uint32_t c = 0) {
    struct Table {
        uint32_t t[256];
        Table() {
            for (uint32_t i = 0; i < 256; i++) {
                uint32_t v = i;
                for (int k = 0; k < 8; k++) v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
                t[i] = v;
            }
        }
    };
    static Table const tab;
    c = ~c;
    for (size_t i = 0; i < n; i++) c = tab.t[(c ^ d[i]) & 255] ^ (c >> 8);
    return ~c;
}
uint32_t be32(uint8_t const* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
} // namespace

// =====================================================================================================================
// PNG
// =====================================================================================================================
bool pngDecode(std::string const& data, int& w, int& h, std::vector<uint32_t>& px, std::string* err) {
    auto fail = [&](char const* m) { if (err) *err = m; return false; };
    auto const* d = reinterpret_cast<uint8_t const*>(data.data());
    static uint8_t const sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (data.size() < 8 || std::memcmp(d, sig, 8) != 0) return fail("Bad PNG Signature");
    size_t      pos = 8;
    int         depth = 0, ctype = 0, interlace = 0;
    std::string idat;
    uint8_t     pal[256][4];
    int         palN = 0;
    bool        hasTrns = false;
    uint16_t    trnsKey[3] = {0, 0, 0};
    for (int i = 0; i < 256; i++) pal[i][0] = pal[i][1] = pal[i][2] = 0, pal[i][3] = 255;
    w = h = 0;
    while (pos + 8 <= data.size()) {
        uint32_t    n    = be32(d + pos);
        std::string type(reinterpret_cast<char const*>(d + pos + 4), 4);
        if (pos + 12 + n > data.size()) return fail("Corrupt PNG");
        uint8_t const* c = d + pos + 8;
        if (type == "IHDR") {
            if (n < 13) return fail("Corrupt PNG");
            w = int(be32(c));
            h = int(be32(c + 4));
            depth = c[8];
            ctype = c[9];
            interlace = c[12];
            if (w <= 0 || h <= 0 || w > 1 << 24 || h > 1 << 24) return fail("Corrupt PNG");
        } else if (type == "PLTE") {
            palN = int(n / 3);
            for (int i = 0; i < palN && i < 256; i++) pal[i][0] = c[i * 3], pal[i][1] = c[i * 3 + 1], pal[i][2] = c[i * 3 + 2];
        } else if (type == "tRNS") {
            if (ctype == 3) for (uint32_t i = 0; i < n && i < 256; i++) pal[i][3] = c[i];
            else if (ctype == 0 && n >= 2) { hasTrns = true; trnsKey[0] = uint16_t(c[0] << 8 | c[1]); }
            else if (ctype == 2 && n >= 6) {
                hasTrns = true;
                for (int k = 0; k < 3; k++) trnsKey[k] = uint16_t(c[k * 2] << 8 | c[k * 2 + 1]);
            }
        } else if (type == "IDAT") {
            idat.append(reinterpret_cast<char const*>(c), n);
        } else if (type == "IEND") break;
        pos += 12 + n;
    }
    if (w == 0) return fail("Corrupt PNG");
    int channels = ctype == 0 ? 1 : ctype == 2 ? 3 : ctype == 3 ? 1 : ctype == 4 ? 2 : ctype == 6 ? 4 : 0;
    if (!channels || !(depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16)) return fail("Unsupported PNG format");
    if (idat.size() < 2) return fail("Corrupt PNG");
    std::string raw;
    try {
        Inflater inf(reinterpret_cast<uint8_t const*>(idat.data()) + 2, idat.size() - 2, raw);
        inf.run();
    } catch (InflateError const&) {
        return fail("Corrupt PNG (zlib)");
    }
    int    bitsPerPixel = channels * depth;
    int    bpp          = std::max(1, bitsPerPixel / 8);
    px.assign(size_t(w) * size_t(h), 0);
    size_t rp = 0;
    auto   pass = [&](int x0, int y0, int dx, int dy) -> bool {
        int pw = (w - x0 + dx - 1) / dx, ph = (h - y0 + dy - 1) / dy;
        if (pw <= 0 || ph <= 0) return true;
        size_t               stride = (size_t(pw) * size_t(bitsPerPixel) + 7) / 8;
        std::vector<uint8_t> prev(stride, 0), cur(stride);
        for (int y = 0; y < ph; y++) {
            if (rp + 1 + stride > raw.size()) return false;
            int f = uint8_t(raw[rp++]);
            for (size_t i = 0; i < stride; i++) {
                int x = uint8_t(raw[rp + i]);
                int a = i >= size_t(bpp) ? cur[i - size_t(bpp)] : 0, b = prev[i], c = i >= size_t(bpp) ? prev[i - size_t(bpp)] : 0;
                switch (f) {
                case 0: break;
                case 1: x += a; break;
                case 2: x += b; break;
                case 3: x += (a + b) / 2; break;
                case 4: {
                    int p = a + b - c, pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
                    x += pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
                    break;
                }
                default: return false;
                }
                cur[i] = uint8_t(x);
            }
            rp += stride;
            for (int xx = 0; xx < pw; xx++) {
                auto sample = [&](int ch) -> uint16_t {
                    if (depth == 8) return cur[size_t(xx * channels + ch)];
                    if (depth == 16) return uint16_t(cur[size_t((xx * channels + ch) * 2)] << 8 | cur[size_t((xx * channels + ch) * 2 + 1)]);
                    size_t bit = size_t(xx * channels + ch) * size_t(depth);
                    return uint16_t(cur[bit / 8] >> (8 - depth - int(bit % 8)) & ((1 << depth) - 1));
                };
                auto to8 = [&](uint16_t v) -> uint8_t {
                    if (depth == 16) return uint8_t(v >> 8);
                    if (depth == 8) return uint8_t(v);
                    return uint8_t(v * 255 / ((1 << depth) - 1));
                };
                uint8_t r, g, b, al = 255;
                if (ctype == 3) {
                    int i = sample(0);
                    r = pal[i][0], g = pal[i][1], b = pal[i][2], al = pal[i][3];
                } else if (ctype == 0 || ctype == 4) {
                    uint16_t v = sample(0);
                    r = g = b = to8(v);
                    if (ctype == 4) al = to8(sample(1));
                    else if (hasTrns && v == trnsKey[0]) al = 0;
                } else {
                    uint16_t rv = sample(0), gv = sample(1), bv = sample(2);
                    r = to8(rv), g = to8(gv), b = to8(bv);
                    if (ctype == 6) al = to8(sample(3));
                    else if (hasTrns && rv == trnsKey[0] && gv == trnsKey[1] && bv == trnsKey[2]) al = 0;
                }
                size_t X = size_t(x0 + xx * dx), Y = size_t(y0 + y * dy);
                px[Y * size_t(w) + X] = uint32_t(al) << 24 | uint32_t(b) << 16 | uint32_t(g) << 8 | r;
            }
            std::swap(prev, cur);
        }
        return true;
    };
    bool ok = true;
    if (interlace) {
        static int const a7[7][4] = {{0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4}, {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2}};
        for (auto const& p : a7) ok = ok && pass(p[0], p[1], p[2], p[3]);
    } else ok = pass(0, 0, 1, 1);
    if (!ok) return fail("Corrupt PNG");
    return true;
}

std::string pngEncode(int w, int h, std::vector<uint32_t> const& px) {
    std::string out("\x89PNG\r\n\x1a\n", 8);
    auto chunk = [&](char const* type, std::string const& body) {
        uint8_t len[4] = {uint8_t(body.size() >> 24), uint8_t(body.size() >> 16), uint8_t(body.size() >> 8), uint8_t(body.size())};
        out.append(reinterpret_cast<char*>(len), 4);
        std::string tb = std::string(type, 4) + body;
        out += tb;
        uint32_t c = crc32(reinterpret_cast<uint8_t const*>(tb.data()), tb.size());
        uint8_t  cb[4] = {uint8_t(c >> 24), uint8_t(c >> 16), uint8_t(c >> 8), uint8_t(c)};
        out.append(reinterpret_cast<char*>(cb), 4);
    };
    std::string ihdr;
    for (uint32_t v : {uint32_t(w), uint32_t(h)}) {
        ihdr += char(v >> 24);
        ihdr += char(v >> 16);
        ihdr += char(v >> 8);
        ihdr += char(v);
    }
    ihdr += char(8);
    ihdr += char(6);
    ihdr += std::string(3, '\0');
    chunk("IHDR", ihdr);
    std::string raw;
    raw.reserve(size_t(h) * (size_t(w) * 4 + 1));
    for (int y = 0; y < h; y++) {
        raw += '\0';
        for (int x = 0; x < w; x++) {
            uint32_t p = px[size_t(y) * size_t(w) + size_t(x)];
            raw += char(p & 255);
            raw += char(p >> 8 & 255);
            raw += char(p >> 16 & 255);
            raw += char(p >> 24 & 255);
        }
    }
    std::string z("\x78\x01", 2);
    size_t      off = 0;
    do {
        size_t n    = std::min<size_t>(65535, raw.size() - off);
        bool   last = off + n >= raw.size();
        z += char(last ? 1 : 0);
        z += char(n & 255);
        z += char(n >> 8);
        z += char(~n & 255);
        z += char((~n >> 8) & 255);
        z.append(raw, off, n);
        off += n;
    } while (off < raw.size());
    uint32_t a = 1, b = 0;
    for (unsigned char c : raw) {
        a = (a + c) % 65521;
        b = (b + a) % 65521;
    }
    uint32_t ad = b << 16 | a;
    z += char(ad >> 24);
    z += char(ad >> 16);
    z += char(ad >> 8);
    z += char(ad);
    chunk("IDAT", z);
    chunk("IEND", "");
    return out;
}

// =====================================================================================================================
// texture registry (AvatarRenderer.textures / customTextures / textureSets)
// =====================================================================================================================
namespace {
char const kRegKey = 0;
struct TexRegistry {
    std::vector<Tex*>           avatarList;
    std::map<std::string, Tex*> avatarTex;
    std::vector<std::string>    customOrder;
    std::map<std::string, Tex*> customTex;
    std::vector<TexSet>         sets;
    std::string                 owner;
};
TexRegistry& reg(lua_State* L) {
    lua_pushlightuserdata(L, (void*)&kRegKey);
    lua_rawget(L, LUA_REGISTRYINDEX);
    auto* r = static_cast<TexRegistry*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return *r;
}
std::string randomUuid() {
    static std::mt19937_64 rng(std::random_device{}());
    uint64_t a = rng(), b = rng();
    a = (a & 0xFFFFFFFFFFFF0FFFull) | 0x4000ull;
    b = (b & 0x3FFFFFFFFFFFFFFFull) | 0x8000000000000000ull;
    char buf[40];
    std::snprintf(buf, sizeof buf, "%08x-%04x-%04x-%04x-%012llx", unsigned(a >> 32), unsigned(a >> 16 & 0xFFFF),
                  unsigned(a & 0xFFFF), unsigned(b >> 48), static_cast<unsigned long long>(b & 0xFFFFFFFFFFFFull));
    return buf;
}
std::string readFileBin(std::filesystem::path const& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
} // namespace

int TexSet::width() const {
    for (Tex* x : t)
        if (x) { texSize(*x); return x->w; }
    return -1;
}
int TexSet::height() const {
    for (Tex* x : t)
        if (x) { texSize(*x); return x->h; }
    return -1;
}
std::vector<TexSet>& texSets(lua_State* L) { return reg(L).sets; }

void texSize(Tex& t) {
    if (t.sized) return;
    t.sized = true;
    if (t.bytes.empty() && !t.file.empty()) {
        // header only: 8 signature + IHDR
        std::ifstream f(t.file, std::ios::binary);
        char          hdr[24];
        if (f.read(hdr, 24)) {
            auto const* u = reinterpret_cast<uint8_t const*>(hdr);
            if (std::memcmp(hdr + 12, "IHDR", 4) == 0) {
                t.w = int(be32(u + 16));
                t.h = int(be32(u + 20));
                return;
            }
        }
    }
    std::string const& b = t.bytes;
    if (b.size() >= 24 && std::memcmp(b.data() + 12, "IHDR", 4) == 0) {
        auto const* u = reinterpret_cast<uint8_t const*>(b.data());
        t.w = int(be32(u + 16));
        t.h = int(be32(u + 20));
        return;
    }
    t.w = t.h = 1; // unreadable: NativeImage(1, 1) fallback
}

void texDecode(lua_State* L, Tex& t) {
    (void)L;
    if (t.decoded) return;
    t.decoded = true;
    std::string data = t.bytes;
    if (data.empty() && !t.file.empty()) data = readFileBin(t.file);
    int         w = 0, h = 0;
    std::string err;
    if (!data.empty() && pngDecode(data, w, h, t.px, &err)) {
        t.w = w;
        t.h = h;
    } else if (t.sized && t.w > 0 && t.h > 0 && data.empty()) {
        t.px.assign(size_t(t.w) * size_t(t.h), 0); // size-only texture (blank)
    } else {
        t.w = t.h = 1; // FiguraTexture: unreadable image -> new NativeImage(1, 1, true)
        t.px.assign(1, 0);
    }
    t.sized = true;
    t.bytes.clear();
    t.bytes.shrink_to_fit();
}

Tex* texByName(lua_State* L, std::string const& name, bool customOnly, bool avatarOnly) {
    TexRegistry& r = reg(L);
    if (!avatarOnly) {
        auto it = r.customTex.find(name);
        if (it != r.customTex.end()) return it->second;
    }
    if (!customOnly) {
        auto it = r.avatarTex.find(name);
        if (it != r.avatarTex.end()) return it->second;
    }
    return nullptr;
}

// =====================================================================================================================
// Texture (FiguraTexture)
// =====================================================================================================================
namespace {
Tex* self(lua_State* L, char const* fn) { return check<Tex>(L, 1, tTexture, fn); }
int  ret1(lua_State* L) { lua_settop(L, 1); return 1; }

struct TexClosed {};
Tex& live(lua_State* L, Tex& t) {
    texDecode(L, t);
    if (t.px.empty()) luaL_error(L, "Image is not allocated.");
    return t;
}
[[noreturn]] void outside(lua_State* L, Tex const& t, int x, int y) {
    luaL_error(L, "(%d, %d) outside of image bounds (%d, %d)", x, y, t.w, t.h);
    for (;;) {}
}
uint32_t getPx(lua_State* L, Tex& t, int x, int y) {
    live(L, t);
    if (x < 0 || y < 0 || x >= t.w || y >= t.h) outside(L, t, x, y);
    return t.px[size_t(y) * size_t(t.w) + size_t(x)];
}
void setPx(lua_State* L, Tex& t, int x, int y, uint32_t c) {
    live(L, t);
    if (x < 0 || y < 0 || x >= t.w || y >= t.h) outside(L, t, x, y);
    t.px[size_t(y) * size_t(t.w) + size_t(x)] = c;
}
void backupImage(Tex& t) {
    t.modified = true;
    if (!t.hasBackup) {
        t.backup    = t.px;
        t.hasBackup = true;
    }
}
/// ColorUtils.rgbaToIntABGR (Java int arithmetic, no clamping)
uint32_t rgbaToABGR(V4 c) {
    int hex = javaInt(c.w * 0xFF);
    hex = int(unsigned(hex) << 8) + javaInt(c.z * 0xFF);
    hex = int(unsigned(hex) << 8) + javaInt(c.y * 0xFF);
    hex = int(unsigned(hex) << 8) + javaInt(c.x * 0xFF);
    return uint32_t(hex);
}
void pushABGR(lua_State* L, uint32_t c) { // ColorUtils.abgrToRGBA
    pushVec4(L, fl((c & 0xFF) / 255.0f), fl((c >> 8 & 0xFF) / 255.0f), fl((c >> 16 & 0xFF) / 255.0f), fl((c >> 24 & 0xFF) / 255.0f));
}
int argInt(lua_State* L, int idx) { return javaInt(luaL_checknumber(L, idx)); }

int texGetName(lua_State* L) { lua_pushstring(L, self(L, "getName")->name.c_str()); return 1; }
int texGetPath(lua_State* L) { lua_pushstring(L, self(L, "getPath")->location.c_str()); return 1; }
int texGetDims(lua_State* L) { Tex* t = self(L, "getDimensions"); texSize(*t); pushVec2(L, t->w, t->h); return 1; }
int texGetPixel(lua_State* L) {
    Tex* t = self(L, "getPixel");
    pushABGR(L, getPx(L, *t, argInt(L, 2), argInt(L, 3)));
    return 1;
}
int texSetPixel(lua_State* L) {
    Tex* t = self(L, "setPixel");
    int  x = argInt(L, 2), y = argInt(L, 3);
    V4   c = luParseVec4(L, 4, "setPixel", 0, 0, 0, 1);
    live(L, *t);
    backupImage(*t);
    setPx(L, *t, x, y, rgbaToABGR(c));
    return ret1(L);
}
int texFill(lua_State* L) {
    Tex* t = self(L, "fill");
    int  x = argInt(L, 2), y = argInt(L, 3), w = argInt(L, 4), h = argInt(L, 5);
    V4   c = luParseVec4(L, 6, "fill", 0, 0, 0, 1);
    live(L, *t);
    backupImage(*t);
    uint32_t col = rgbaToABGR(c);
    for (int i = x; i < x + w; i++)        // NativeImage.fillRect: column by column
        for (int j = y; j < y + h; j++) setPx(L, *t, i, j, col);
    return ret1(L);
}
int texUpdate(lua_State* L) { self(L, "update")->dirty = true; return ret1(L); }
int texRestore(lua_State* L) {
    Tex* t = self(L, "restore");
    if (t->modified) {
        t->px       = t->backup;
        t->modified = false;
    }
    return ret1(L);
}
int texSave(lua_State* L) {
    Tex* t = self(L, "save");
    live(L, *t);
    std::string b = b64Encode(pngEncode(t->w, t->h, t->px));
    lua_pushlstring(L, b.data(), b.size());
    return 1;
}
int texApplyFunc(lua_State* L) {
    Tex* t = self(L, "applyFunc");
    int  x = argInt(L, 2), y = argInt(L, 3), w = argInt(L, 4), h = argInt(L, 5);
    luaL_checktype(L, 6, LUA_TFUNCTION);
    for (int i = y; i < y + h; i++)
        for (int j = x; j < x + w; j++) {
            uint32_t c = getPx(L, *t, j, i);
            lua_pushvalue(L, 6);
            pushABGR(L, c);
            lua_pushinteger(L, j);
            lua_pushinteger(L, i);
            lua_call(L, 3, 1);
            int n = 0;
            double* v = toVec(L, -1, &n);
            if (v && n == 4) {
                V4 col{v[0], v[1], v[2], v[3]};
                backupImage(*t);
                setPx(L, *t, j, i, rgbaToABGR(col));
            }
            lua_pop(L, 1);
        }
    return ret1(L);
}
int texApplyMatrix(lua_State* L) {
    Tex* t = self(L, "applyMatrix");
    int  x = argInt(L, 2), y = argInt(L, 3), w = argInt(L, 4), h = argInt(L, 5);
    Mat4* m = toMat4(L, 6);
    if (!m) return luaL_error(L, "bad argument 6 to 'applyMatrix' (Matrix4 expected, got %s)", figuraTypeName(L, 6).c_str());
    Mat4 M    = *m;
    bool clip = lua_toboolean(L, 7) != 0;
    for (int i = y; i < y + h; i++)
        for (int j = x; j < x + w; j++) {
            uint32_t c = getPx(L, *t, j, i);
            double   v[4] = {fl((c & 0xFF) / 255.0f), fl((c >> 8 & 0xFF) / 255.0f), fl((c >> 16 & 0xFF) / 255.0f), fl((c >> 24 & 0xFF) / 255.0f)};
            double   r[4];
            for (int k = 0; k < 4; k++) r[k] = M.at(k, 0) * v[0] + M.at(k, 1) * v[1] + M.at(k, 2) * v[2] + M.at(k, 3) * v[3];
            if (clip)
                for (double& q : r) q = std::max(0.0, std::min(q, 1.0));
            backupImage(*t);
            setPx(L, *t, j, i, rgbaToABGR({r[0], r[1], r[2], r[3]}));
        }
    return ret1(L);
}
int texIndex(lua_State* L) {
    Tex* t = self(L, "__index");
    if (lua_type(L, 2) == LUA_TSTRING && std::strcmp(lua_tostring(L, 2), "name") == 0) {
        lua_pushstring(L, t->name.c_str());
        return 1;
    }
    lua_pushnil(L);
    return 1;
}
int texToString(lua_State* L) {
    Tex* t = self(L, "__tostring");
    texSize(*t);
    lua_pushfstring(L, "%s (%dx%d) (Texture)", t->name.c_str(), t->w, t->h);
    return 1;
}

// ---- runtime upload to Bedrock
void texSyncImpl(Avatar& a) {
    if (!a.L || !a.host) return;
    auto upload = [&](Tex& t, std::string const& path) {
        if (path.empty()) return;
        texDecode(a.L, t);
        if (t.w <= 0 || t.h <= 0 || t.px.size() < size_t(t.w) * size_t(t.h)) return;
        // NativeImage int = ABGR with red in the low byte: little-endian bytes are already R, G, B, A
        a.host->uploadTexture(path, t.w, t.h, reinterpret_cast<uint8_t const*>(t.px.data()));
    };
    TexRegistry& r = reg(a.L);
    for (Tex* t : r.avatarList)
        if (t && t->dirty && (t->modified || t->uploaded) && t->fileIndex >= 0 && t->fileIndex < int(a.textureFiles.size())) {
            upload(*t, a.textureFiles[size_t(t->fileIndex)].rpTexture);
            t->dirty    = false;
            t->uploaded = true;
        }
    for (auto const& f : a.textureFiles) {
        if (f.dyn < 0 || size_t(f.dyn) >= a.dynSlotTex.size() || !a.dynSlotTex[size_t(f.dyn)]) continue;
        Tex& t = *static_cast<Tex*>(a.dynSlotTex[size_t(f.dyn)]);
        if (!t.dirty) continue;
        upload(t, f.rpTexture);
        t.dirty = false;
    }
}

// ---- TextureAPI
Tex* newTex(lua_State* L, std::string const& name, bool custom) {
    Tex* t      = newOwned<Tex>(L, tTexture);
    t->name     = name;
    t->location = "figura:avatar_tex/" + reg(L).owner + (custom ? "/custom/" : "/") + randomUuid();
    return t;
}
/// Avatar.registerTexture: size check, replaces (closes) an older texture of the same name, limit 128
Tex* registerTex(lua_State* L, std::string const& name, int w, int h, std::vector<uint32_t> px) {
    int const max = 2048;
    if (w > max || h > max) {
        luaL_error(L, "Texture exceeded max size of %d x %d resolution, got %d x %d", max, max, w, h);
        return nullptr;
    }
    TexRegistry& r = reg(L);
    auto old = r.customTex.find(name);
    if (old != r.customTex.end()) { // FiguraTexture.close(): the old object stops working
        old->second->px.clear();
        old->second->backup.clear();
        old->second->decoded = true;
    }
    if (r.customTex.size() > 128) {
        luaL_error(L, "Maximum amount of textures reached!");
        return nullptr;
    }
    Tex* t     = newTex(L, name, true);
    t->w       = w;
    t->h       = h;
    t->sized   = true;
    t->decoded = true;
    t->px      = std::move(px);
    if (old == r.customTex.end()) r.customOrder.push_back(name);
    r.customTex[name] = t;
    // keep a strong reference (the cache also does, this documents ownership)
    return t;
}
int apiNewTexture(lua_State* L) {
    std::string name = optStr(L, 1, "");
    int         w = argInt(L, 2), h = argInt(L, 3);
    if (w <= 0 || h <= 0) return luaL_error(L, "Invalid texture size: %dx%d", w, h);
    Tex* t = registerTex(L, name, w, h, std::vector<uint32_t>(size_t(w) * size_t(h), 0));
    // fill with ColorUtils.Colors.AWESOME_BLUE (0x5EA5FF), alpha 1
    V4 c{fl(0x5E / 255.0f), fl(0xA5 / 255.0f), fl(0xFF / 255.0f), 1.0};
    backupImage(*t);
    std::fill(t->px.begin(), t->px.end(), rgbaToABGR(c));
    return 1;
}
int apiRead(lua_State* L) {
    std::string name = optStr(L, 1, "");
    std::string bytes;
    if (lua_type(L, 2) == LUA_TTABLE) {
        size_t n = lua_rawlen(L, 2);
        for (size_t i = 1; i <= n; i++) {
            lua_rawgeti(L, 2, int(i));
            if (!lua_isnumber(L, -1)) return luaL_error(L, "bad argument (number expected, got %s)", luaL_typename(L, -1));
            bytes += char(javaInt(lua_tonumber(L, -1)));
            lua_pop(L, 1);
        }
    } else if (lua_type(L, 2) == LUA_TSTRING) {
        std::string err;
        if (!b64Decode(lua_tostring(L, 2), bytes, &err)) return luaL_error(L, "%s", err.c_str());
    } else {
        return luaL_error(L, "Invalid type for read \"%s\"", lua_isnoneornil(L, 2) ? "null" : figuraTypeName(L, 2).c_str());
    }
    int                   w = 0, h = 0;
    std::vector<uint32_t> px;
    std::string           err;
    if (!pngDecode(bytes, w, h, px, &err)) return luaL_error(L, "Could not read image: %s", err.c_str());
    registerTex(L, name, w, h, std::move(px));
    return 1;
}
int apiCopy(lua_State* L) {
    std::string name = optStr(L, 1, "");
    Tex*        src  = check<Tex>(L, 2, tTexture, "copy");
    live(L, *src);
    registerTex(L, name, src->w, src->h, src->px);
    return 1;
}
int apiGet(lua_State* L) {
    std::string name = optStr(L, 1, "");
    pushRef(L, texByName(L, name, true, false), tTexture);
    return 1;
}
int apiGetTextures(lua_State* L) {
    TexRegistry& r = reg(L);
    lua_createtable(L, int(r.avatarList.size()), 0);
    int i = 1;
    for (Tex* t : r.avatarList) {
        pushRef(L, t, tTexture);
        lua_rawseti(L, -2, i++);
    }
    return 1;
}
int apiFromVanilla(lua_State* L) {
    std::string name = optStr(L, 1, "");
    std::string path = luaL_checkstring(L, 2);
    // vanilla textures the compiler found named in the scripts ship in the bundle (rig "vanillaTextures")
    if (Avatar* av = avatarOf(L)) {
        std::string key = path.find(':') == std::string::npos ? "minecraft:" + path : path;
        auto it = av->vanillaTextures.find(key);
        if (it != av->vanillaTextures.end()) {
            std::string data = readFileBin(av->dir / it->second);
            int w = 0, h = 0;
            std::vector<uint32_t> px;
            if (!data.empty() && pngDecode(data, w, h, px, nullptr)) {
                registerTex(L, name, w, h, std::move(px));
                return 1;
            }
        }
    }
    // other Minecraft resources are not available: "missing" -> MissingTextureAtlasSprite (16x16)
    std::vector<uint32_t> px(256);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) px[size_t(y * 16 + x)] = ((y < 8) ^ (x < 8)) ? 0xFFF800F8u : 0xFF000000u;
    registerTex(L, name, 16, 16, std::move(px));
    return 1;
}
int apiIndex(lua_State* L) {
    if (lua_type(L, 2) != LUA_TSTRING) { lua_pushnil(L); return 1; }
    pushRef(L, texByName(L, lua_tostring(L, 2), false, false), tTexture);
    return 1;
}
} // namespace

void openTextureApi(Avatar& a) {
    lua_State* L = a.L;
    TypeDef td;
    td.name    = "Texture";
    td.methods = {{"__index", texIndex}, {"getName", texGetName}, {"getPath", texGetPath},
                  {"getDimensions", texGetDims}, {"getPixel", texGetPixel}, {"setPixel", texSetPixel},
                  {"pixel", texSetPixel}, {"fill", texFill}, {"update", texUpdate}, {"restore", texRestore},
                  {"save", texSave}, {"applyFunc", texApplyFunc}, {"applyMatrix", texApplyMatrix}};
    td.index    = texIndex;
    td.tostring = texToString;
    tTexture    = registerType(L, td);

    // registry object
    auto* r = static_cast<TexRegistry*>(lua_newuserdata(L, sizeof(TexRegistry)));
    new (r) TexRegistry();
    lua_newtable(L);
    lua_pushcfunction(L, [](lua_State* L2) -> int {
        static_cast<TexRegistry*>(lua_touserdata(L2, 1))->~TexRegistry();
        return 0;
    });
    lua_setfield(L, -2, "__gc");
    lua_setmetatable(L, -2);
    lua_pushlightuserdata(L, (void*)&kRegKey);
    lua_insert(L, -2);
    lua_rawset(L, LUA_REGISTRYINDEX);
    r->owner = a.player.uuid.empty() ? randomUuid() : a.player.uuid;

    // avatar textures (AvatarRenderer: nbt "textures"."src") + sets ("textures"."data")
    if (a.nbtJson) {
        json::Value const& tx = (*a.nbtJson)["textures"];
        for (auto const& [key, v] : tx["src"].members()) {
            Tex* t = newTex(L, key, false);
            lua_pop(L, 1); // kept alive by the reference cache
            t->avatarTexture = true;
            t->fileIndex     = a.fileIndexByFiguraName(key);
            if (v.isObject() && v.has("__file")) {
                std::filesystem::path f = a.dir / v["__file"].str();
                if (!std::filesystem::exists(f)) f = a.dir.parent_path() / v["__file"].str();
                t->file = f;
            } else if (v.isArray() && v.size() >= 2 && v[size_t(0)].isNumber()) {
                if (v.size() == 2) { // [w, h] = blank texture
                    t->w = int(v[size_t(0)].asNumber());
                    t->h = int(v[size_t(1)].asNumber());
                    t->sized = true;
                } else { // raw byte array
                    for (size_t i = 0; i < v.size(); i++) t->bytes += char(int(v[i].asNumber()));
                }
            } else if (v.isString()) {
                std::string out;
                if (b64Decode(v.str(), out, nullptr)) t->bytes = out;
            }
            r->avatarList.push_back(t);
            r->avatarTex[key] = t;
        }
        auto texOf = [&](json::Value const& d, char const* k) -> Tex* {
            if (!d.has(k)) return nullptr;
            auto it = r->avatarTex.find(d[k].str());
            return it == r->avatarTex.end() ? nullptr : it->second;
        };
        for (auto const& d : tx["data"].items()) {
            TexSet s;
            std::string n = d["d"].str();
            if (n.empty()) for (char const* k : {"e", "s", "n"}) {
                std::string v = d[k].str();
                if (!v.empty()) { n = v.substr(0, v.size() - 2); break; }
            }
            s.name = n;
            s.t[0] = texOf(d, "d");
            s.t[1] = texOf(d, "e");
            s.t[2] = texOf(d, "s");
            s.t[3] = texOf(d, "n");
            r->sets.push_back(s);
        }
    }

    TypeDef ta;
    ta.name     = "TextureAPI";
    ta.methods  = {{"__index", apiIndex}, {"getTextures", [](lua_State* L2) { return apiGetTextures(L2); }},
                   {"get", [](lua_State* L2) { lua_remove(L2, 1); return apiGet(L2); }},
                   {"newTexture", [](lua_State* L2) { lua_remove(L2, 1); return apiNewTexture(L2); }},
                   {"read", [](lua_State* L2) { lua_remove(L2, 1); return apiRead(L2); }},
                   {"copy", [](lua_State* L2) { lua_remove(L2, 1); return apiCopy(L2); }},
                   {"fromVanilla", [](lua_State* L2) { lua_remove(L2, 1); return apiFromVanilla(L2); }}};
    ta.index    = apiIndex;
    ta.tostring = [](lua_State* L2) { lua_pushstring(L2, "TextureAPI"); return 1; };
    uint32_t tTexApi = registerType(L, ta);
    static int texApiAnchor;
    pushRef(L, &texApiAnchor, tTexApi);
    lua_setglobal(L, "textures");
}

void texMarkDirty(Avatar& a) {
    if (!a.L) return;
    TexRegistry& r = reg(a.L);
    for (Tex* t : r.avatarList)
        if (t && t->uploaded) t->dirty = true;
    for (void* p : a.dynSlotTex)
        if (p) static_cast<Tex*>(p)->dirty = true;
}
void texSync(Avatar& a) { texSyncImpl(a); }

} // namespace bf::figura
