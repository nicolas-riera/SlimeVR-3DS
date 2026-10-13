#pragma once

/* UI strings. English is the default; other languages follow the console's
   system language when a translation exists. */
typedef enum {
	S_FOR_3DS,
	S_PILL_CONNECTED,
	S_PILL_SEARCHING,
	S_PILL_NO_WIFI,
	S_ATTITUDE_FMT,
	S_SENSORS_UNAVAILABLE,
	S_CALIBRATING,
	S_LID_HINT,
	S_TITLE,
	S_SUB_TRACKING,
	S_SUB_REGISTERING,
	S_SUB_SEARCHING,
	S_SUB_NO_WIFI,
	S_NETWORK,
	S_WIFI,
	S_CONNECTED,
	S_LOCKED,
	S_NOT_LOCKED,
	S_NOT_CONNECTED,
	S_CONSOLE,
	S_SERVER,
	S_SEARCHING_FMT,
	S_SCANNING,
	S_BROADCAST,
	S_SENDING,
	S_SENDING_FMT,
	S_IMU_REGISTERED,
	S_IMU_PENDING,
	S_ACCEPT_HINT,
	S_GYROSCOPE,
	S_CALIB,
	S_ERROR,
	S_STATE,
	S_AT_REST,
	S_MOVING,
	S_SPEED,
	S_FOOTER,
	S_COUNT
} StrId;

/* Call after cfguInit(). */
void lang_init(void);
const char* tr(StrId id);
