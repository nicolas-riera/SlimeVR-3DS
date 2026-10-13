#include "tracker.h"

#include <string.h>

#include "fusion.h"
#include "net.h"
#include "sensors.h"

#define RAD2DEG 57.29578f

static Thread s_thread;
static volatile bool s_running;
static LightLock s_lock;
static TrackerState s_state;
static Fusion s_fusion;
static bool s_sensorsOk;

/* Sensors, filter and network all run here, independently of the UI, so the
   tracker keeps working with the lid closed and the screens off. */
static void worker(void* arg) {
	(void)arg;
	vec3 samples[32];
	int decim = 0;

	while (s_running) {
		u64 now = osGetTime();
		int n = s_sensorsOk ? sensors_read_gyro(samples, 32) : 0;
		vec3 acc = s_sensorsOk ? sensors_read_accel() : v3(0, 0, 0);
		float dt = sensors_gyro_period();

		for (int i = 0; i < n; i++) fusion_update(&s_fusion, samples[i], acc, dt);

		net_tick(now, n > 0 && s_fusion.initialized, s_fusion.q);

		LightLock_Lock(&s_lock);
		s_state.sensorsOk = s_sensorsOk;
		s_state.fusionReady = s_fusion.initialized;
		s_state.q = s_fusion.q;
		s_state.calibrated = s_fusion.calibrated;
		s_state.calibProgress = s_fusion.calibProgress;
		s_state.atRest = s_fusion.atRest;
		s_state.gyroHz = 1.0f / dt;
		s_state.accG = v3_scale(acc, 1.0f / s_fusion.accScale);
		for (int i = 0; i < n; i++) {
			vec3 w = v3_scale(v3_sub(samples[i], s_fusion.bias), RAD2DEG);
			s_state.gyroDps = w;
			if (++decim >= 2) {
				decim = 0;
				s_state.hist[s_state.histHead] = w;
				s_state.histHead = (s_state.histHead + 1) % HIST_LEN;
			}
		}
		s_state.net = *net_status();
		LightLock_Unlock(&s_lock);

		svcSleepThread(4 * 1000000LL);
	}
}

void tracker_start(bool haveMcuHwc) {
	LightLock_Init(&s_lock);
	memset(&s_state, 0, sizeof(s_state));
	fusion_init(&s_fusion);
	s_sensorsOk = sensors_init();
	net_init(haveMcuHwc);

	s32 prio = 0x30;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	s_running = true;
	s_thread = threadCreate(worker, NULL, 64 * 1024, prio > 0x19 ? prio - 1 : prio, -2, false);
}

void tracker_stop(void) {
	s_running = false;
	if (s_thread) {
		threadJoin(s_thread, U64_MAX);
		threadFree(s_thread);
		s_thread = NULL;
	}
	net_exit();
	if (s_sensorsOk) sensors_exit();
}

void tracker_snapshot(TrackerState* out) {
	LightLock_Lock(&s_lock);
	*out = s_state;
	LightLock_Unlock(&s_lock);
}
