// PC test harness for spotpass.cpp: PS AES-CTR is emulated with OpenSSL and
// the all-zero test key used by badge-arcade-offline's test fixtures.
#include <3ds.h>
#include <openssl/evp.h>
#include <cstdio>
#include <cstring>
#include <iostream>
#include "spotpass.hpp"

extern "C" Result PS_EncryptDecryptAes(u32 size, u8* in, u8* out, PS_AESAlgorithm algo, PS_AESKeyType key, u8* iv) {
    if (algo != PS_ALGORITHM_CTR_DEC || key != PS_KEYSLOT_38 || size % 16) return -1;
    static const unsigned char zero_key[16] = {};
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    int len = 0;
    EVP_DecryptInit_ex(ctx, EVP_aes_128_ctr(), nullptr, zero_key, iv);
    EVP_DecryptUpdate(ctx, out, &len, in, int(size));
    EVP_CIPHER_CTX_free(ctx);
    // Advance the big-endian 128-bit counter like the console does.
    unsigned blocks = size / 16;
    for (int i = 15; i >= 0 && blocks; --i) {
        unsigned sum = iv[i] + (blocks & 0xFF);
        iv[i] = u8(sum);
        blocks = (blocks >> 8) + (sum >> 8);
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc != 3) { std::cerr << "usage: spotpass-host PACK CACHE_DIR\n"; return 2; }
    try {
        std::cout << spotpass::open_pack(argv[1], argv[2]) << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
