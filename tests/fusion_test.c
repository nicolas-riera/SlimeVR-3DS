/*
 * Host-side test of the orientation filter.
 *   gcc -O2 -I../source fusion_test.c ../source/fusion.c -lm -o fusion_test && ./fusion_test
 *
 * Simulates a 3DS following a known trajectory (sensor frame of sensors.h),
 * generates gyro (with bias + noise) and accelerometer samples at 100 Hz,
 * and checks the estimated attitude against the truth.
 */
#include <stdio.h>
#include <stdlib.h>
#include "fusion.h"

#define DEG2RAD 0.017453292f
#define RAD2DEG 57.29578f

static float noise(float s) {
	float u = 0;
	for (int i = 0; i < 6; i++) u += (float)rand() / RAND_MAX;
	return (u - 3.0f) * s;
}

/* truth: rest 4 s, then a mix of slow and fast rotations, then rest */
static quat truth(float t, quat q0) {
	if (t < 4.0f) return q0;
	float u = t - 4.0f;
	if (u > 20.0f) u = 20.0f;
	vec3 r = v3(0.9f * sinf(u * 1.3f), 0.6f * (sinf(u * 0.7f + 1.0f) - sinf(1.0f)) + 0.3f * u, 0.8f * sinf(u * 2.1f));
	return q_mul(q0, q_from_rotvec(r));
}

static float angle_between(vec3 a, vec3 b) {
	float d = v3_dot(v3_norm(a), v3_norm(b));
	if (d > 1) d = 1;
	if (d < -1) d = -1;
	return acosf(d) * RAD2DEG;
}

static float yaw_offset(quat est, quat tru) {
	quat d = q_mul(est, q_conj(tru));
	return 2.0f * atan2f(d.z, d.w) * RAD2DEG;
}

int main(int argc, char** argv) {
	int verbose = argc > 1;
	(void)argv;
	Fusion f;
	fusion_init(&f);
	const float dt = 0.0099f;
	const vec3 bias = v3(1.2f * DEG2RAD, -0.8f * DEG2RAD, 0.5f * DEG2RAD);
	/* console lying open on a table: sensor +Y up */
	quat q0 = q_from_two_vectors(v3(0, 1, 0), v3(0, 0, 1));

	quat prev = truth(0, q0);
	float maxTilt = 0, tiltAt30 = 0;
	quat estRef = q_identity(), truthRef = q_identity();
	int failures = 0;

	for (int i = 1; i < (int)(30.0f / dt); i++) {
		float t = i * dt;
		quat q = truth(t, q0);
		/* body rate from consecutive truth orientations */
		quat dq = q_mul(q_conj(prev), q);
		if (dq.w < 0) dq = (quat){-dq.w, -dq.x, -dq.y, -dq.z};
		float s = sqrtf(dq.x * dq.x + dq.y * dq.y + dq.z * dq.z);
		float ang = 2.0f * atan2f(s, dq.w);
		vec3 w = s > 1e-9f ? v3_scale(v3(dq.x, dq.y, dq.z), ang / s / dt) : v3(0, 0, 0);
		prev = q;

		vec3 gyro = v3_add(v3_add(w, bias), v3(noise(0.15f * DEG2RAD), noise(0.15f * DEG2RAD), noise(0.15f * DEG2RAD)));
		/* specific force (up) in the body frame, 512 raw per g */
		vec3 acc = v3_scale(q_rotate(q_conj(q), v3(0, 0, 1)), 512.0f);
		acc = v3_add(acc, v3(noise(3), noise(3), noise(3)));

		fusion_update(&f, gyro, acc, dt);

		if (t > 3.0f) {
			vec3 upTrue = q_rotate(q_conj(q), v3(0, 0, 1));
			vec3 upEst = q_rotate(q_conj(f.q), v3(0, 0, 1));
			float tilt = angle_between(upTrue, upEst);
			if (t > 6.0f && tilt > maxTilt) maxTilt = tilt;
			tiltAt30 = tilt;
		}
		if (i == (int)(4.0f / dt)) {
			estRef = f.q;
			truthRef = q;
		}
		if (verbose && i % 100 == 0)
			printf("t=%5.1f yaw offset=%7.2f rest=%d\n", t, yaw_offset(f.q, q), f.atRest);
	}

	/* yaw drift: the estimate equals the truth up to a heading offset; measure
	   how much that offset changed between t=4 s and the end. */
	float yawErr = fabsf(remainderf(yaw_offset(f.q, prev) - yaw_offset(estRef, truthRef), 360.0f));

	vec3 b = v3_scale(f.bias, RAD2DEG);
	printf("calibrated=%d bias est = (%.2f, %.2f, %.2f) dps (true 1.20 -0.80 0.50)\n", f.calibrated, b.x, b.y, b.z);
	printf("max tilt error during motion = %.2f deg, final = %.2f deg\n", maxTilt, tiltAt30);
	printf("relative yaw error after 26 s of motion = %.2f deg\n", yawErr);

	if (!f.calibrated) failures++;
	if (fabsf(b.x - 1.2f) > 0.15f || fabsf(b.y + 0.8f) > 0.15f || fabsf(b.z - 0.5f) > 0.15f) failures++;
	if (maxTilt > 5.0f || tiltAt30 > 2.0f) failures++;
	if (yawErr > 5.0f) failures++;
	printf(failures ? "FAIL\n" : "PASS\n");
	return failures != 0;
}
