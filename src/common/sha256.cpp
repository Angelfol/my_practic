#include "sha256.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <random>

#if __has_include(<openssl/evp.h>)
// Основная ветка (Linux/Beget): libcrypto из OpenSSL, интерфейс EVP.
#define APTEKA_HAVE_OPENSSL 1
#include <openssl/evp.h>
#endif

namespace apteka {

namespace {

const char* kHex = "0123456789abcdef";

std::string to_hex(const unsigned char* data, size_t n) {
    std::string out(n * 2, '0');
    for (size_t i = 0; i < n; ++i) {
        out[2 * i] = kHex[data[i] >> 4];
        out[2 * i + 1] = kHex[data[i] & 0x0f];
    }
    return out;
}

#ifndef APTEKA_HAVE_OPENSSL
// Портативная реализация SHA-256 (FIPS 180-4) для сборки без OpenSSL —
// используется только при локальной разработке; дайджесты идентичны libcrypto.
struct Sha256Ctx {
    uint32_t h[8];
    uint64_t total = 0;
    unsigned char buf[64];
    size_t buf_len = 0;
};

constexpr uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t rotr(uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }

void sha_init(Sha256Ctx& c) {
    static const uint32_t init[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372,
                                     0xa54ff53a, 0x510e527f, 0x9b05688c,
                                     0x1f83d9ab, 0x5be0cd19};
    std::memcpy(c.h, init, sizeof(init));
    c.total = 0;
    c.buf_len = 0;
}

void sha_block(Sha256Ctx& c, const unsigned char* p) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (uint32_t(p[4 * i]) << 24) | (uint32_t(p[4 * i + 1]) << 16) |
               (uint32_t(p[4 * i + 2]) << 8) | uint32_t(p[4 * i + 3]);
    }
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = c.h[0], b = c.h[1], cc = c.h[2], d = c.h[3];
    uint32_t e = c.h[4], f = c.h[5], g = c.h[6], h = c.h[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + s1 + ch + K[i] + w[i];
        uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & cc) ^ (b & cc);
        uint32_t t2 = s0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = cc; cc = b; b = a; a = t1 + t2;
    }
    c.h[0] += a; c.h[1] += b; c.h[2] += cc; c.h[3] += d;
    c.h[4] += e; c.h[5] += f; c.h[6] += g; c.h[7] += h;
}

void sha_update(Sha256Ctx& c, const unsigned char* p, size_t n) {
    c.total += n;
    while (n > 0) {
        size_t take = 64 - c.buf_len;
        if (take > n) take = n;
        std::memcpy(c.buf + c.buf_len, p, take);
        c.buf_len += take;
        p += take;
        n -= take;
        if (c.buf_len == 64) {
            sha_block(c, c.buf);
            c.buf_len = 0;
        }
    }
}

void sha_final(Sha256Ctx& c, unsigned char out[32]) {
    uint64_t bits = c.total * 8;
    unsigned char pad = 0x80;
    sha_update(c, &pad, 1);
    unsigned char zero = 0;
    while (c.buf_len != 56) sha_update(c, &zero, 1);
    unsigned char len[8];
    for (int i = 0; i < 8; ++i) len[i] = static_cast<unsigned char>(bits >> (56 - 8 * i));
    sha_update(c, len, 8);
    for (int i = 0; i < 8; ++i) {
        out[4 * i] = static_cast<unsigned char>(c.h[i] >> 24);
        out[4 * i + 1] = static_cast<unsigned char>(c.h[i] >> 16);
        out[4 * i + 2] = static_cast<unsigned char>(c.h[i] >> 8);
        out[4 * i + 3] = static_cast<unsigned char>(c.h[i]);
    }
}
#endif // !APTEKA_HAVE_OPENSSL

} // namespace

std::string sha256_hex(const std::string& data) {
    unsigned char md[32];
#ifdef APTEKA_HAVE_OPENSSL
    unsigned int md_len = 0;
    EVP_Digest(data.data(), data.size(), md, &md_len, EVP_sha256(), nullptr);
#else
    Sha256Ctx ctx;
    sha_init(ctx);
    sha_update(ctx, reinterpret_cast<const unsigned char*>(data.data()), data.size());
    sha_final(ctx, md);
#endif
    return to_hex(md, 32);
}

std::string random_hex(size_t n_bytes) {
    std::string bytes(n_bytes, '\0');
    std::ifstream urandom("/dev/urandom", std::ios::binary);
    if (urandom && urandom.read(&bytes[0], static_cast<std::streamsize>(n_bytes))) {
        return to_hex(reinterpret_cast<const unsigned char*>(bytes.data()), n_bytes);
    }
    // Вне POSIX (локальная разработка под Windows): std::random_device.
    std::random_device rd;
    for (size_t i = 0; i < n_bytes; ++i) {
        bytes[i] = static_cast<char>(rd() & 0xff);
    }
    return to_hex(reinterpret_cast<const unsigned char*>(bytes.data()), n_bytes);
}

} // namespace apteka
