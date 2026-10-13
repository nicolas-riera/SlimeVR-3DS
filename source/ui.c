#include "ui.h"
#include "lang.h"
#include "wifilock.h"

#include <citro2d.h>
#include <arpa/inet.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RAD2DEG 57.29578f

typedef struct { u8 r, g, b; } RGB;

static const RGB C_BG_TOP_A = {14, 13, 32};
static const RGB C_BG_TOP_B = {44, 30, 84};
static const RGB C_BODY = {128, 96, 238};
static const RGB C_BODY_IN = {58, 50, 96};
static const RGB C_ACCENT = {170, 130, 255};
static const RGB C_GREEN = {80, 225, 145};
static const RGB C_ORANGE = {255, 182, 72};
static const RGB C_RED = {255, 92, 108};
static const RGB C_TEXT = {236, 236, 250};
static const RGB C_DIM = {150, 148, 188};

static C3D_RenderTarget* s_top;
static C3D_RenderTarget* s_bot;
static C2D_TextBuf s_tb;
static quat s_yawFix = {1, 0, 0, 0};
static bool s_recenterReq = true;
static bool s_wasCalibrated;
static u32 s_frame;
static float s_graphScale = 60.0f;

/* ------------------------------------------------------------- helpers */

static u32 col(RGB c, float k, u8 a) {
	float r = c.r * k, g = c.g * k, b = c.b * k;
	return C2D_Color32(r > 255 ? 255 : (u8)r, g > 255 ? 255 : (u8)g, b > 255 ? 255 : (u8)b, a);
}
static u32 solid(RGB c) { return col(c, 1.0f, 255); }

static float text(float x, float y, float size, u32 color, u32 align, const char* fmt, ...) {
	char buf[160];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	C2D_Text t;
	C2D_TextParse(&t, s_tb, buf);
	C2D_TextOptimize(&t);
	C2D_DrawText(&t, C2D_WithColor | align, x, y, 0.5f, size, size, color);
	float w = 0, h = 0;
	C2D_TextGetDimensions(&t, size, size, &w, &h);
	return w;
}

static void round_rect(float x, float y, float w, float h, float r, u32 c) {
	C2D_DrawRectSolid(x + r, y, 0.5f, w - 2 * r, h, c);
	C2D_DrawRectSolid(x, y + r, 0.5f, r, h - 2 * r, c);
	C2D_DrawRectSolid(x + w - r, y + r, 0.5f, r, h - 2 * r, c);
	C2D_DrawCircleSolid(x + r, y + r, 0.5f, r, c);
	C2D_DrawCircleSolid(x + w - r, y + r, 0.5f, r, c);
	C2D_DrawCircleSolid(x + r, y + h - r, 0.5f, r, c);
	C2D_DrawCircleSolid(x + w - r, y + h - r, 0.5f, r, c);
}

static RGB state_color(const TrackerState* st) {
	switch (st->net.state) {
	case NET_CONNECTED: return C_GREEN;
	case NET_SEARCHING: return C_ORANGE;
	default: return C_RED;
	}
}

static const char* dots(void) {
	static const char* d[4] = {"", ".", "..", "..."};
	return d[(s_frame / 20) % 4];
}

/* A little slime, the SlimeVR mascot style. */
static void draw_slime(float cx, float cy, float r, RGB c, bool happy) {
	C2D_DrawEllipseSolid(cx - r * 1.1f, cy + r * 0.55f, 0.5f, r * 2.2f, r * 0.5f, C2D_Color32(0, 0, 0, 60));
	C2D_DrawEllipse(cx - r, cy - r * 0.85f, 0.5f, r * 2, r * 1.75f, col(c, 1.15f, 255), col(c, 1.15f, 255),
					col(c, 0.75f, 255), col(c, 0.75f, 255));
	C2D_DrawRectSolid(cx - r, cy, 0.5f, r * 2, r * 0.8f, col(c, 0.75f, 255));
	C2D_DrawEllipseSolid(cx - r, cy + r * 0.55f, 0.5f, r * 2, r * 0.5f, col(c, 0.75f, 255));
	C2D_DrawEllipseSolid(cx - r * 0.55f, cy - r * 0.6f, 0.5f, r * 0.45f, r * 0.3f, C2D_Color32(255, 255, 255, 110));
	u32 eye = C2D_Color32(25, 20, 40, 255);
	float er = r * 0.14f;
	if (happy) {
		C2D_DrawCircleSolid(cx - r * 0.35f, cy + r * 0.05f, 0.5f, er, eye);
		C2D_DrawCircleSolid(cx + r * 0.35f, cy + r * 0.05f, 0.5f, er, eye);
	} else {
		C2D_DrawRectSolid(cx - r * 0.5f, cy + r * 0.02f, 0.5f, r * 0.3f, er * 0.8f, eye);
		C2D_DrawRectSolid(cx + r * 0.2f, cy + r * 0.02f, 0.5f, r * 0.3f, er * 0.8f, eye);
	}
}

/* ------------------------------------------------------------- 3D preview */

typedef struct {
	vec3 pos, r, u, f;
	float focal, cx, cy;
} Camera;

static Camera s_cam;
static quat s_qd;          /* displayed orientation */
static vec3 s_pivot;       /* model pivot in sensor frame */
static const vec3 LIGHT = {-0.35f, -0.55f, 0.76f};

typedef struct { float x, y; } P2;

static P2 project(vec3 p) {
	vec3 d = v3_sub(p, s_cam.pos);
	float z = v3_dot(d, s_cam.f);
	if (z < 0.5f) z = 0.5f;
	return (P2){s_cam.cx + s_cam.focal * v3_dot(d, s_cam.r) / z, s_cam.cy - s_cam.focal * v3_dot(d, s_cam.u) / z};
}

/* sensor-frame model point -> world */
static vec3 xf(vec3 p) { return q_rotate(s_qd, v3_sub(p, s_pivot)); }

static void tri(P2 a, P2 b, P2 c, u32 ca, u32 cb, u32 cc) {
	C2D_DrawTriangle(a.x, a.y, ca, b.x, b.y, cb, c.x, c.y, cc, 0.5f);
}

static void quad_world(const vec3 w[4], u32 c0, u32 c1, u32 c2, u32 c3) {
	P2 p[4];
	for (int i = 0; i < 4; i++) p[i] = project(w[i]);
	tri(p[0], p[1], p[2], c0, c1, c2);
	tri(p[0], p[2], p[3], c0, c2, c3);
}

/* Face-local decal coordinates: origin + u*U + v*V (sensor frame). */
typedef struct { vec3 o, U, V; } Plane;

static vec3 plane_pt(const Plane* pl, float u, float v) {
	return xf(v3_add(pl->o, v3_add(v3_scale(pl->U, u), v3_scale(pl->V, v))));
}

static void decal_rect(const Plane* pl, float u0, float v0, float u1, float v1, u32 c0, u32 c1) {
	vec3 w[4] = {plane_pt(pl, u0, v0), plane_pt(pl, u1, v0), plane_pt(pl, u1, v1), plane_pt(pl, u0, v1)};
	quad_world(w, c0, c0, c1, c1);
}

static void decal_ellipse(const Plane* pl, float cu, float cv, float ru, float rv, u32 c) {
	enum { N = 14 };
	P2 p[N];
	for (int i = 0; i < N; i++) {
		float a = (float)i / N * 6.2831853f;
		p[i] = project(plane_pt(pl, cu + cosf(a) * ru, cv + sinf(a) * rv));
	}
	for (int i = 1; i < N - 1; i++) tri(p[0], p[i], p[i + 1], c, c, c);
}

typedef struct {
	vec3 w[4];
	vec3 n;
	float depth;
	RGB color;
	int part, face;
} Face;

static const int BOX_FACES[6][4] = {
	{0, 2, 6, 4}, {1, 3, 7, 5}, {0, 1, 5, 4}, {2, 3, 7, 6}, {0, 1, 3, 2}, {4, 5, 7, 6},
};

static int add_box(Face* out, int part, vec3 o, vec3 a, vec3 b, vec3 c, RGB body, RGB inner) {
	vec3 corner[8];
	vec3 center = v3(0, 0, 0);
	for (int i = 0; i < 8; i++) {
		vec3 p = o;
		if (i & 1) p = v3_add(p, a);
		if (i & 2) p = v3_add(p, b);
		if (i & 4) p = v3_add(p, c);
		corner[i] = xf(p);
		center = v3_add(center, corner[i]);
	}
	center = v3_scale(center, 1.0f / 8);
	for (int f = 0; f < 6; f++) {
		Face* F = &out[f];
		vec3 fc = v3(0, 0, 0);
		for (int k = 0; k < 4; k++) {
			F->w[k] = corner[BOX_FACES[f][k]];
			fc = v3_add(fc, F->w[k]);
		}
		fc = v3_scale(fc, 0.25f);
		F->n = v3_norm(v3_sub(fc, center));
		F->depth = v3_dot(v3_sub(fc, s_cam.pos), s_cam.f);
		F->color = (f == 3) ? inner : body;
		F->part = part;
		F->face = f;
	}
	return 6;
}

/* Sensor-frame geometry of an open 3DS (centimetres). */
#define HALF_W   6.7f
#define HALF_D   3.7f
#define BOT_T    1.1f
#define TOP_T    0.9f
#define TOP_LEN  7.4f
#define OPEN_ANG 2.705f /* ~155 degrees */

static void draw_decals(const Face* F, RGB screenCol, bool connected) {
	float k = 0.35f + 0.65f * fmaxf(0.0f, v3_dot(F->n, LIGHT));
	if (F->part == 0 && F->face == 3) {
		Plane pl = {v3(0, 0.02f, 0), v3(1, 0, 0), v3(0, 0, 1)};
		u32 bezel = C2D_Color32(18, 18, 28, 255);
		decal_rect(&pl, -3.6f, -2.9f, 3.6f, 2.6f, bezel, bezel);
		decal_rect(&pl, -3.2f, -2.6f, 3.2f, 2.3f, col(screenCol, 0.25f, 255), col(screenCol, 0.45f, 255));
		/* circle pad, d-pad (left = +X), ABXY (right) */
		decal_ellipse(&pl, 5.3f, 1.7f, 0.78f, 0.78f, col((RGB){70, 70, 84}, k, 255));
		decal_ellipse(&pl, 5.3f, 1.7f, 0.55f, 0.55f, col((RGB){120, 120, 135}, k, 255));
		u32 dp = col((RGB){40, 40, 52}, k, 255);
		decal_rect(&pl, 4.7f, -1.4f, 5.9f, -1.0f, dp, dp);
		decal_rect(&pl, 5.1f, -1.8f, 5.5f, -0.6f, dp, dp);
		decal_ellipse(&pl, -5.3f, 1.75f, 0.24f, 0.24f, col((RGB){90, 140, 255}, k, 255));  /* X */
		decal_ellipse(&pl, -5.3f, 0.65f, 0.24f, 0.24f, col((RGB){255, 210, 80}, k, 255));  /* B */
		decal_ellipse(&pl, -4.75f, 1.2f, 0.24f, 0.24f, col((RGB){90, 220, 120}, k, 255));  /* Y */
		decal_ellipse(&pl, -5.85f, 1.2f, 0.24f, 0.24f, col((RGB){255, 90, 100}, k, 255)); /* A */
	} else if (F->part == 1 && F->face == 3) {
		vec3 d = v3(0, sinf(OPEN_ANG), -cosf(OPEN_ANG));
		vec3 n = v3(0, -cosf(OPEN_ANG), -sinf(OPEN_ANG));
		vec3 H = v3(0, 0, HALF_D);
		Plane pl = {v3_add(H, v3_scale(n, 0.02f)), v3(1, 0, 0), d};
		u32 bezel = C2D_Color32(18, 18, 28, 255);
		decal_rect(&pl, -5.1f, 0.6f, 5.1f, 6.7f, bezel, bezel);
		decal_rect(&pl, -4.3f, 1.0f, 4.3f, 6.2f, col(screenCol, 0.35f, 255), col(screenCol, 0.85f, 255));
		/* tiny slime on the virtual top screen */
		RGB slime = connected ? C_GREEN : C_ACCENT;
		decal_ellipse(&pl, 0, 3.4f, 1.3f, 1.05f, col(slime, 1.0f, 255));
		decal_rect(&pl, -1.3f, 2.4f, 1.3f, 3.4f, col(slime, 1.0f, 255), col(slime, 1.0f, 255));
		u32 eye = C2D_Color32(20, 18, 34, 255);
		decal_ellipse(&pl, 0.45f, 3.3f, 0.17f, 0.2f, eye);
		decal_ellipse(&pl, -0.45f, 3.3f, 0.17f, 0.2f, eye);
	}
}

static void draw_model(const TrackerState* st) {
	Face faces[12];
	int nf = 0;

	nf += add_box(faces + nf, 0, v3(-HALF_W, -BOT_T, -HALF_D), v3(2 * HALF_W, 0, 0), v3(0, BOT_T, 0),
				  v3(0, 0, 2 * HALF_D), C_BODY, C_BODY_IN);

	vec3 d = v3(0, sinf(OPEN_ANG), -cosf(OPEN_ANG));   /* along the top half */
	vec3 n = v3(0, -cosf(OPEN_ANG), -sinf(OPEN_ANG));  /* top screen normal */
	vec3 H = v3(0, 0, HALF_D);
	vec3 o = v3_add(v3_add(H, v3(-HALF_W, 0, 0)), v3_scale(n, -TOP_T));
	nf += add_box(faces + nf, 1, o, v3(2 * HALF_W, 0, 0), v3_scale(n, TOP_T), v3_scale(d, TOP_LEN), C_BODY,
				  C_BODY_IN);

	/* painter's algorithm, far to near */
	int order[12];
	for (int i = 0; i < nf; i++) order[i] = i;
	for (int i = 1; i < nf; i++)
		for (int j = i; j > 0 && faces[order[j]].depth > faces[order[j - 1]].depth; j--) {
			int t = order[j];
			order[j] = order[j - 1];
			order[j - 1] = t;
		}

	RGB scr = state_color(st);
	for (int i = 0; i < nf; i++) {
		const Face* F = &faces[order[i]];
		vec3 fc = v3_scale(v3_add(v3_add(F->w[0], F->w[1]), v3_add(F->w[2], F->w[3])), 0.25f);
		if (v3_dot(F->n, v3_sub(s_cam.pos, fc)) <= 0) continue;
		float k = 0.35f + 0.65f * fmaxf(0.0f, v3_dot(F->n, LIGHT));
		u32 c = col(F->color, k, 255);
		quad_world(F->w, c, c, c, c);
		P2 p[4];
		for (int j = 0; j < 4; j++) p[j] = project(F->w[j]);
		u32 edge = col(F->color, k * 0.6f, 255);
		for (int j = 0; j < 4; j++)
			C2D_DrawLine(p[j].x, p[j].y, edge, p[(j + 1) & 3].x, p[(j + 1) & 3].y, edge, 1.0f, 0.5f);
		draw_decals(F, scr, st->net.state == NET_CONNECTED);
	}
}

static void draw_ground(void) {
	const float z = -8.5f;
	/* soft shadow */
	enum { N = 20 };
	P2 c = project(v3(0, 0, z));
	P2 prev = project(v3(7.5f, 0, z));
	for (int i = 1; i <= N; i++) {
		float a = (float)i / N * 6.2831853f;
		P2 p = project(v3(cosf(a) * 7.5f, sinf(a) * 7.5f, z));
		tri(c, prev, p, C2D_Color32(0, 0, 0, 90), C2D_Color32(0, 0, 0, 0), C2D_Color32(0, 0, 0, 0));
		prev = p;
	}
	for (int i = -5; i <= 5; i++) {
		float t = i * 3.0f;
		u8 a = (u8)(70 - abs(i) * 11);
		u32 cc = C2D_Color32(120, 110, 220, a), ce = C2D_Color32(120, 110, 220, 0);
		P2 a0 = project(v3(t, -15, z)), am = project(v3(t, 0, z)), a1 = project(v3(t, 15, z));
		C2D_DrawLine(a0.x, a0.y, ce, am.x, am.y, cc, 1.0f, 0.5f);
		C2D_DrawLine(am.x, am.y, cc, a1.x, a1.y, ce, 1.0f, 0.5f);
		P2 b0 = project(v3(-15, t, z)), bm = project(v3(0, t, z)), b1 = project(v3(15, t, z));
		C2D_DrawLine(b0.x, b0.y, ce, bm.x, bm.y, cc, 1.0f, 0.5f);
		C2D_DrawLine(bm.x, bm.y, cc, b1.x, b1.y, ce, 1.0f, 0.5f);
	}
}

static void update_recenter(const TrackerState* st) {
	if (st->calibrated && !s_wasCalibrated) s_recenterReq = true;
	s_wasCalibrated = st->calibrated;
	if (!s_recenterReq || !st->fusionReady) return;
	s_recenterReq = false;
	/* heading of the hinge direction; if it points up/down use the screen normal */
	vec3 f = q_rotate(st->q, v3(0, 0, 1));
	if (sqrtf(f.x * f.x + f.y * f.y) < 0.4f) f = q_rotate(st->q, v3(0, 1, 0));
	float yaw = atan2f(f.x, f.y);
	s_yawFix = q_from_axis_angle(v3(0, 0, 1), yaw);
}

static void draw_top(const TrackerState* st) {
	C2D_DrawRectangle(0, 0, 0.5f, 400, 240, solid(C_BG_TOP_A), solid(C_BG_TOP_A), solid(C_BG_TOP_B),
					  solid(C_BG_TOP_B));

	s_qd = q_mul(s_yawFix, st->q);
	draw_ground();
	if (st->fusionReady) draw_model(st);

	/* title */
	float w = text(12, 8, 0.62f, solid(C_ACCENT), C2D_AlignLeft, "SlimeVR");
	text(14 + w, 11, 0.5f, solid(C_DIM), C2D_AlignLeft, "%s", tr(S_FOR_3DS));

	/* status pill */
	RGB sc = state_color(st);
	const char* label = tr(st->net.state == NET_CONNECTED   ? S_PILL_CONNECTED
						   : st->net.state == NET_SEARCHING ? S_PILL_SEARCHING
															: S_PILL_NO_WIFI);
	round_rect(292, 8, 100, 22, 11, col(sc, 0.25f, 230));
	float pulse = st->net.state == NET_SEARCHING ? 0.6f + 0.4f * sinf(s_frame * 0.12f) : 1.0f;
	C2D_DrawCircleSolid(305, 19, 0.5f, 4.5f, col(sc, pulse, 255));
	text(315, 11, 0.47f, solid(C_TEXT), C2D_AlignLeft, "%s", label);

	/* attitude readout */
	if (st->fusionReady) {
		vec3 h = q_rotate(s_qd, v3(0, 0, 1)), u = q_rotate(s_qd, v3(0, 1, 0)), l = q_rotate(s_qd, v3(1, 0, 0));
		float pitch = asinf(fmaxf(-1, fminf(1, h.z))) * RAD2DEG;
		float roll = atan2f(l.z, u.z) * RAD2DEG;
		float yaw = atan2f(h.x, h.y) * RAD2DEG;
		text(12, 32, 0.42f, solid(C_DIM), C2D_AlignLeft, tr(S_ATTITUDE_FMT), pitch, roll, yaw);
	}

	if (!st->sensorsOk) {
		round_rect(70, 196, 260, 30, 8, C2D_Color32(80, 20, 30, 230));
		text(200, 203, 0.5f, solid(C_RED), C2D_AlignCenter, "%s", tr(S_SENSORS_UNAVAILABLE));
	} else if (!st->calibrated) {
		round_rect(40, 186, 320, 44, 8, C2D_Color32(20, 16, 44, 220));
		text(200, 190, 0.45f, solid(C_ORANGE), C2D_AlignCenter, "%s", tr(S_CALIBRATING));
		C2D_DrawRectSolid(60, 214, 0.5f, 280, 8, C2D_Color32(60, 50, 100, 255));
		C2D_DrawRectSolid(60, 214, 0.5f, 280 * st->calibProgress, 8, solid(C_ORANGE));
	} else if (st->net.state == NET_CONNECTED) {
		text(200, 220, 0.42f, solid(C_DIM), C2D_AlignCenter, "%s", tr(S_LID_HINT));
	}
}

/* ------------------------------------------------------------- bottom screen */

static void card(float x, float y, float w, float h, const char* title) {
	round_rect(x, y, w, h, 8, C2D_Color32(32, 29, 62, 255));
	text(x + 9, y + 4, 0.42f, solid(C_ACCENT), C2D_AlignLeft, "%s", title);
}

static void signal_bars(float x, float y, int strength) {
	for (int i = 0; i < 3; i++) {
		float h = 4 + i * 3.5f;
		u32 c = i < strength ? solid(C_GREEN) : C2D_Color32(80, 76, 120, 255);
		C2D_DrawRectSolid(x + i * 6, y + 12 - h, 0.5f, 4, h, c);
	}
}

static void battery_icon(float x, float y, int pct, bool charging) {
	C2D_DrawRectSolid(x, y, 0.5f, 24, 12, C2D_Color32(200, 200, 220, 255));
	C2D_DrawRectSolid(x + 24, y + 3, 0.5f, 2, 6, C2D_Color32(200, 200, 220, 255));
	C2D_DrawRectSolid(x + 1.5f, y + 1.5f, 0.5f, 21, 9, C2D_Color32(20, 18, 36, 255));
	if (pct >= 0) {
		RGB c = pct > 40 ? C_GREEN : pct > 15 ? C_ORANGE : C_RED;
		C2D_DrawRectSolid(x + 2.5f, y + 2.5f, 0.5f, 19 * pct / 100.0f, 7, solid(c));
	}
	if (charging) text(x - 4, y - 1, 0.42f, solid(C_ORANGE), C2D_AlignRight, "+");
}

static void draw_graph(float x, float y, float w, float h, const TrackerState* st) {
	round_rect(x, y, w, h, 5, C2D_Color32(18, 16, 38, 255));
	C2D_DrawRectSolid(x + 4, y + h / 2, 0.5f, w - 8, 1, C2D_Color32(70, 64, 120, 255));

	float maxAbs = 0;
	for (int i = 0; i < HIST_LEN; i++) {
		vec3 v = st->hist[i];
		maxAbs = fmaxf(maxAbs, fmaxf(fabsf(v.x), fmaxf(fabsf(v.y), fabsf(v.z))));
	}
	float target = fmaxf(30.0f, maxAbs * 1.15f);
	s_graphScale += (target - s_graphScale) * (target > s_graphScale ? 0.3f : 0.03f);

	static const RGB lines[3] = {{255, 100, 115}, {90, 230, 140}, {110, 165, 255}};
	float sx = (w - 8) / (HIST_LEN - 1), sy = (h / 2 - 4) / s_graphScale;
	for (int axis = 0; axis < 3; axis++) {
		u32 c = solid(lines[axis]);
		float px = 0, py = 0;
		for (int i = 0; i < HIST_LEN; i++) {
			vec3 v = st->hist[(st->histHead + i) % HIST_LEN];
			float val = axis == 0 ? v.x : axis == 1 ? v.y : v.z;
			float gx = x + 4 + i * sx, gy = y + h / 2 - val * sy;
			if (i) C2D_DrawLine(px, py, c, gx, gy, c, 1.2f, 0.5f);
			px = gx;
			py = gy;
		}
	}
	text(x + w - 4, y + 2, 0.36f, solid(C_DIM), C2D_AlignRight, "±%.0f°/s", s_graphScale);
	text(x + 5, y + h - 14, 0.36f, solid(lines[0]), C2D_AlignLeft, "X");
	text(x + 15, y + h - 14, 0.36f, solid(lines[1]), C2D_AlignLeft, "Y");
	text(x + 25, y + h - 14, 0.36f, solid(lines[2]), C2D_AlignLeft, "Z");
}

static void draw_bottom(const TrackerState* st) {
	const NetStatus* n = &st->net;
	RGB sc = state_color(st);
	char ip[20];

	C2D_DrawRectangle(0, 0, 0.5f, 320, 38, C2D_Color32(44, 30, 84, 255), C2D_Color32(44, 30, 84, 255),
					  C2D_Color32(24, 20, 50, 255), C2D_Color32(24, 20, 50, 255));
	draw_slime(22, 19, 11, sc, n->state == NET_CONNECTED);
	text(42, 3, 0.55f, solid(C_TEXT), C2D_AlignLeft, "%s", tr(S_TITLE));
	const char* sub = tr(n->state == NET_CONNECTED   ? (n->sensorAcked ? S_SUB_TRACKING : S_SUB_REGISTERING)
						 : n->state == NET_SEARCHING ? S_SUB_SEARCHING
													 : S_SUB_NO_WIFI);
	text(42, 21, 0.42f, solid(sc), C2D_AlignLeft, "%s%s", sub, n->state == NET_SEARCHING ? dots() : "");
	battery_icon(286, 13, n->battery, n->charging);
	if (n->battery >= 0) text(280, 12, 0.42f, solid(C_DIM), C2D_AlignRight, "%d%%", n->battery);

	/* network card */
	card(6, 44, 308, 88, tr(S_NETWORK));
	text(306, 48, 0.36f, solid(C_DIM), C2D_AlignRight, "ID %02X:%02X:%02X:%02X:%02X:%02X", n->mac[0], n->mac[1],
		 n->mac[2], n->mac[3], n->mac[4], n->mac[5]);
	const float lx = 15, vx = 88;
	float y = 63;
	text(lx, y, 0.45f, solid(C_DIM), C2D_AlignLeft, "%s", tr(S_WIFI));
	if (n->state != NET_NO_WIFI) {
		float w = text(vx, y, 0.47f, solid(C_TEXT), C2D_AlignLeft, "%s", tr(S_CONNECTED));
		signal_bars(vx + w + 8, y + 1, n->wifiStrength);
		if (wifilock_active()) text(vx + w + 34, y + 1, 0.4f, solid(C_GREEN), C2D_AlignLeft, "%s", tr(S_LOCKED));
		else text(vx + w + 34, y + 1, 0.4f, solid(C_ORANGE), C2D_AlignLeft, "%s", tr(S_NOT_LOCKED));
	} else {
		text(vx, y, 0.47f, solid(C_RED), C2D_AlignLeft, "%s", tr(S_NOT_CONNECTED));
	}
	y += 16;
	text(lx, y, 0.45f, solid(C_DIM), C2D_AlignLeft, "%s", tr(S_CONSOLE));
	if (n->localIp) {
		struct in_addr a = {.s_addr = n->localIp};
		snprintf(ip, sizeof(ip), "%s", inet_ntoa(a));
		text(vx, y, 0.47f, solid(C_TEXT), C2D_AlignLeft, "%s", ip);
	} else {
		text(vx, y, 0.47f, solid(C_DIM), C2D_AlignLeft, "-");
	}
	y += 16;
	text(lx, y, 0.45f, solid(C_DIM), C2D_AlignLeft, "%s", tr(S_SERVER));
	if (n->state == NET_CONNECTED) {
		struct in_addr a = {.s_addr = n->serverIp};
		snprintf(ip, sizeof(ip), "%s", inet_ntoa(a));
		text(vx, y, 0.47f, solid(C_GREEN), C2D_AlignLeft, "%s:%u", ip, n->serverPort);
	} else if (n->state == NET_SEARCHING) {
		text(vx, y, 0.47f, solid(C_ORANGE), C2D_AlignLeft, tr(S_SEARCHING_FMT), dots(),
			 tr(n->scanning ? S_SCANNING : S_BROADCAST));
	} else {
		text(vx, y, 0.47f, solid(C_DIM), C2D_AlignLeft, "-");
	}
	y += 16;
	if (n->state == NET_CONNECTED) {
		text(lx, y, 0.45f, solid(C_DIM), C2D_AlignLeft, "%s", tr(S_SENDING));
		text(vx, y, 0.47f, solid(C_TEXT), C2D_AlignLeft, tr(S_SENDING_FMT), (unsigned long)n->sentPerSec,
			 tr(n->sensorAcked ? S_IMU_REGISTERED : S_IMU_PENDING));
	} else if (n->state == NET_SEARCHING && n->searchMs > 8000) {
		text(lx, y, 0.42f, solid(C_ORANGE), C2D_AlignLeft, "%s", tr(S_ACCEPT_HINT));
	}

	/* gyroscope card */
	card(6, 138, 308, 74, tr(S_GYROSCOPE));
	text(140, 142, 0.38f, solid(C_DIM), C2D_AlignRight, "%.0f Hz", st->gyroHz);
	y = 157;
	text(lx, y, 0.42f, solid(C_DIM), C2D_AlignLeft, "%s", tr(S_CALIB));
	if (!st->sensorsOk) text(60, y, 0.44f, solid(C_RED), C2D_AlignLeft, "%s", tr(S_ERROR));
	else if (st->calibrated) text(60, y, 0.44f, solid(C_GREEN), C2D_AlignLeft, "OK");
	else text(60, y, 0.44f, solid(C_ORANGE), C2D_AlignLeft, "%d%%", (int)(st->calibProgress * 100));
	y += 15;
	text(lx, y, 0.42f, solid(C_DIM), C2D_AlignLeft, "%s", tr(S_STATE));
	text(60, y, 0.44f, solid(C_TEXT), C2D_AlignLeft, "%s", tr(st->atRest ? S_AT_REST : S_MOVING));
	y += 15;
	float speed = v3_len(st->gyroDps);
	text(lx, y, 0.42f, solid(C_DIM), C2D_AlignLeft, "%s", tr(S_SPEED));
	text(60, y, 0.44f, solid(C_TEXT), C2D_AlignLeft, "%.0f°/s  %.2fg", speed, v3_len(st->accG));
	draw_graph(148, 142, 162, 66, st);

	/* footer */
	text(160, 220, 0.42f, solid(C_DIM), C2D_AlignCenter, "%s", tr(S_FOOTER));
}

/* ------------------------------------------------------------- api */

void ui_init(void) {
	gfxInitDefault();
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	C2D_Init(8192);
	C2D_Prepare();
	s_top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
	s_bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
	s_tb = C2D_TextBufNew(4096);

	s_pivot = v3(0, 1.3f, 2.6f);
	s_cam.pos = v3(0, -24.0f, 12.0f);
	s_cam.f = v3_norm(v3_sub(v3(0, 0, -0.5f), s_cam.pos));
	s_cam.r = v3_norm(v3_cross(s_cam.f, v3(0, 0, 1)));
	s_cam.u = v3_cross(s_cam.r, s_cam.f);
	s_cam.focal = 300.0f;
	s_cam.cx = 200.0f;
	s_cam.cy = 136.0f;
}

void ui_exit(void) {
	C2D_TextBufDelete(s_tb);
	C2D_Fini();
	C3D_Fini();
	gfxExit();
}

void ui_request_recenter(void) {
	s_recenterReq = true;
}

void ui_render(const TrackerState* st) {
	s_frame++;
	update_recenter(st);
	C2D_TextBufClear(s_tb);
	C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
	C2D_TargetClear(s_top, solid(C_BG_TOP_A));
	C2D_SceneBegin(s_top);
	draw_top(st);
	C2D_TargetClear(s_bot, C2D_Color32(16, 15, 34, 255));
	C2D_SceneBegin(s_bot);
	draw_bottom(st);
	C3D_FrameEnd(0);
}
