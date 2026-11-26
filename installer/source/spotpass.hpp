#pragma once
// SpotPass (BOSS) packs on the console: decrypts archived Badge Arcade packs
// with the console's own BOSS key (AES keyslot 0x38 through the ps:ps
// service), so no boot9 dump is needed. Mirrors badge-arcade-offline's
// src/badge_spotpass.py: same framing, both SHA-256 checks, same title check.

#include <string>
#include "builder/nba.hpp"

namespace spotpass {

// Returns the path of a plain SARC for `path`: `path` itself when it already
// is one, otherwise a decrypted copy in `cache_dir` (reused on later calls).
// Throws std::runtime_error with a short message on any problem.
std::string open_pack(const std::string& path, const std::string& cache_dir,
                      const nba::Progress& progress = {});

}
