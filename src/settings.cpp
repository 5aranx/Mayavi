#include "settings.h"

#include <Preferences.h>

#include "config.h"

static Settings s_cfg;
static Preferences s_prefs;

static void copyStr(char *dst, size_t cap, const char *src) {
  if (cap == 0) return;
  if (!src) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, cap - 1);
  dst[cap - 1] = '\0';
}

// Preferences::getString(key, default) still logs an NVS error when the key is
// absent, so check first to keep the boot log clean.
static String getStr(const char *key, const char *dflt) {
  return s_prefs.isKey(key) ? s_prefs.getString(key) : String(dflt);
}

namespace settings {

void resetApName() {
  const uint16_t tail = (uint16_t)(ESP.getEfuseMac() & 0xFFFF);
  snprintf(s_cfg.apSsid, sizeof(s_cfg.apSsid), "%s-%04X", AP_SSID_PREFIX, tail);
  copyStr(s_cfg.apPass, sizeof(s_cfg.apPass), AP_PASSWORD_DFLT);
}

void begin() {
  s_prefs.begin(NVS_NAMESPACE, false);

  memset(&s_cfg, 0, sizeof(s_cfg));
  resetApName();

  copyStr(s_cfg.bleName, sizeof(s_cfg.bleName),
          getStr("name", "Mayavi").c_str());
  s_cfg.appearance   = s_prefs.getUShort("appearance", 0x03C1);
  s_cfg.mode         = s_prefs.getUChar("advmode", ADV_SWIFT_PAIR);
  s_cfg.profile      = s_prefs.getUChar("profile", 0);
  s_cfg.discoverySec = s_prefs.getUShort("disc", DISCOVERY_DEF_SEC);

  if (s_cfg.discoverySec < DISCOVERY_MIN_SEC) s_cfg.discoverySec = DISCOVERY_MIN_SEC;
  if (s_cfg.discoverySec > DISCOVERY_MAX_SEC) s_cfg.discoverySec = DISCOVERY_MAX_SEC;

  // AP credentials may have been customised from the Settings tab.
  const String ap = s_prefs.isKey("apssid") ? s_prefs.getString("apssid") : String();
  if (ap.length()) copyStr(s_cfg.apSsid, sizeof(s_cfg.apSsid), ap.c_str());
  const String pw = s_prefs.isKey("appass") ? s_prefs.getString("appass") : String();
  if (pw.length()) copyStr(s_cfg.apPass, sizeof(s_cfg.apPass), pw.c_str());

  // An empty password means an open network; the flag distinguishes "open"
  // from "never configured" (which keeps the factory default).
  if (s_prefs.getUChar("apopen", 0)) s_cfg.apPass[0] = '\0';
}

Settings &get() { return s_cfg; }

void save() {
  s_prefs.putString("name", s_cfg.bleName);
  s_prefs.putUShort("appearance", s_cfg.appearance);
  s_prefs.putUChar("advmode", s_cfg.mode);
  s_prefs.putUChar("profile", s_cfg.profile);
  s_prefs.putUShort("disc", s_cfg.discoverySec);
  s_prefs.putString("apssid", s_cfg.apSsid);
  s_prefs.putString("appass", s_cfg.apPass);
  s_prefs.putUChar("apopen", s_cfg.apPass[0] ? 0 : 1);
}

}  // namespace settings
