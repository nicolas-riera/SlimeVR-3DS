#pragma once
#include <3ds.h>
#include <stdbool.h>
#include "mathx.h"

#define HIST_LEN 150

typedef enum {
	NET_NO_WIFI,
	NET_SEARCHING,
	NET_CONNECTED,
} NetState;

typedef struct {
	NetState state;
	u32 localIp;        /* network byte order */
	u32 serverIp;       /* network byte order */
	u16 serverPort;
	u32 sentPerSec;
	bool sensorAcked;   /* server registered our IMU */
	bool scanning;      /* unicast sweep of the subnet in progress */
	u32 searchMs;       /* time spent searching */
	u8 mac[6];
	int wifiStrength;   /* 0..3 */
	int battery;        /* percent, -1 unknown */
	bool charging;
} NetStatus;

/* Snapshot of everything the UI needs, copied under a lock. */
typedef struct {
	bool sensorsOk;
	bool fusionReady;
	quat q;             /* sensor -> world (Z up) */
	vec3 gyroDps;       /* bias corrected */
	vec3 accG;
	float gyroHz;
	bool calibrated;
	float calibProgress;
	bool atRest;
	vec3 hist[HIST_LEN];
	int histHead;
	NetStatus net;
} TrackerState;

void tracker_start(void);
void tracker_stop(void);
void tracker_snapshot(TrackerState* out);
