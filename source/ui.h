#pragma once
#include "tracker.h"

void ui_init(void);
void ui_exit(void);
void ui_render(const TrackerState* st);
/* Re-aligns the 3D preview so the console faces the viewer. Display only. */
void ui_request_recenter(void);
