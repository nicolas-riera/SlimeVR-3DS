#pragma once
#include "tracker.h"

/* SlimeVR UDP protocol client with automatic server discovery. */
void net_init(bool haveMcuHwc);
void net_exit(void);
/* Call often (every few ms). sendRotation: a new orientation is available. */
void net_tick(u64 nowMs, bool sendRotation, quat q);
const NetStatus* net_status(void);
