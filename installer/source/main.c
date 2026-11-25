/*
 * Pretendo Bridge Installer
 *
 * Writes the Badge Arcade Pretendo Bridge patch (badge-arcade-offline v0.5.4
 * plus one hook) to sdmc:/luma/titles/0004000000153600/code.ips and turns on
 * Luma3DS "Enable game patching" if it is off. No network access: both patch
 * variants are embedded in the app.
 *
 * luma_config.*, luma_system.* and fileops.* are taken unchanged from
 * Badge Arcade Reclawed (MIT, see ../LICENSE-reclawed).
 */
#include <3ds.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fileops.h"
#include "luma_config.h"
#include "luma_system.h"

#include "home_menu_ips.h"
#include "game_only_ips.h"

#define BASE_TID             0x0004000000153600ULL
#define UPDATE_TID           0x0004000E00153600ULL
#define UPDATE_V131          5136u /* title version of EUR update 1.3.1 */
#define PATCH_PATH           "sdmc:/luma/titles/0004000000153600/code.ips"
#define LUMA_CONFIG_PATH     "sdmc:/luma/config.ini"
#define LUMA_CONFIG_BACKUP   "sdmc:/luma/config.ini.pretendo-bridge.bak"
#define LUMA_CONFIG_MAX_SIZE 0x2400

#define C_RESET  "\x1b[0m"
#define C_RED    "\x1b[31m"
#define C_GREEN  "\x1b[32m"
#define C_YELLOW "\x1b[33m"
#define C_CYAN   "\x1b[36m"

typedef enum
{
	PATCH_NONE,
	PATCH_HOME,
	PATCH_GAME,
	PATCH_OTHER,
} PatchState;

typedef struct
{
	LumaSystem luma;
	bool config_found;
	LumaCfgStatus config_status;
	int config_patching; /* 0 or 1 in config.ini; -1 when unknown */
	bool base_installed;
	unsigned base_version;
	bool update_installed;
	unsigned update_version;
	PatchState patch;
} State;

static PrintConsole top, bottom;
static State st;
static bool restart_pending; /* config.ini changed: Luma reads it at boot */

/* ---------------------------------------------------------------- logging */

static void log_line(const char *color, const char *fmt, ...)
{
	consoleSelect(&bottom);
	printf("%s", color);
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf(C_RESET "\n");
}

#define LOG_INFO(...) log_line(C_RESET, __VA_ARGS__)
#define LOG_OK(...)   log_line(C_GREEN, __VA_ARGS__)
#define LOG_WARN(...) log_line(C_YELLOW, __VA_ARGS__)
#define LOG_ERR(...)  log_line(C_RED, __VA_ARGS__)

/* ------------------------------------------------------------------ probe */

static bool title_version(u64 tid, unsigned *version)
{
	AM_TitleInfo entry;
	if (R_FAILED(AM_GetTitleInfo(MEDIATYPE_SD, 1, &tid, &entry)))
		return false;
	*version = entry.version;
	return true;
}

static void probe(void)
{
	memset(&st, 0, sizeof(st));
	luma_system_query(&st.luma);

	st.config_patching = -1;
	unsigned char *text;
	size_t size;
	st.config_found = fops_read(LUMA_CONFIG_PATH, &text, &size, LUMA_CONFIG_MAX_SIZE) == 0;
	if (st.config_found)
	{
		LumaCfgInfo info;
		st.config_status = lumacfg_parse((const char *)text, size, &info);
		if (st.config_status == LUMACFG_OK)
			st.config_patching = info.game_patching;
		free(text);
	}

	st.base_installed = title_version(BASE_TID, &st.base_version);
	st.update_installed = title_version(UPDATE_TID, &st.update_version);

	if (!fops_exists(PATCH_PATH))
		st.patch = PATCH_NONE;
	else if (fops_same_content(PATCH_PATH, home_menu_ips, home_menu_ips_size))
		st.patch = PATCH_HOME;
	else if (fops_same_content(PATCH_PATH, game_only_ips, game_only_ips_size))
		st.patch = PATCH_GAME;
	else
		st.patch = PATCH_OTHER;
}

static bool game_ok(void)
{
	return st.base_installed && st.update_installed && st.update_version == UPDATE_V131;
}

/* ------------------------------------------------------------------- draw */

static void draw(void)
{
	consoleSelect(&top);
	consoleClear();

	printf(C_CYAN "Pretendo Bridge Installer v%s" C_RESET "\n", APP_VERSION);
	printf("Nintendo Badge Arcade offline, EUR 1.3.1\n");
	printf("Works in Nintendo and Pretendo mode (Nimbus)\n\n");

	/* Luma3DS */
	if (!st.luma.detected)
		printf(" Luma3DS        " C_RED "not detected" C_RESET "\n");
	else if (!st.luma.config_on_sd)
		printf(" Luma3DS        " C_RED "v%u.%u.%u on CTRNAND" C_RESET "\n",
		       st.luma.major, st.luma.minor, st.luma.build);
	else
		printf(" Luma3DS        " C_GREEN "v%u.%u.%u" C_RESET "\n",
		       st.luma.major, st.luma.minor, st.luma.build);

	/* Game patching */
	if (st.luma.game_patching_active)
		printf(" Game patching  " C_GREEN "on" C_RESET "\n");
	else if (st.config_patching == 1)
		printf(" Game patching  " C_YELLOW "on after a restart" C_RESET "\n");
	else if (st.config_patching == 0)
		printf(" Game patching  " C_YELLOW "off (install turns it on)" C_RESET "\n");
	else
		printf(" Game patching  " C_YELLOW "off or unknown" C_RESET "\n");

	/* Badge Arcade */
	if (!st.base_installed)
		printf(" Badge Arcade   " C_RED "EUR version not installed" C_RESET "\n");
	else if (!st.update_installed)
		printf(" Badge Arcade   " C_RED "update 1.3.1 missing" C_RESET "\n");
	else if (st.update_version != UPDATE_V131)
		printf(" Badge Arcade   " C_RED "update v%u (needs v%u)" C_RESET "\n",
		       st.update_version, UPDATE_V131);
	else
		printf(" Badge Arcade   " C_GREEN "EUR, update 1.3.1" C_RESET "\n");

	/* Patch on the SD card */
	switch (st.patch)
	{
	case PATCH_NONE:
		printf(" Patch on SD    none\n");
		break;
	case PATCH_HOME:
		printf(" Patch on SD    " C_GREEN "Bridge (HOME Menu)" C_RESET "\n");
		break;
	case PATCH_GAME:
		printf(" Patch on SD    " C_GREEN "Bridge (game only)" C_RESET "\n");
		break;
	case PATCH_OTHER:
		printf(" Patch on SD    " C_YELLOW "other patch (gets replaced)" C_RESET "\n");
		break;
	}

	printf("\n");
	printf(" A       Install: badges go to the HOME Menu\n");
	printf(" Y       Install: badges stay in the game\n");
	printf(" X       Uninstall the bridge\n");
	if (restart_pending)
		printf(C_YELLOW " SELECT  Restart now" C_RESET "\n");
	printf(" START   Exit\n\n");

	printf(" Back up first: Checkpoint (extdata) and\n");
	printf(" Simple Badge Injector (HOME Menu badges).\n");
	printf(" After switching accounts in Nimbus, run the\n");
	printf(" NNID/PNID fix in Simple Badge Injector\n");
	printf(" before you start Badge Arcade.\n");
}

/* ---------------------------------------------------------------- actions */

static bool restart_console(void)
{
	if (R_SUCCEEDED(APT_HardwareResetAsync()))
		return true;
	if (R_SUCCEEDED(ptmSysmInit()))
	{
		Result r = PTMSYSM_RebootAsync(0);
		ptmSysmExit();
		return R_SUCCEEDED(r);
	}
	return false;
}

/* Turns on [boot] enable_game_patching by flipping its single value byte,
 * after a backup, and checks the result (same approach as Reclawed). */
static void enable_game_patching(void)
{
	unsigned char *text;
	size_t size;
	LumaCfgInfo info;

	if (fops_read(LUMA_CONFIG_PATH, &text, &size, LUMA_CONFIG_MAX_SIZE) != 0)
	{
		if (st.luma.game_patching_active)
			LOG_OK("Game patching is on. Start Badge Arcade!");
		else
			LOG_WARN("Can't read luma/config.ini. Turn on 'Enable game patching' by hand: hold SELECT while powering on.");
		return;
	}

	LumaCfgStatus status = lumacfg_parse((const char *)text, size, &info);
	if (status != LUMACFG_OK)
	{
		free(text);
		if (st.luma.game_patching_active)
			LOG_OK("Game patching is on. Start Badge Arcade!");
		else
			LOG_WARN("config.ini: %s. Turn on 'Enable game patching' by hand: hold SELECT while powering on.",
			         lumacfg_status_str(status));
		return;
	}

	if (info.game_patching == 1)
	{
		free(text);
		if (st.luma.game_patching_active)
			LOG_OK("Game patching is on. Start Badge Arcade!");
		else
		{
			restart_pending = true;
			LOG_WARN("Game patching turns on after a restart. Press SELECT.");
		}
		return;
	}

	if (fops_write_replace(LUMA_CONFIG_BACKUP, text, size) != 0)
	{
		free(text);
		LOG_ERR("Can't back up config.ini, game patching left off.");
		return;
	}
	if (fops_patch_byte(LUMA_CONFIG_PATH, info.value_offset, '1') != 0)
	{
		free(text);
		LOG_ERR("Can't write config.ini, game patching left off.");
		return;
	}

	unsigned char *after = NULL;
	size_t after_size = 0;
	LumaCfgInfo check;
	bool ok = fops_read(LUMA_CONFIG_PATH, &after, &after_size, LUMA_CONFIG_MAX_SIZE) == 0 &&
	          after_size == size &&
	          lumacfg_parse((const char *)after, after_size, &check) == LUMACFG_OK &&
	          check.game_patching == 1;
	free(after);

	if (ok)
	{
		restart_pending = true;
		LOG_OK("Game patching enabled.");
		LOG_INFO("Backup: luma/config.ini.pretendo-bridge.bak");
		LOG_WARN("Press SELECT to restart, then start Badge Arcade.");
	}
	else if (fops_patch_byte(LUMA_CONFIG_PATH, info.value_offset, '0') == 0)
		LOG_ERR("config.ini check failed, change undone.");
	else
		LOG_ERR("config.ini check failed. Restore luma/config.ini.pretendo-bridge.bak.");
	free(text);
}

static void install(bool home_menu)
{
	consoleSelect(&bottom);
	consoleClear();

	if (!st.luma.detected)
	{
		LOG_ERR("Luma3DS not detected. The patch needs Luma3DS custom firmware.");
		return;
	}
	if (!st.luma.config_on_sd)
	{
		LOG_ERR("Luma3DS runs from CTRNAND. Copy code.ips by hand to CTRNAND:/luma/titles/0004000000153600/.");
		return;
	}
	if (!game_ok())
	{
		LOG_ERR("This patch only fits the EUR game with update 1.3.1 (title version %u).", UPDATE_V131);
		return;
	}

	const u8 *data = home_menu ? home_menu_ips : game_only_ips;
	u32 size = home_menu ? home_menu_ips_size : game_only_ips_size;
	bool replaced_other = st.patch == PATCH_OTHER;

	if (fops_mkdirs_for(PATCH_PATH) != 0 || fops_write_replace(PATCH_PATH, data, size) != 0)
	{
		LOG_ERR("Can't write the patch to the SD card.");
		return;
	}
	if (replaced_other)
		LOG_WARN("Replaced the previous patch for Badge Arcade.");
	LOG_OK("Installed: %s.", home_menu ? "badges go to the HOME Menu" : "badges stay in the game");

	enable_game_patching();
	probe();
}

static void uninstall(void)
{
	consoleSelect(&bottom);
	consoleClear();

	if (st.patch == PATCH_NONE)
	{
		LOG_INFO("No patch installed.");
		return;
	}
	if (st.patch == PATCH_OTHER)
	{
		LOG_WARN("The patch on the SD card is not the Pretendo Bridge. Left unchanged.");
		return;
	}
	if (remove(PATCH_PATH) != 0 || fops_exists(PATCH_PATH))
	{
		LOG_ERR("Can't delete the patch.");
		return;
	}
	LOG_OK("Bridge removed. Save data and badges are kept.");
	probe();
}

/* ------------------------------------------------------------------- main */

int main(void)
{
	gfxInitDefault();
	consoleInit(GFX_TOP, &top);
	consoleInit(GFX_BOTTOM, &bottom);
	amInit();

	probe();
	draw();
	consoleSelect(&bottom);
	if (game_ok() && st.luma.detected)
		LOG_OK("Ready. Press A to install.");
	else
		LOG_WARN("Check the status on the top screen.");

	bool restarting = false;
	while (aptMainLoop())
	{
		hidScanInput();
		u32 down = hidKeysDown();

		if (restarting)
		{
			/* The system closes the app; aptMainLoop() returns false. */
		}
		else if (down & KEY_START)
			break;
		else if (down & KEY_A)
		{
			install(true);
			draw();
		}
		else if (down & KEY_Y)
		{
			install(false);
			draw();
		}
		else if (down & KEY_X)
		{
			uninstall();
			draw();
		}
		else if ((down & KEY_SELECT) && restart_pending)
		{
			consoleSelect(&bottom);
			if (restart_console())
			{
				restarting = true;
				LOG_INFO("Restarting...");
			}
			else
				LOG_ERR("Restart failed. Please restart the console yourself.");
		}

		gfxFlushBuffers();
		gfxSwapBuffers();
		gspWaitForVBlank();
	}

	amExit();
	gfxExit();
	return 0;
}
