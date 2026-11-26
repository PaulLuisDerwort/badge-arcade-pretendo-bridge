#include "spotpass.hpp"

#include <3ds.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <sys/stat.h>
#include <vector>

namespace spotpass {
namespace {

constexpr uint64_t EUR_TITLE_ID = 0x0004000000153600ULL;
constexpr size_t BOSS_HEADER = 0x28;   // plain container header
constexpr size_t BODY_HEADER = 0x26E;  // encrypted headers before the payload
constexpr size_t CHUNK = 0x10000;      // multiple of the AES block size

using File = std::unique_ptr<FILE, int (*)(FILE*)>;

File open_file(const std::string& path, const char* mode) {
    FILE* f = fopen(path.c_str(), mode);
    if (!f) throw std::runtime_error("Cannot open " + path + ": " + strerror(errno));
    return File(f, fclose);
}

uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
uint64_t be64(const uint8_t* p) { return uint64_t(be32(p)) << 32 | be32(p + 4); }

std::string base_name(const std::string& path) {
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// Decrypts `size` bytes in place, continuing the CTR counter in `ctr`.
void decrypt(uint8_t* data, size_t size, std::array<uint8_t, 16>& ctr) {
    size_t padded = (size + 15) & ~size_t(15);
    std::vector<uint8_t> in(padded, 0), out(padded, 0);
    std::memcpy(in.data(), data, size);
    Result r = PS_EncryptDecryptAes(padded, in.data(), out.data(), PS_ALGORITHM_CTR_DEC, PS_KEYSLOT_38, ctr.data());
    if (R_FAILED(r)) throw std::runtime_error("Console AES (ps:ps) failed");
    std::memcpy(data, out.data(), size);
}

bool same(const std::array<uint8_t, 32>& a, const uint8_t* b) { return std::memcmp(a.data(), b, 32) == 0; }

}  // namespace

std::string open_pack(const std::string& path, const std::string& cache_dir, const nba::Progress& progress) {
    auto in = open_file(path, "rb");
    uint8_t head[BOSS_HEADER];
    size_t got = fread(head, 1, sizeof(head), in.get());
    if (got >= 4 && std::memcmp(head, "SARC", 4) == 0) return path;  // already plain
    if (got != sizeof(head) || std::memcmp(head, "boss", 4) != 0)
        throw std::runtime_error(base_name(path) + ": not a SpotPass pack or SARC");

    struct stat st{};
    if (stat(path.c_str(), &st) || st.st_size < 0) throw std::runtime_error("Cannot read " + path);
    const uint64_t total = static_cast<uint64_t>(st.st_size);
    if (be32(head + 4) != 0x10001 || be32(head + 8) != total || total < BOSS_HEADER + BODY_HEADER)
        throw std::runtime_error(base_name(path) + ": invalid SpotPass container");

    const std::string out_path = cache_dir + "/" + base_name(path) + ".sarc";
    struct stat cached{};
    if (stat(out_path.c_str(), &cached) == 0 && cached.st_size > 0 &&
        static_cast<uint64_t>(cached.st_size) == total - BOSS_HEADER - BODY_HEADER)
        return out_path;  // decrypted earlier

    // Counter block: the 12-byte nonce at 0x1C, then a big-endian 1.
    std::array<uint8_t, 16> ctr{};
    std::memcpy(ctr.data(), head + 0x1C, 12);
    ctr[15] = 1;

    const uint64_t body_size = total - BOSS_HEADER;
    std::vector<uint8_t> chunk(CHUNK);
    uint8_t headers[BODY_HEADER];
    size_t header_have = 0;

    nba::mkdirs(cache_dir);
    const std::string tmp_path = out_path + ".tmp";
    remove(tmp_path.c_str());
    auto out = open_file(tmp_path, "wb");
    nba::Sha256 payload_hash;
    bool payload_started = false;
    uint64_t payload_written = 0;

    for (uint64_t done = 0; done < body_size;) {
        size_t count = static_cast<size_t>(std::min<uint64_t>(CHUNK, body_size - done));
        if (fread(chunk.data(), 1, count, in.get()) != count) throw std::runtime_error("Truncated SpotPass pack");
        decrypt(chunk.data(), count, ctr);

        size_t offset = 0;
        if (header_have < BODY_HEADER) {
            size_t take = std::min(count, BODY_HEADER - header_have);
            std::memcpy(headers + header_have, chunk.data(), take);
            header_have += take;
            offset = take;
            if (header_have == BODY_HEADER) {
                // Content header hash: sha256(header[0:0x12] + 2 zero bytes).
                uint8_t first[0x14] = {};
                std::memcpy(first, headers, 0x12);
                nba::Sha256 h;
                h.update(first, sizeof(first));
                if (!same(h.finish(), headers + 0x12))
                    throw std::runtime_error("SpotPass key check failed (damaged pack?)");
                if (be16(headers + 0x10) != 1) throw std::runtime_error("Expected one SpotPass payload");
                if (be64(headers + 0x132) != EUR_TITLE_ID)
                    throw std::runtime_error(base_name(path) + " is not for EUR Badge Arcade");
                if (be32(headers + 0x132 + 16) != body_size - BODY_HEADER)
                    throw std::runtime_error("SpotPass payload length mismatch");
                uint8_t seed[0x1E] = {};
                std::memcpy(seed, headers + 0x132, 0x1C);
                payload_hash.update(seed, sizeof(seed));
                payload_started = true;
            }
        }
        if (payload_started && offset < count) {
            if (payload_written == 0 && count - offset >= 4 && std::memcmp(chunk.data() + offset, "SARC", 4) != 0)
                throw std::runtime_error("SpotPass payload is not a SARC");
            payload_hash.update(chunk.data() + offset, count - offset);
            if (fwrite(chunk.data() + offset, 1, count - offset, out.get()) != count - offset)
                throw std::runtime_error("Write failed (SD card full?)");
            payload_written += count - offset;
        }
        done += count;
        if (progress) progress("Decrypting " + base_name(path), done, body_size);
    }
    if (!same(payload_hash.finish(), headers + 0x14E)) {
        out.reset();
        remove(tmp_path.c_str());
        throw std::runtime_error("SpotPass payload hash mismatch");
    }
    if (fflush(out.get()) != 0) throw std::runtime_error("Write failed (SD card full?)");
    out.reset();
    remove(out_path.c_str());
    if (rename(tmp_path.c_str(), out_path.c_str()) != 0) throw std::runtime_error("Cannot store decrypted pack");
    return out_path;
}

}  // namespace spotpass
