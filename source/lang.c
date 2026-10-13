#include "lang.h"

#include <3ds.h>

static const char* const EN[S_COUNT] = {
	[S_FOR_3DS] = "for 3DS",
	[S_PILL_CONNECTED] = "Connected",
	[S_PILL_SEARCHING] = "Searching",
	[S_PILL_NO_WIFI] = "No Wi-Fi",
	[S_ATTITUDE_FMT] = "Pitch %+4.0f°   Roll %+4.0f°   Yaw %+4.0f°",
	[S_SENSORS_UNAVAILABLE] = "Motion sensors unavailable",
	[S_CALIBRATING] = "Calibrating: put the console down and keep it still",
	[S_LID_HINT] = "You can close the console, tracking keeps going",
	[S_TITLE] = "SlimeVR Tracker",
	[S_SUB_TRACKING] = "Tracking",
	[S_SUB_REGISTERING] = "Registering sensor...",
	[S_SUB_SEARCHING] = "Looking for the SlimeVR server",
	[S_SUB_NO_WIFI] = "Turn on the console's Wi-Fi",
	[S_NETWORK] = "NETWORK",
	[S_WIFI] = "Wi-Fi",
	[S_CONNECTED] = "Connected",
	[S_LOCKED] = "locked",
	[S_NOT_LOCKED] = "not locked",
	[S_NOT_CONNECTED] = "Not connected",
	[S_CONSOLE] = "Console",
	[S_SERVER] = "Server",
	[S_SEARCHING_FMT] = "Searching%s %s",
	[S_SCANNING] = "(network scan)",
	[S_BROADCAST] = "(broadcast)",
	[S_SENDING] = "Sending",
	[S_SENDING_FMT] = "%lu packets/s  ·  IMU %s",
	[S_IMU_REGISTERED] = "registered",
	[S_IMU_PENDING] = "pending",
	[S_ACCEPT_HINT] = "If SlimeVR asks, accept the new tracker.",
	[S_GYROSCOPE] = "GYROSCOPE",
	[S_CALIB] = "Calib.",
	[S_ERROR] = "Error",
	[S_STATE] = "State",
	[S_AT_REST] = "At rest",
	[S_MOVING] = "Moving",
	[S_SPEED] = "Speed",
	[S_FOOTER] = " Recenter view    SELECT Screens    START Exit",
};

static const char* const FR[S_COUNT] = {
	[S_FOR_3DS] = "pour 3DS",
	[S_PILL_CONNECTED] = "Connecté",
	[S_PILL_SEARCHING] = "Recherche",
	[S_PILL_NO_WIFI] = "Pas de Wi-Fi",
	[S_ATTITUDE_FMT] = "Tangage %+4.0f°   Roulis %+4.0f°   Cap %+4.0f°",
	[S_SENSORS_UNAVAILABLE] = "Capteurs indisponibles",
	[S_CALIBRATING] = "Calibration : posez la console sans la bouger",
	[S_LID_HINT] = "Vous pouvez fermer la console, le suivi continue",
	[S_TITLE] = "Tracker SlimeVR",
	[S_SUB_TRACKING] = "Suivi actif",
	[S_SUB_REGISTERING] = "Enregistrement du capteur...",
	[S_SUB_SEARCHING] = "Recherche du serveur SlimeVR",
	[S_SUB_NO_WIFI] = "Activez le Wi-Fi de la console",
	[S_NETWORK] = "RÉSEAU",
	[S_CONNECTED] = "Connecté",
	[S_LOCKED] = "verrouillé",
	[S_NOT_LOCKED] = "non verrouillé",
	[S_NOT_CONNECTED] = "Non connecté",
	[S_SERVER] = "Serveur",
	[S_SEARCHING_FMT] = "Recherche%s %s",
	[S_SCANNING] = "(scan du réseau)",
	[S_SENDING] = "Envoi",
	[S_SENDING_FMT] = "%lu paquets/s  ·  IMU %s",
	[S_IMU_REGISTERED] = "enregistrée",
	[S_IMU_PENDING] = "en attente",
	[S_ACCEPT_HINT] = "Si SlimeVR le demande, acceptez le nouveau tracker.",
	[S_ERROR] = "Erreur",
	[S_STATE] = "État",
	[S_AT_REST] = "Au repos",
	[S_MOVING] = "En mouvement",
	[S_SPEED] = "Vitesse",
	[S_FOOTER] = " Recentrer la vue    SELECT Écrans    START Quitter",
};

static const char* const* s_table = EN;

void lang_init(void) {
	u8 lang = CFG_LANGUAGE_EN;
	if (R_SUCCEEDED(CFGU_GetSystemLanguage(&lang)) && lang == CFG_LANGUAGE_FR) s_table = FR;
}

/* Missing translations fall back to English. */
const char* tr(StrId id) {
	return s_table[id] ? s_table[id] : EN[id];
}
