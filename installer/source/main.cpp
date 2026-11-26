/*
 * Pretendo Bridge Installer
 *
 * Installs Nintendo Badge Arcade's offline patch with the Pretendo bridge and
 * switches between its catalog modes on the console:
 *   Vanilla   - 384 machines from the installed update (patch only)
 *   Expanded  - 406 machines, built here from the installed base game and update
 *   Week      - all machines of one archived SpotPass week, built here
 *
 * Expanded and weeks are generated on the console from the game's own RomFS
 * (read with romfsMountFromTitle), its SpotPass data in extdata, and packs the
 * user puts in sdmc:/3ds/pretendo-bridge/spotpass/. Nothing of the game is
 * bundled with the app; the patches (IPS) contain only the patch code.
 *
 * builder/ is badge-arcade-offline's native catalog builder (MIT) with an
 * added Expanded mode; luma_config, luma_system and fileops come from
 * Badge Arcade Reclawed (MIT).
 */
#include <3ds.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "builder/nba.hpp"
#include "builder/vendor/picojson.h"
#include "spotpass.hpp"

extern "C" {
#include "fileops.h"
#include "luma_config.h"
#include "luma_system.h"

#include "vanilla_home_ips.h"
#include "vanilla_game_ips.h"
#include "expanded_home_ips.h"
#include "expanded_game_ips.h"
#include "weekly_home_ips.h"
#include "weekly_game_ips.h"

// Keep the linear heap small (only the console framebuffers live there) so
// the builder gets most of the application memory.
u32 __ctru_linear_heap_size = 4 * 1024 * 1024;
}

namespace {

constexpr u64 BASE_TID = 0x0004000000153600ULL;
constexpr u64 UPDATE_TID = 0x0004000E00153600ULL;
constexpr unsigned UPDATE_V131 = 5136;  // title version of EUR update 1.3.1
constexpr u32 EXTDATA_ID = 0x1536;      // the game's SD extdata

#define TITLE_DIR "sdmc:/luma/titles/0004000000153600"
#define APP_DIR "sdmc:/3ds/pretendo-bridge"
const char* const PATCH_PATH = TITLE_DIR "/code.ips";
const char* const CATALOG_PATH = TITLE_DIR "/catalog.json";
const char* const PACK_DIR = APP_DIR "/spotpass";
const char* const CACHE_DIR = APP_DIR "/cache";
const char* const STAGE_DIR = APP_DIR "/build";
const char* const INPUT_DIR = APP_DIR "/input";
const char* const LUMA_CONFIG_PATH = "sdmc:/luma/config.ini";
const char* const LUMA_CONFIG_BACKUP = "sdmc:/luma/config.ini.pretendo-bridge.bak";
constexpr size_t LUMA_CONFIG_MAX_SIZE = 0x2400;

// Files a mode adds next to code.ips, relative to TITLE_DIR or STAGE_DIR.
const char* const MODE_FILES[] = {
    "/catalog.json",
    "/romfs/region_common/Schedule.xml",
    "/romfs/region_EUR/common/Schedule.xml",
    "/romfs/region_EUR/common/sharc/pc.sarc",
};
const char* const MODE_DIRS[] = {
    "/romfs/region_EUR/common/sharc", "/romfs/region_EUR/common", "/romfs/region_EUR",
    "/romfs/region_common", "/romfs",
};

#define C_RESET "\x1b[0m"
#define C_RED "\x1b[31m"
#define C_GREEN "\x1b[32m"
#define C_YELLOW "\x1b[33m"
#define C_CYAN "\x1b[36m"
#define C_INV "\x1b[7m"

enum class Kind { Vanilla, Expanded, Week };

struct Ips { const u8* data; u32 size; };

Ips ips_for(Kind kind, bool home) {
    switch (kind) {
    case Kind::Vanilla: return home ? Ips{vanilla_home_ips, vanilla_home_ips_size} : Ips{vanilla_game_ips, vanilla_game_ips_size};
    case Kind::Expanded: return home ? Ips{expanded_home_ips, expanded_home_ips_size} : Ips{expanded_game_ips, expanded_game_ips_size};
    case Kind::Week: return home ? Ips{weekly_home_ips, weekly_home_ips_size} : Ips{weekly_game_ips, weekly_game_ips_size};
    }
    return {nullptr, 0};
}

const char* kind_name(Kind kind) {
    switch (kind) {
    case Kind::Vanilla: return "Vanilla";
    case Kind::Expanded: return "Expanded";
    case Kind::Week: return "Week";
    }
    return "?";
}

struct ModeEntry {
    Kind kind;
    std::string label;   // shown on screen
    std::string pack;    // week pack path; empty for Vanilla/Expanded
    std::string date;    // week start, when known
    bool console = false;  // the console's own SpotPass week (extdata)
};

struct State {
    LumaSystem luma{};
    int config_patching = -1;
    bool base_installed = false, update_installed = false;
    unsigned update_version = 0;

    enum class Installed { None, Ours, Other } installed = Installed::None;
    Kind inst_kind = Kind::Vanilla;
    bool inst_home = true;
    std::string inst_detail;  // week date or "incomplete"
    bool inst_incomplete = false;

    bool extdata = false;
    std::string console_week;   // start date of extdata boss.sarc, empty if none
    bool console_allbadge = false;
    std::string sd_allbadge;    // catalog pack on the SD card, empty if none
    std::vector<ModeEntry> modes;
};

PrintConsole top, bottom;
State st;
size_t sel_mode = 0;
bool sel_home = true;
int sel_row = 0;  // 0 = mode, 1 = badges
bool restart_pending = false;
bool extdata_mounted = false;

// ------------------------------------------------------------------ output

void logv(const char* color, const char* fmt, va_list ap) {
    consoleSelect(&bottom);
    printf("%s", color);
    vprintf(fmt, ap);
    printf(C_RESET "\n");
}
void log_info(const char* fmt, ...) { va_list ap; va_start(ap, fmt); logv(C_RESET, fmt, ap); va_end(ap); }
void log_ok(const char* fmt, ...) { va_list ap; va_start(ap, fmt); logv(C_GREEN, fmt, ap); va_end(ap); }
void log_warn(const char* fmt, ...) { va_list ap; va_start(ap, fmt); logv(C_YELLOW, fmt, ap); va_end(ap); }
void log_err(const char* fmt, ...) { va_list ap; va_start(ap, fmt); logv(C_RED, fmt, ap); va_end(ap); }

void present() {
    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
}

// Progress line on the bottom screen, redrawn at most every 200 ms.
nba::Progress progress_printer() {
    return [last = u64(0), label_shown = std::string()](const std::string& label, uint64_t done, uint64_t total) mutable {
        u64 now = osGetTime();
        if (label != label_shown) {
            consoleSelect(&bottom);
            printf("\n");
            label_shown = label;
            last = 0;
        }
        if (now - last < 200 && done < total) return;
        last = now;
        consoleSelect(&bottom);
        unsigned pct = total ? unsigned(done * 100 / total) : 0;
        printf("\r%.30s %3u%%", label.c_str(), pct);
        present();
    };
}

// ------------------------------------------------------------------- files

bool file_exists(const std::string& path) {
    struct stat s{};
    return stat(path.c_str(), &s) == 0 && S_ISREG(s.st_mode);
}

bool title_version(u64 tid, unsigned* version) {
    AM_TitleInfo entry;
    if (R_FAILED(AM_GetTitleInfo(MEDIATYPE_SD, 1, &tid, &entry))) return false;
    *version = entry.version;
    return true;
}

bool game_ok() { return st.base_installed && st.update_installed && st.update_version == UPDATE_V131; }

bool lower_ends(const std::string& s, const char* suffix) {
    size_t n = strlen(suffix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i)
        if (tolower((unsigned char)s[s.size() - n + i]) != suffix[i]) return false;
    return true;
}

std::string lower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

// "data_v131-2022-05-27-01-40-EU.enc" -> "2022-05-27"
std::string date_in_name(const std::string& name) {
    for (size_t i = 0; i + 10 <= name.size(); ++i) {
        const char* p = name.c_str() + i;
        bool ok = (p[0] == '2' && p[1] == '0');
        for (int k : {2, 3, 5, 6, 8, 9}) ok = ok && isdigit((unsigned char)p[k]);
        ok = ok && p[4] == '-' && p[7] == '-';
        if (ok) return name.substr(i, 10);
    }
    return "";
}

// "20220527" -> "2022-05-27"
std::string pretty_date(const std::string& d) {
    if (d.size() == 8) return d.substr(0, 4) + "-" + d.substr(4, 2) + "-" + d.substr(6, 2);
    return d;
}

// ------------------------------------------------------------------- probe

void probe_installed() {
    st.installed = State::Installed::None;
    st.inst_detail.clear();
    st.inst_incomplete = false;
    if (!fops_exists(PATCH_PATH)) return;

    unsigned char* data;
    size_t size;
    if (fops_read(PATCH_PATH, &data, &size, FOPS_MAX_READ) != 0) {
        st.installed = State::Installed::Other;
        return;
    }
    bool found = false;
    for (Kind k : {Kind::Vanilla, Kind::Expanded, Kind::Week})
        for (bool home : {true, false}) {
            Ips ips = ips_for(k, home);
            if (!found && ips.size == size && memcmp(ips.data, data, size) == 0) {
                found = true;
                st.inst_kind = k;
                st.inst_home = home;
            }
        }
    free(data);
    if (!found) {
        st.installed = State::Installed::Other;
        return;
    }
    st.installed = State::Installed::Ours;
    if (st.inst_kind == Kind::Vanilla) return;

    std::string pc = std::string(TITLE_DIR) + "/romfs/region_EUR/common/sharc/pc.sarc";
    if (!file_exists(pc)) {
        st.inst_incomplete = true;
        st.inst_detail = "incomplete";
        return;
    }
    unsigned char* json;
    size_t json_size;
    if (st.inst_kind == Kind::Week && fops_read(CATALOG_PATH, &json, &json_size, FOPS_MAX_READ) == 0) {
        picojson::value v;
        std::string err = picojson::parse(v, std::string(reinterpret_cast<char*>(json), json_size));
        free(json);
        if (err.empty() && v.is<picojson::object>() && v.get("week").is<std::string>())
            st.inst_detail = pretty_date(v.get("week").get<std::string>());
    }
}

void probe_inputs() {
    st.console_week.clear();
    st.console_allbadge = false;
    st.sd_allbadge.clear();
    st.modes.clear();
    st.modes.push_back({Kind::Vanilla, "Vanilla (384 machines)", "", "", false});
    st.modes.push_back({Kind::Expanded, "Expanded (406 machines)", "", "", false});

    if (extdata_mounted) {
        if (file_exists("nbaext:/boss.sarc")) {
            try {
                st.console_week = pretty_date(nba::read_week(nba::Asset::file("nbaext:/boss.sarc")).start);
            } catch (const std::exception&) {
                st.console_week.clear();
            }
        }
        st.console_allbadge = file_exists("nbaext:/allbadge.sarc");
    }
    if (!st.console_week.empty())
        st.modes.push_back({Kind::Week, "Week " + st.console_week + " (console)", "nbaext:/boss.sarc", st.console_week, true});

    std::vector<ModeEntry> weeks;
    if (DIR* dir = opendir(PACK_DIR)) {
        while (dirent* e = readdir(dir)) {
            std::string name = e->d_name;
            if (name == "." || name == "..") continue;
            if (!(lower_ends(name, ".boss") || lower_ends(name, ".enc") || lower_ends(name, ".sarc") || lower_ends(name, ".dat")))
                continue;
            std::string path = std::string(PACK_DIR) + "/" + name;
            if (lower(name).find("allbadge") != std::string::npos) {
                // Prefer a decrypted catalog if several are present.
                if (st.sd_allbadge.empty() || lower_ends(name, ".sarc")) st.sd_allbadge = path;
                continue;
            }
            std::string date = date_in_name(name);
            weeks.push_back({Kind::Week, "Week " + (date.empty() ? name.substr(0, 28) : date), path, date, false});
        }
        closedir(dir);
    }
    std::sort(weeks.begin(), weeks.end(), [](const ModeEntry& a, const ModeEntry& b) {
        return a.date != b.date ? a.date < b.date : a.pack < b.pack;
    });
    st.modes.insert(st.modes.end(), weeks.begin(), weeks.end());
    if (sel_mode >= st.modes.size()) sel_mode = 0;
}

void probe() {
    luma_system_query(&st.luma);
    st.config_patching = -1;
    unsigned char* text;
    size_t size;
    if (fops_read(LUMA_CONFIG_PATH, &text, &size, LUMA_CONFIG_MAX_SIZE) == 0) {
        LumaCfgInfo info;
        if (lumacfg_parse(reinterpret_cast<const char*>(text), size, &info) == LUMACFG_OK)
            st.config_patching = info.game_patching;
        free(text);
    }
    unsigned base_version = 0;
    st.base_installed = title_version(BASE_TID, &base_version);
    st.update_version = 0;
    st.update_installed = title_version(UPDATE_TID, &st.update_version);
    probe_installed();
    probe_inputs();
    nba::close_read_cache();
}

// -------------------------------------------------------------------- draw

std::string installed_text() {
    switch (st.installed) {
    case State::Installed::None: return "nothing";
    case State::Installed::Other: return C_YELLOW "other patch" C_RESET;
    case State::Installed::Ours: break;
    }
    std::string s = kind_name(st.inst_kind);
    if (!st.inst_detail.empty()) s += " " + st.inst_detail;
    s += st.inst_home ? ", HOME Menu" : ", game only";
    return (st.inst_incomplete ? C_YELLOW : C_GREEN) + s + C_RESET;
}

void draw_top() {
    consoleSelect(&top);
    consoleClear();
    printf(C_CYAN "Pretendo Bridge Installer v%s" C_RESET "\n", APP_VERSION);
    printf("Badge Arcade offline - Nintendo + Pretendo\n\n");

    if (!st.luma.detected) printf(" Luma3DS       " C_RED "not detected" C_RESET "\n");
    else printf(" Luma3DS       %sv%u.%u.%u" C_RESET "\n", st.luma.config_on_sd ? C_GREEN : C_RED,
                st.luma.major, st.luma.minor, st.luma.build);

    if (st.luma.game_patching_active) printf(" Game patching " C_GREEN "on" C_RESET "\n");
    else if (st.config_patching == 1) printf(" Game patching " C_YELLOW "on after restart" C_RESET "\n");
    else printf(" Game patching " C_YELLOW "off (install turns it on)" C_RESET "\n");

    if (!st.base_installed) printf(" Badge Arcade  " C_RED "EUR game not installed" C_RESET "\n");
    else if (!st.update_installed) printf(" Badge Arcade  " C_RED "update 1.3.1 missing" C_RESET "\n");
    else if (st.update_version != UPDATE_V131) printf(" Badge Arcade  " C_RED "update v%u, needs v%u" C_RESET "\n", st.update_version, UPDATE_V131);
    else printf(" Badge Arcade  " C_GREEN "EUR, update 1.3.1" C_RESET "\n");

    printf(" Installed     %s\n", installed_text().c_str());
    printf(" Catalog       %s\n", !st.sd_allbadge.empty() ? C_GREEN "SD card" C_RESET
                                  : st.console_allbadge ? C_GREEN "console" C_RESET
                                  : C_YELLOW "missing (needed for weeks)" C_RESET);
    printf("\n");

    const ModeEntry& m = st.modes[sel_mode];
    printf(" %sMode   < %-30.30s >" C_RESET "\n", sel_row == 0 ? C_INV : "", m.label.c_str());
    printf("          %u of %u\n", unsigned(sel_mode + 1), unsigned(st.modes.size()));
    printf(" %sBadges < %-30.30s >" C_RESET "\n\n", sel_row == 1 ? C_INV : "",
           sel_home ? "go to the HOME Menu" : "stay in the game");

    printf(" UP/DOWN choose row   LEFT/RIGHT change\n");
    printf(" L/R     jump 10 weeks\n");
    printf(" A       install selected mode\n");
    printf(" X       uninstall      Y  rescan SD card\n");
    if (restart_pending) printf(C_YELLOW " SELECT  restart now" C_RESET "\n");
    printf(" START   exit\n\n");
    printf(" Weeks: put SpotPass packs (.boss/.enc/.sarc)\n");
    printf(" and allbadge_v131 in /3ds/pretendo-bridge/spotpass\n");
}

void describe_selection() {
    consoleSelect(&bottom);
    consoleClear();
    const ModeEntry& m = st.modes[sel_mode];
    switch (m.kind) {
    case Kind::Vanilla:
        log_info("Vanilla: 384 machines from the installed");
        log_info("update. Patch only, installs instantly.");
        break;
    case Kind::Expanded:
        log_info("Expanded: 398 machines plus 8 bonus layouts,");
        log_info("built here from your installed game.");
        if (st.sd_allbadge.empty() && !st.console_allbadge)
            log_warn("Without the badge catalog, older badges are hidden in the collection (kept in your save).");
        break;
    case Kind::Week:
        log_info("Week: every machine of the SpotPass week%s%s,", m.date.empty() ? "" : " from ", m.date.c_str());
        log_info("built here. Stays until you switch.");
        log_info("Source: %s", m.console ? "your console's SpotPass data" : m.pack.c_str() + strlen(PACK_DIR) + 1);
        if (st.sd_allbadge.empty() && !st.console_allbadge)
            log_warn("Needs the badge catalog (allbadge_v131) in /3ds/pretendo-bridge/spotpass.");
        break;
    }
    log_info("Building takes a few minutes. Save data and HOME Menu badges stay as they are.");
}

void redraw() {
    draw_top();
    describe_selection();
}

// ----------------------------------------------------------------- actions

bool restart_console() {
    if (R_SUCCEEDED(APT_HardwareResetAsync())) return true;
    if (R_SUCCEEDED(ptmSysmInit())) {
        Result r = PTMSYSM_RebootAsync(0);
        ptmSysmExit();
        return R_SUCCEEDED(r);
    }
    return false;
}

// Turns on [boot] enable_game_patching by flipping its value byte after a
// backup, then checks the file (same approach as Reclawed).
void enable_game_patching() {
    unsigned char* text;
    size_t size;
    LumaCfgInfo info;
    if (fops_read(LUMA_CONFIG_PATH, &text, &size, LUMA_CONFIG_MAX_SIZE) != 0) {
        if (!st.luma.game_patching_active) log_warn("Can't read luma/config.ini. Turn on game patching by hand (hold SELECT while powering on).");
        return;
    }
    LumaCfgStatus status = lumacfg_parse(reinterpret_cast<const char*>(text), size, &info);
    if (status != LUMACFG_OK || info.game_patching == 1) {
        free(text);
        if (st.luma.game_patching_active) return;
        if (status == LUMACFG_OK) {
            restart_pending = true;
            log_warn("Game patching turns on after a restart. Press SELECT.");
        } else {
            log_warn("config.ini: %s. Turn on game patching by hand (hold SELECT while powering on).", lumacfg_status_str(status));
        }
        return;
    }
    if (fops_write_replace(LUMA_CONFIG_BACKUP, text, size) != 0 || fops_patch_byte(LUMA_CONFIG_PATH, info.value_offset, '1') != 0) {
        free(text);
        log_err("Can't change config.ini, game patching left off.");
        return;
    }
    unsigned char* after = nullptr;
    size_t after_size = 0;
    LumaCfgInfo check;
    bool ok = fops_read(LUMA_CONFIG_PATH, &after, &after_size, LUMA_CONFIG_MAX_SIZE) == 0 && after_size == size &&
              lumacfg_parse(reinterpret_cast<const char*>(after), after_size, &check) == LUMACFG_OK && check.game_patching == 1;
    free(after);
    if (ok) {
        restart_pending = true;
        log_ok("Game patching enabled (backup: luma/config.ini.pretendo-bridge.bak).");
        log_warn("Press SELECT to restart.");
    } else if (fops_patch_byte(LUMA_CONFIG_PATH, info.value_offset, '0') == 0) {
        log_err("config.ini check failed, change undone.");
    } else {
        log_err("config.ini check failed. Restore luma/config.ini.pretendo-bridge.bak.");
    }
    free(text);
}

// Removes a mode's files below `root` (TITLE_DIR or STAGE_DIR); keeps code.ips.
bool remove_mode_files(const std::string& root) {
    bool ok = true;
    for (const char* rel : MODE_FILES) {
        std::string path = root + rel;
        if (file_exists(path) && remove(path.c_str()) != 0) ok = false;
    }
    for (const char* rel : MODE_DIRS) rmdir((root + rel).c_str());  // only if empty
    return ok;
}

// Inputs from the installed titles' RomFS, or exported files on the SD card.
struct Inputs {
    std::string base, update, schedule;
    bool mounted = false;
    ~Inputs() {
        if (mounted) {
            romfsUnmount("nbabase");
            romfsUnmount("nbaupd");
        }
    }
};

bool open_inputs(Inputs& in) {
    if (R_SUCCEEDED(romfsMountFromTitle(BASE_TID, MEDIATYPE_SD, "nbabase"))) {
        if (R_SUCCEEDED(romfsMountFromTitle(UPDATE_TID, MEDIATYPE_SD, "nbaupd"))) {
            in.mounted = true;
            in.base = "nbabase:/region_EUR/common/sharc/pc.sarc";
            in.update = "nbaupd:/region_EUR/common/sharc/pc.sarc";
            in.schedule = "nbaupd:/region_common/Schedule.xml";
            if (file_exists(in.base) && file_exists(in.update) && file_exists(in.schedule)) return true;
        } else {
            romfsUnmount("nbabase");
        }
    }
    // Fallback: files exported with GodMode9 (badge-arcade-offline README, step 3).
    in.base = std::string(INPUT_DIR) + "/base-pc.sarc";
    in.update = std::string(INPUT_DIR) + "/pc.sarc";
    in.schedule = std::string(INPUT_DIR) + "/Schedule.xml";
    if (file_exists(in.base) && file_exists(in.update) && file_exists(in.schedule)) return true;
    log_err("Can't read the game's files from the console.");
    log_info("Export base-pc.sarc, pc.sarc and Schedule.xml with GodMode9 to /3ds/pretendo-bridge/input.");
    return false;
}

std::string catalog_source(const nba::Progress& progress) {
    if (!st.sd_allbadge.empty()) return spotpass::open_pack(st.sd_allbadge, CACHE_DIR, progress);
    if (st.console_allbadge) return "nbaext:/allbadge.sarc";
    return "";
}

bool write_patch(Kind kind, bool home) {
    Ips ips = ips_for(kind, home);
    if (fops_mkdirs_for(PATCH_PATH) != 0 || fops_write_replace(PATCH_PATH, ips.data, ips.size) != 0) {
        log_err("Can't write the patch to the SD card.");
        return false;
    }
    return true;
}

bool build_and_install(const ModeEntry& m, bool home) {
    auto progress = progress_printer();
    Inputs in;
    log_info("Reading the installed game...");
    present();
    if (!open_inputs(in)) return false;

    nba::Build request;
    request.base_archive = in.base;
    request.update_archive = in.update;
    request.schedule = in.schedule;
    request.home = home;
    request.expanded = m.kind == Kind::Expanded;

    std::string catalog = catalog_source(progress);
    if (!catalog.empty()) request.allbadge = std::make_shared<nba::Asset>(nba::Asset::file(catalog));
    if (m.kind == Kind::Week) {
        if (catalog.empty()) {
            log_err("Weeks need the badge catalog (allbadge_v131) in /3ds/pretendo-bridge/spotpass.");
            return false;
        }
        std::string week = spotpass::open_pack(m.pack, CACHE_DIR, progress);
        request.week = std::make_shared<nba::Week>(nba::read_week(nba::Asset::file(week)));
    }

    remove_mode_files(STAGE_DIR);
    log_info("\nBuilding %s...", m.label.c_str());
    present();
    nba::Catalog result = nba::build_content(request, STAGE_DIR, progress);
    printf("\n");

    // Swap the files in: first remove the old mode, then move the new one.
    if (!remove_mode_files(TITLE_DIR)) {
        log_err("Can't remove the previous mode's files.");
        return false;
    }
    for (const char* rel : MODE_FILES) {
        std::string from = std::string(STAGE_DIR) + rel, to = std::string(TITLE_DIR) + rel;
        nba::mkdirs(to.substr(0, to.find_last_of('/')));
        if (rename(from.c_str(), to.c_str()) != 0) {
            log_err("Can't move %s into place.", rel);
            return false;
        }
    }
    remove_mode_files(STAGE_DIR);
    if (!write_patch(m.kind, home)) return false;
    log_ok("Installed %s: %u machines, %u badges.", m.label.c_str(), result.machines, result.prizes);
    return true;
}

void install() {
    consoleSelect(&bottom);
    consoleClear();
    if (!st.luma.detected) { log_err("Luma3DS not detected."); return; }
    if (!st.luma.config_on_sd) { log_err("Luma3DS runs from CTRNAND; install by hand there."); return; }
    if (!game_ok()) { log_err("Needs EUR Badge Arcade with update 1.3.1 (v%u).", UPDATE_V131); return; }

    const ModeEntry m = st.modes[sel_mode];
    bool other = st.installed == State::Installed::Other;
    bool ok = false;
    aptSetHomeAllowed(false);
    try {
        if (m.kind == Kind::Vanilla) {
            ok = remove_mode_files(TITLE_DIR) && write_patch(Kind::Vanilla, sel_home);
            if (ok) log_ok("Installed Vanilla: 384 machines.");
        } else {
            ok = build_and_install(m, sel_home);
        }
    } catch (const std::exception& e) {
        printf("\n");
        log_err("%s", e.what());
        ok = false;
    }
    aptSetHomeAllowed(true);
    if (ok) {
        if (other) log_warn("Replaced the previous Badge Arcade patch.");
        enable_game_patching();
        if (!restart_pending) log_ok("Start Badge Arcade!");
    } else {
        log_warn("Nothing was changed if the error came before the swap.");
    }
    probe_installed();
    draw_top();
}

void uninstall() {
    consoleSelect(&bottom);
    consoleClear();
    if (st.installed == State::Installed::None) { log_info("No patch installed."); return; }
    if (st.installed == State::Installed::Other) { log_warn("The patch on the SD card is not the Pretendo Bridge. Left unchanged."); return; }
    bool ok = remove_mode_files(TITLE_DIR) && remove(PATCH_PATH) == 0 && !fops_exists(PATCH_PATH);
    if (ok) log_ok("Bridge removed. Save data and badges are kept.");
    else log_err("Can't delete all patch files.");
    probe_installed();
    draw_top();
}

void mount_extdata() {
    u32 path[3] = {MEDIATYPE_SD, EXTDATA_ID, 0};
    FS_Path where = {PATH_BINARY, sizeof(path), path};
    extdata_mounted = R_SUCCEEDED(archiveMount(ARCHIVE_EXTDATA, where, "nbaext"));
}

}  // namespace

int main() {
    gfxInitDefault();
    consoleInit(GFX_TOP, &top);
    consoleInit(GFX_BOTTOM, &bottom);
    amInit();
    psInit();
    osSetSpeedupEnable(true);
    mount_extdata();

    probe();
    redraw();

    bool restarting = false;
    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown();
        if (restarting) {
            // The system closes the app; aptMainLoop() returns false.
        } else if (down & KEY_START) {
            break;
        } else if (down & (KEY_UP | KEY_DOWN)) {
            sel_row ^= 1;
            draw_top();
        } else if (down & (KEY_LEFT | KEY_RIGHT | KEY_L | KEY_R)) {
            int step = (down & (KEY_L | KEY_R)) ? 10 : 1;
            int dir = (down & (KEY_LEFT | KEY_L)) ? -1 : 1;
            if (sel_row == 0) {
                int n = int(st.modes.size());
                sel_mode = size_t(((int(sel_mode) + dir * step) % n + n) % n);
                redraw();
            } else {
                sel_home = !sel_home;
                draw_top();
            }
        } else if (down & KEY_A) {
            install();
        } else if (down & KEY_X) {
            uninstall();
        } else if (down & KEY_Y) {
            probe();
            redraw();
            log_ok("Rescanned: %u modes available.", unsigned(st.modes.size()));
        } else if ((down & KEY_SELECT) && restart_pending) {
            consoleSelect(&bottom);
            if (restart_console()) {
                restarting = true;
                log_info("Restarting...");
            } else {
                log_err("Restart failed. Please restart the console yourself.");
            }
        }
        present();
    }

    if (extdata_mounted) archiveUnmount("nbaext");
    psExit();
    amExit();
    gfxExit();
    return 0;
}
