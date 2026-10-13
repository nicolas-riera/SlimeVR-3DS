#include "net.h"
#include "wifilock.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define SLIME_PORT       6969
#define CONFIG_DIR       "sdmc:/3ds/SlimeVR-3DS"
#define SERVER_FILE      CONFIG_DIR "/server.txt"

#define FIRMWARE_NAME    "3DS-SlimeVR 1.0.0"
#define PROTOCOL_VERSION 21
#define BOARD_CUSTOM     4
#define IMU_UNKNOWN      0
#define MCU_UNKNOWN      0

#define TIMEOUT_MS        5000
#define HANDSHAKE_MS      1000
#define SCAN_DELAY_MS     3000  /* broadcast only, then also sweep the subnet */
#define SCAN_STEP_MS      25
#define SCAN_BATCH        8
#define SCAN_PAUSE_MS     4000
#define SCAN_MAX_HOSTS    1024
#define RECONNECT_DELAY_MS 3000
#define RECONNECT_EVERY_MS 10000

#ifndef SO_BROADCAST
#define SO_BROADCAST 0x0020
#endif

enum {
	PKT_HEARTBEAT = 0,
	PKT_HANDSHAKE = 3,
	PKT_PING_PONG = 10,
	PKT_BATTERY_LEVEL = 12,
	PKT_SENSOR_INFO = 15,
	PKT_ROTATION_DATA = 17,
	PKT_SIGNAL_STRENGTH = 19,
};

enum {
	RX_HEARTBEAT = 1,
	RX_PING_PONG = 10,
	RX_SENSOR_INFO = 15,
};

static const char HANDSHAKE_REPLY[] = "Hey OVR =D 5";

static NetStatus s_st;
static bool s_mcuHwc;
static int s_sock = -1;
static u64 s_packetNumber = 1;
static struct sockaddr_in s_server;
static u32 s_ip, s_mask, s_bcast;   /* network byte order */
static u32 s_savedIp;               /* network byte order, 0 if none */

static u64 s_lastWifiCheck, s_lastRecv, s_lastSend, s_lastHandshake, s_lastHint;
static u64 s_lastSensorInfo, s_lastBattery, s_lastSignal, s_searchStart;
static u64 s_rateStart;
static u64 s_wifiLost, s_lastReconnect;
static u32 s_rateCount;

static u32 s_scanNet, s_scanCount, s_scanPos; /* host byte order */
static u64 s_scanNext;

/* ---------------------------------------------------------------- packets */

typedef struct {
	u8 d[256];
	int n;
} Packet;

static void put_u8(Packet* p, u8 v) {
	if (p->n < (int)sizeof(p->d)) p->d[p->n++] = v;
}
static void put_u32(Packet* p, u32 v) {
	put_u8(p, v >> 24);
	put_u8(p, v >> 16);
	put_u8(p, v >> 8);
	put_u8(p, v);
}
static void put_u64(Packet* p, u64 v) {
	put_u32(p, (u32)(v >> 32));
	put_u32(p, (u32)v);
}
static void put_f32(Packet* p, float f) {
	u32 u;
	memcpy(&u, &f, 4);
	put_u32(p, u);
}
static void put_str(Packet* p, const char* s) {
	size_t l = strlen(s);
	put_u8(p, (u8)l);
	for (size_t i = 0; i < l; i++) put_u8(p, (u8)s[i]);
}
static void begin(Packet* p, u32 type, bool numbered) {
	p->n = 0;
	put_u32(p, type);
	put_u64(p, numbered ? s_packetNumber++ : 0);
}

static void send_raw(u32 ip, u16 port, const void* data, int len) {
	if (s_sock < 0) return;
	struct sockaddr_in a = {0};
	a.sin_family = AF_INET;
	a.sin_port = htons(port);
	a.sin_addr.s_addr = ip;
	if (sendto(s_sock, data, len, 0, (struct sockaddr*)&a, sizeof(a)) == len) s_rateCount++;
}

static void send_server(const Packet* p, u64 now) {
	send_raw(s_server.sin_addr.s_addr, ntohs(s_server.sin_port), p->d, p->n);
	s_lastSend = now;
}

static void send_handshake(u32 ip) {
	Packet p;
	begin(&p, PKT_HANDSHAKE, false);
	put_u32(&p, BOARD_CUSTOM);
	put_u32(&p, IMU_UNKNOWN);
	put_u32(&p, MCU_UNKNOWN);
	put_u32(&p, 0);
	put_u32(&p, 0);
	put_u32(&p, 0);
	put_u32(&p, PROTOCOL_VERSION);
	put_str(&p, FIRMWARE_NAME);
	for (int i = 0; i < 6; i++) put_u8(&p, s_st.mac[i]);
	put_u8(&p, 0);               /* tracker type: rotation */
	put_str(&p, "Homebrew");     /* vendor */
	put_str(&p, "");             /* vendor url */
	put_str(&p, "Nintendo 3DS"); /* product */
	put_str(&p, "");             /* update address */
	put_str(&p, "");             /* update name */
	send_raw(ip, SLIME_PORT, p.d, p.n);
}

/* ---------------------------------------------------------------- helpers */

static void make_mac(void) {
	u64 hash = 0;
	if (R_FAILED(CFGU_GenHashConsoleUnique(0x51E3D5, &hash)) || hash == 0)
		hash = svcGetSystemTick() * 0x9E3779B97F4A7C15ULL;
	s_st.mac[0] = 0x02; /* locally administered, unicast */
	for (int i = 1; i < 6; i++) s_st.mac[i] = (u8)(hash >> (8 * (i - 1)));
}

static void load_saved_server(void) {
	FILE* f = fopen(SERVER_FILE, "r");
	if (!f) return;
	char line[64] = {0};
	if (fgets(line, sizeof(line), f)) {
		line[strcspn(line, " \r\n")] = 0;
		struct in_addr a;
		if (inet_aton(line, &a)) s_savedIp = a.s_addr;
	}
	fclose(f);
}

static void save_server(u32 ip) {
	if (ip == s_savedIp) return;
	s_savedIp = ip;
	mkdir("sdmc:/3ds", 0777);
	mkdir(CONFIG_DIR, 0777);
	FILE* f = fopen(SERVER_FILE, "w");
	if (!f) return;
	struct in_addr a = {.s_addr = ip};
	fprintf(f, "%s\n", inet_ntoa(a));
	fclose(f);
}

static void close_socket(void) {
	if (s_sock >= 0) close(s_sock);
	s_sock = -1;
}

static bool open_socket(void) {
	close_socket();
	s_sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (s_sock < 0) return false;

	int one = 1;
	setsockopt(s_sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	setsockopt(s_sock, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one)); /* may be unsupported */

	/* Like the official firmware, listen on 6969: the server's discovery
	   broadcasts are sent to that port. */
	struct sockaddr_in a = {0};
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = INADDR_ANY;
	a.sin_port = htons(SLIME_PORT);
	if (bind(s_sock, (struct sockaddr*)&a, sizeof(a)) < 0) {
		a.sin_port = 0;
		bind(s_sock, (struct sockaddr*)&a, sizeof(a));
	}
	fcntl(s_sock, F_SETFL, fcntl(s_sock, F_GETFL, 0) | O_NONBLOCK);
	return true;
}

static void setup_scan(void) {
	u32 ip = ntohl(s_ip), mask = ntohl(s_mask);
	u32 hosts = ~mask;
	if (mask == 0 || hosts + 1 > SCAN_MAX_HOSTS) {
		mask = 0xFFFFFF00; /* huge or unknown subnet: scan our /24 only */
		hosts = 0xFF;
	}
	s_scanNet = ip & mask;
	s_scanCount = hosts; /* host ids 1 .. hosts-1 */
	s_scanPos = 1;
}

static void start_search(u64 now) {
	s_st.state = NET_SEARCHING;
	s_st.sensorAcked = false;
	s_st.scanning = false;
	s_searchStart = now;
	s_lastHandshake = 0;
	s_scanNext = now + SCAN_DELAY_MS;
	setup_scan();
}

static void read_battery(void) {
	u8 v = 0;
	if (s_mcuHwc && R_SUCCEEDED(MCUHWC_GetBatteryLevel(&v)) && v <= 100) s_st.battery = v;
	else if (R_SUCCEEDED(PTMU_GetBatteryLevel(&v))) s_st.battery = v * 20;
	else s_st.battery = -1;
	u8 c = 0;
	s_st.charging = R_SUCCEEDED(PTMU_GetBatteryChargeState(&c)) && c;
}

/* ---------------------------------------------------------------- wifi */

static void check_wifi(u64 now) {
	u32 wifi = 0;
	struct in_addr ip = {0}, mask = {0}, bcast = {0};
	bool up = R_SUCCEEDED(ACU_GetWifiStatus(&wifi)) && wifi != 0 &&
			  SOCU_GetIPInfo(&ip, &mask, &bcast) == 0 && ip.s_addr != 0;
	s_st.wifiStrength = up ? osGetWifiStrength() : 0;

	if (!up) {
		if (s_st.state != NET_NO_WIFI) {
			close_socket();
			s_st.state = NET_NO_WIFI;
			s_st.localIp = 0;
			s_wifiLost = now;
			s_lastReconnect = 0;
		}
		/* the system will not reconnect by itself while the lid is closed */
		if (now - s_wifiLost >= RECONNECT_DELAY_MS && now - s_lastReconnect >= RECONNECT_EVERY_MS) {
			s_lastReconnect = now;
			wifilock_reconnect();
		}
		return;
	}
	if (s_st.state == NET_NO_WIFI || ip.s_addr != s_ip || s_sock < 0) {
		s_ip = ip.s_addr;
		s_mask = mask.s_addr;
		s_bcast = bcast.s_addr ? bcast.s_addr : (s_ip | ~s_mask);
		s_st.localIp = s_ip;
		if (open_socket()) start_search(now);
	}
}

/* ---------------------------------------------------------------- receive */

static void on_connected(const struct sockaddr_in* from, u64 now) {
	s_server = *from;
	s_st.state = NET_CONNECTED;
	s_st.serverIp = from->sin_addr.s_addr;
	s_st.serverPort = ntohs(from->sin_port);
	s_st.sensorAcked = false;
	s_st.scanning = false;
	s_lastRecv = now;
	s_lastSensorInfo = 0;
	s_lastBattery = 0;
	s_lastSignal = 0;
	save_server(s_st.serverIp);
}

static void receive(u64 now) {
	u8 buf[512];
	for (int i = 0; i < 64; i++) {
		struct sockaddr_in from;
		socklen_t fl = sizeof(from);
		int len = recvfrom(s_sock, buf, sizeof(buf), 0, (struct sockaddr*)&from, &fl);
		if (len <= 0) break;
		if (from.sin_addr.s_addr == s_ip) continue; /* our own broadcast */

		bool isHandshake = len >= 13 && buf[0] == PKT_HANDSHAKE &&
						   memcmp(buf + 1, HANDSHAKE_REPLY, sizeof(HANDSHAKE_REPLY) - 1) == 0;

		if (s_st.state == NET_SEARCHING) {
			if (isHandshake) {
				on_connected(&from, now);
				continue;
			}
			/* Any other packet coming from a SlimeVR port (the server
			   broadcasts heartbeats while it has no tracker): answer it
			   directly with a handshake. */
			if (ntohs(from.sin_port) == SLIME_PORT && now - s_lastHint > 300) {
				s_lastHint = now;
				send_handshake(from.sin_addr.s_addr);
			}
			continue;
		}

		if (s_st.state != NET_CONNECTED) continue;
		if (from.sin_addr.s_addr != s_server.sin_addr.s_addr || from.sin_port != s_server.sin_port)
			continue;
		if (isHandshake) continue;

		s_lastRecv = now;
		if (len < 4) continue;
		u32 type = ((u32)buf[0] << 24) | ((u32)buf[1] << 16) | ((u32)buf[2] << 8) | buf[3];
		switch (type) {
		case RX_HEARTBEAT: {
			Packet p;
			begin(&p, PKT_HEARTBEAT, true);
			send_server(&p, now);
			break;
		}
		case RX_PING_PONG:
			/* echo it back untouched */
			send_raw(s_server.sin_addr.s_addr, ntohs(s_server.sin_port), buf, len);
			break;
		case RX_SENSOR_INFO:
			if (len >= 5 && buf[4] == 0) s_st.sensorAcked = true;
			break;
		default:
			break;
		}
	}
}

/* ---------------------------------------------------------------- states */

static void tick_search(u64 now) {
	s_st.searchMs = (u32)(now - s_searchStart);

	if (now - s_lastHandshake >= HANDSHAKE_MS) {
		s_lastHandshake = now;
		send_handshake(INADDR_BROADCAST);
		if (s_bcast && s_bcast != INADDR_BROADCAST) send_handshake(s_bcast);
		if (s_savedIp) send_handshake(s_savedIp);
	}

	/* Some networks / consoles drop broadcasts: fall back to asking every
	   host of the local subnet, in small batches. */
	if (now >= s_scanNext) {
		s_st.scanning = true;
		for (int i = 0; i < SCAN_BATCH && s_scanPos < s_scanCount; i++, s_scanPos++) {
			u32 host = htonl(s_scanNet + s_scanPos);
			if (host != s_ip) send_handshake(host);
		}
		if (s_scanPos >= s_scanCount) {
			s_scanPos = 1;
			s_st.scanning = false;
			s_scanNext = now + SCAN_PAUSE_MS;
		} else {
			s_scanNext = now + SCAN_STEP_MS;
		}
	}
}

static void tick_connected(u64 now, bool sendRotation, quat q) {
	if (now - s_lastRecv > TIMEOUT_MS) {
		start_search(now);
		return;
	}

	Packet p;
	if (!s_st.sensorAcked && now - s_lastSensorInfo >= 1000) {
		s_lastSensorInfo = now;
		begin(&p, PKT_SENSOR_INFO, true);
		put_u8(&p, 0);           /* sensor id */
		put_u8(&p, 1);           /* status: OK */
		put_u8(&p, IMU_UNKNOWN); /* sensor type */
		send_server(&p, now);
	}

	if (sendRotation) {
		begin(&p, PKT_ROTATION_DATA, true);
		put_u8(&p, 0); /* sensor id */
		put_u8(&p, 1); /* data type: normal */
		put_f32(&p, q.x);
		put_f32(&p, q.y);
		put_f32(&p, q.z);
		put_f32(&p, q.w);
		put_u8(&p, 0); /* accuracy */
		send_server(&p, now);
	} else if (now - s_lastSend > 250) {
		begin(&p, PKT_HEARTBEAT, true);
		send_server(&p, now);
	}

	if (now - s_lastBattery >= 10000 || s_lastBattery == 0) {
		s_lastBattery = now;
		if (s_st.battery >= 0) {
			begin(&p, PKT_BATTERY_LEVEL, true);
			put_f32(&p, s_st.battery / 100.0f);
			send_server(&p, now);
		}
	}

	if (now - s_lastSignal >= 5000 || s_lastSignal == 0) {
		s_lastSignal = now;
		static const s8 rssi[4] = {-95, -80, -67, -55};
		begin(&p, PKT_SIGNAL_STRENGTH, true);
		put_u8(&p, 255);
		put_u8(&p, (u8)rssi[s_st.wifiStrength & 3]);
		send_server(&p, now);
	}
}

/* ---------------------------------------------------------------- api */

void net_init(bool haveMcuHwc) {
	memset(&s_st, 0, sizeof(s_st));
	s_mcuHwc = haveMcuHwc;
	s_st.state = NET_NO_WIFI;
	s_st.battery = -1;
	make_mac();
	load_saved_server();
	read_battery();
}

void net_exit(void) {
	close_socket();
}

void net_tick(u64 now, bool sendRotation, quat q) {
	if (now - s_lastWifiCheck >= 1000 || s_lastWifiCheck == 0) {
		s_lastWifiCheck = now;
		check_wifi(now);
		static u64 lastBatteryRead;
		if (now - lastBatteryRead >= 5000) {
			lastBatteryRead = now;
			read_battery();
		}
	}

	if (now - s_rateStart >= 1000) {
		s_st.sentPerSec = s_rateCount * 1000 / (u32)(now - s_rateStart);
		s_rateCount = 0;
		s_rateStart = now;
	}

	if (s_st.state == NET_NO_WIFI || s_sock < 0) return;

	receive(now);
	if (s_st.state == NET_SEARCHING) tick_search(now);
	else if (s_st.state == NET_CONNECTED) tick_connected(now, sendRotation, q);
}

const NetStatus* net_status(void) {
	return &s_st;
}
