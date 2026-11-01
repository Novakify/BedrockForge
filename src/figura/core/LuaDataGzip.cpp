// FiguraBridge: tiny gzip reader (RFC 1952 + RFC 1951 inflate) for ResourcesAPI, which serves the avatar's
// "resources" byte arrays through a GZIPInputStream exactly like Figura. Self-contained (no zlib in the build).
// Errors use java.util.zip.GZIPInputStream / Inflater messages.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace bf::figura::data {

namespace {
struct Bits {
    uint8_t const* p;
    size_t         n, pos = 0;
    uint32_t       buf = 0;
    int            cnt = 0;
    bool           eof = false;
    int bit() {
        if (cnt == 0) {
            if (pos >= n) { eof = true; return 0; }
            buf = p[pos++];
            cnt = 8;
        }
        int b = buf & 1;
        buf >>= 1;
        cnt--;
        return b;
    }
    uint32_t bits(int k) {
        uint32_t v = 0;
        for (int i = 0; i < k; i++) v |= uint32_t(bit()) << i;
        return v;
    }
};

struct Huff {
    uint16_t count[16]{};
    uint16_t sym[320]{};
    // returns false on over-subscribed / incomplete (allowed only for single-code distance trees)
    int build(uint8_t const* len, int n) {
        std::memset(count, 0, sizeof count);
        for (int i = 0; i < n; i++) count[len[i]]++;
        if (count[0] == n) return 0;
        int left = 1;
        for (int i = 1; i < 16; i++) {
            left <<= 1;
            left -= count[i];
            if (left < 0) return -1;
        }
        uint16_t offs[16];
        offs[1] = 0;
        for (int i = 1; i < 15; i++) offs[i + 1] = uint16_t(offs[i] + count[i]);
        for (int i = 0; i < n; i++)
            if (len[i]) sym[offs[len[i]]++] = uint16_t(i);
        return left;
    }
    int decode(Bits& b) const {
        int code = 0, first = 0, index = 0;
        for (int l = 1; l < 16; l++) {
            code |= b.bit();
            int c = count[l];
            if (code - c < first) return sym[index + (code - first)];
            index += c;
            first += c;
            first <<= 1;
            code <<= 1;
            if (b.eof) return -1;
        }
        return -2;
    }
};

const uint16_t kLBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint16_t kLExt[29]  = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const uint16_t kDExt[30]  = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool codes(Bits& b, std::vector<uint8_t>& out, Huff const& lc, Huff const& dc, std::string* err) {
    for (;;) {
        int s = lc.decode(b);
        if (s < 0) { *err = s == -1 ? "Unexpected end of ZLIB input stream" : "invalid literal/length code"; return false; }
        if (s < 256) { out.push_back(uint8_t(s)); continue; }
        if (s == 256) return true;
        s -= 257;
        if (s >= 29) { *err = "invalid literal/length code"; return false; }
        size_t len = kLBase[s] + b.bits(kLExt[s]);
        int    d   = dc.decode(b);
        if (d < 0 || d >= 30) { *err = d == -1 ? "Unexpected end of ZLIB input stream" : "invalid distance code"; return false; }
        size_t dist = kDBase[d] + b.bits(kDExt[d]);
        if (b.eof) { *err = "Unexpected end of ZLIB input stream"; return false; }
        if (dist > out.size()) { *err = "invalid distance too far back"; return false; }
        size_t from = out.size() - dist;
        for (size_t i = 0; i < len; i++) out.push_back(out[from + i]);
    }
}

bool inflateRaw(Bits& b, std::vector<uint8_t>& out, std::string* err) {
    int last;
    do {
        last     = b.bit();
        int type = int(b.bits(2));
        if (b.eof) { *err = "Unexpected end of ZLIB input stream"; return false; }
        if (type == 0) {
            b.buf = 0;
            b.cnt = 0;
            if (b.pos + 4 > b.n) { *err = "Unexpected end of ZLIB input stream"; return false; }
            unsigned len  = b.p[b.pos] | b.p[b.pos + 1] << 8;
            unsigned nlen = b.p[b.pos + 2] | b.p[b.pos + 3] << 8;
            b.pos += 4;
            if (len != (~nlen & 0xffff)) { *err = "invalid stored block lengths"; return false; }
            if (b.pos + len > b.n) { *err = "Unexpected end of ZLIB input stream"; return false; }
            out.insert(out.end(), b.p + b.pos, b.p + b.pos + len);
            b.pos += len;
        } else if (type == 1) {
            static Huff lc, dc;
            static bool init = false;
            if (!init) {
                uint8_t l[288];
                int     i = 0;
                for (; i < 144; i++) l[i] = 8;
                for (; i < 256; i++) l[i] = 9;
                for (; i < 280; i++) l[i] = 7;
                for (; i < 288; i++) l[i] = 8;
                lc.build(l, 288);
                for (i = 0; i < 30; i++) l[i] = 5;
                dc.build(l, 30);
                init = true;
            }
            if (!codes(b, out, lc, dc, err)) return false;
        } else if (type == 2) {
            int nlen = int(b.bits(5)) + 257, ndist = int(b.bits(5)) + 1, ncode = int(b.bits(4)) + 4;
            if (nlen > 286 || ndist > 30) { *err = "too many length or distance symbols"; return false; }
            static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            uint8_t lengths[320]{};
            for (int i = 0; i < ncode; i++) lengths[order[i]] = uint8_t(b.bits(3));
            Huff lencode, distcode;
            if (lencode.build(lengths, 19) != 0) { *err = "invalid code lengths set"; return false; }
            int idx = 0;
            while (idx < nlen + ndist) {
                int s = lencode.decode(b);
                if (s < 0) { *err = s == -1 ? "Unexpected end of ZLIB input stream" : "invalid code lengths set"; return false; }
                if (s < 16) { lengths[idx++] = uint8_t(s); continue; }
                int len = 0, rep;
                if (s == 16) {
                    if (idx == 0) { *err = "invalid bit length repeat"; return false; }
                    len = lengths[idx - 1];
                    rep = 3 + int(b.bits(2));
                } else if (s == 17) rep = 3 + int(b.bits(3));
                else rep = 11 + int(b.bits(7));
                if (idx + rep > nlen + ndist) { *err = "invalid bit length repeat"; return false; }
                while (rep--) lengths[idx++] = uint8_t(len);
            }
            if (lengths[256] == 0) { *err = "invalid code -- missing end-of-block"; return false; }
            int e = lencode.build(lengths, nlen);
            if (e < 0 || (e > 0 && nlen - lencode.count[0] != 1)) { *err = "invalid literal/lengths set"; return false; }
            e = distcode.build(lengths + nlen, ndist);
            if (e < 0 || (e > 0 && ndist - distcode.count[0] != 1)) { *err = "invalid distances set"; return false; }
            if (!codes(b, out, lencode, distcode, err)) return false;
        } else {
            *err = "invalid block type";
            return false;
        }
    } while (!last);
    return true;
}

uint32_t crc32(uint8_t const* d, size_t n) {
    static uint32_t table[256];
    static bool     init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = table[(c ^ d[i]) & 0xff] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}
} // namespace

/// GZIPInputStream semantics: header errors -> "java.util.zip.ZipException: Not in GZIP format" etc.
/// err receives "<java exception class>: <message>". Concatenated members are decoded like Java does.
bool gunzip(std::vector<uint8_t> const& in, std::vector<uint8_t>& out, std::string* err) {
    size_t pos = 0;
    bool   first = true;
    while (true) {
        if (in.size() - pos < 10 || in[pos] != 0x1f || in[pos + 1] != 0x8b) {
            if (first) {
                *err = in.size() - pos < 2 ? "java.io.EOFException" : "java.util.zip.ZipException: Not in GZIP format";
                return false;
            }
            return true; // trailing garbage after a member is ignored by GZIPInputStream
        }
        if (in[pos + 2] != 8) { *err = "java.util.zip.ZipException: Unsupported compression method"; return false; }
        uint8_t flg = in[pos + 3];
        size_t  p   = pos + 10;
        auto need = [&](size_t k) { return p + k <= in.size(); };
        if (flg & 4) {
            if (!need(2)) { *err = "java.io.EOFException"; return false; }
            size_t xlen = in[p] | in[p + 1] << 8;
            p += 2 + xlen;
        }
        if (flg & 8) { while (p < in.size() && in[p]) p++; p++; }
        if (flg & 16) { while (p < in.size() && in[p]) p++; p++; }
        if (flg & 2) p += 2;
        if (p > in.size()) { *err = "java.io.EOFException"; return false; }
        Bits        b{in.data() + p, in.size() - p};
        size_t      start = out.size();
        std::string e;
        if (!inflateRaw(b, out, &e)) {
            *err = (e.rfind("Unexpected end", 0) == 0 ? "java.io.EOFException: " : "java.util.zip.ZipException: ") + e;
            return false;
        }
        p += b.pos;
        if (p + 8 > in.size()) { *err = "java.io.EOFException: Unexpected end of ZLIB input stream"; return false; }
        uint32_t crc  = in[p] | in[p + 1] << 8 | in[p + 2] << 16 | uint32_t(in[p + 3]) << 24;
        uint32_t isz  = in[p + 4] | in[p + 5] << 8 | in[p + 6] << 16 | uint32_t(in[p + 7]) << 24;
        if (crc != crc32(out.data() + start, out.size() - start) || isz != uint32_t(out.size() - start)) {
            *err = "java.util.zip.ZipException: Corrupt GZIP trailer";
            return false;
        }
        pos   = p + 8;
        first = false;
        if (pos >= in.size()) return true;
    }
}

} // namespace bf::figura::data
