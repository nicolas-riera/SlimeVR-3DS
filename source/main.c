#include <3ds.h>
#include <malloc.h>

#include "lang.h"
#include "tracker.h"
#include "ui.h"
#include "wifilock.h"

#define SOC_BUFFER_SIZE 0x100000

static bool s_backlightOn = true;

static void set_backlight(bool on) {
	if (on == s_backlightOn) return;
	s_backlightOn = on;
	if (on) GSPLCD_PowerOnBacklight(GSPLCD_SCREEN_BOTH);
	else GSPLCD_PowerOffBacklight(GSPLCD_SCREEN_BOTH);
}

int main(void) {
	osSetSpeedupEnable(true);
	/* Never go to sleep when the lid is closed: the tracker must keep running. */
	aptSetSleepAllowed(false);

	ui_init();
	acInit();
	cfguInit();
	lang_init();
	ptmuInit();
	gspLcdInit();
	bool haveMcuHwc = R_SUCCEEDED(mcuHwcInit());

	u32* socBuffer = (u32*)memalign(0x1000, SOC_BUFFER_SIZE);
	bool socOk = socBuffer && R_SUCCEEDED(socInit(socBuffer, SOC_BUFFER_SIZE));
	(void)socOk;

	/* keep the Wi-Fi up with the lid closed (no StreetPass takeover) */
	wifilock_begin();
	tracker_start(haveMcuHwc);

	bool userScreensOff = false;
	u8 shellOpen = 1;
	u64 lastShellCheck = 0;
	TrackerState st;

	while (aptMainLoop()) {
		hidScanInput();
		u32 down = hidKeysDown();
		if (down & KEY_START) break;

		if (userScreensOff && down) userScreensOff = false;
		else if (down & KEY_SELECT) userScreensOff = true;
		if (down & KEY_A) ui_request_recenter();

		u64 now = osGetTime();
		if (now - lastShellCheck >= 100) {
			lastShellCheck = now;
			if (R_FAILED(PTMU_GetShellState(&shellOpen))) shellOpen = 1;
		}

		bool screensOn = shellOpen && !userScreensOff;
		set_backlight(screensOn);

		if (screensOn) {
			tracker_snapshot(&st);
			ui_render(&st);
		} else {
			/* nothing to draw: keep the loop alive cheaply */
			svcSleepThread(50 * 1000000LL);
		}
	}

	tracker_stop();
	wifilock_end();
	set_backlight(true);
	aptSetSleepAllowed(true);

	socExit();
	free(socBuffer);
	if (haveMcuHwc) mcuHwcExit();
	gspLcdExit();
	ptmuExit();
	cfguExit();
	acExit();
	ui_exit();
	return 0;
}
