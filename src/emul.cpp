#include "emul.h"

#include <BLE2902.h>
#include <BLEAdvertising.h>
#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#include <BLESecurity.h>
#include <esp_gap_ble_api.h>

#include "config.h"
#include "hid.h"

// ---------------------------------------------------------------------------
// Profiles
// ---------------------------------------------------------------------------
static const ProfileDef kProfiles[] = {
    {"keyboard",    "Wireless Keyboard", 0x03C1, "Wireless Keyboard", "keyboard"},
    {"mouse",       "Wireless Mouse",    0x03C2, "Wireless Mouse",    "mouse"},
    {"gamepad",     "Wireless Gamepad",  0x03C4, "Wireless Gamepad",  "gamepad"},
    {"joystick",    "Joystick",          0x03C3, "Wireless Joystick", "gamepad"},
    {"pen",         "Digital Pen",       0x03C7, "Digital Pen",       "keyboard"},
    {"android-tv",  "Android TV",        0x0280, "Android TV",        "keyboard"},
    {"apple-tv",    "Apple TV",          0x0280, "Apple TV",          "keyboard"},
    {"airtag",      "AirTag",            0x0200, "AirTag",            "tracker"},
    {"jiotag",      "JioTag",            0x0200, "JioTag",            "tracker"},
    {"hrm",         "Heart Rate",        0x0341, "HR Sensor",         "sensor"},
    {"custom",      "Custom",            0x03C0, "Mayavi",           "keyboard"},
};
static const int kProfileCount = sizeof(kProfiles) / sizeof(kProfiles[0]);

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static BLEServer *s_server = nullptr;
static BLEHIDDevice *s_hid = nullptr;

static bool s_adv = false;
static bool s_conn = false;
static bool s_secure = false;
static bool s_shouldAdv = false;
static uint32_t s_advUntil = 0;    // 0 = no auto stop
static uint32_t s_restartAt = 0;
static uint16_t s_connId = 0;

// ---------------------------------------------------------------------------
// Advertising payload
// ---------------------------------------------------------------------------
static void advAdd(BLEAdvertisementData &d, uint8_t type, const uint8_t *v, size_t n) {
  if (n > 29) n = 29;  // one AD element cannot exceed 31 bytes with header
  std::string s;
  s.reserve(n + 2);
  s.push_back((char)(uint8_t)(n + 1));
  s.push_back((char)type);
  s.append((const char *)v, n);
  d.addData(s);
}

static uint16_t appearanceNow() {
  const Settings &c = settings::get();
  const ProfileDef *p = emul::profile(c.profile);
  if (!p) return c.appearance;
  if (strcmp(p->id, "custom") == 0) return c.appearance;
  return p->appearance;
}

static void applyAdvertising() {
  Settings &c = settings::get();
  BLEAdvertising *adv = BLEDevice::getAdvertising();

  if (s_adv) {
    adv->stop();
    delay(20);
  }

  BLEAdvertisementData data;
  BLEAdvertisementData rsp;

  // Flags: LE General Discoverable, BR/EDR not supported.
  const uint8_t flags = 0x06;
  advAdd(data, 0x01, &flags, 1);

  // Appearance drives the icon Windows/macOS draw during discovery.
  data.setAppearance(appearanceNow());

  if (c.mode == ADV_SWIFT_PAIR) {
    // Microsoft Swift Pair: company ID 0x0006, beacon type 0x03,
    // payload type 0x01 ("device name") followed by the name itself.
    uint8_t mfr[4 + 18];
    mfr[0] = 0x06;
    mfr[1] = 0x00;
    mfr[2] = 0x03;
    mfr[3] = 0x01;
    size_t n = strlen(c.bleName);
    if (n > 18) n = 18;  // 31-byte advertisement budget
    memcpy(&mfr[4], c.bleName, n);
    advAdd(data, 0xFF, mfr, 4 + n);
  } else {
    // Plain beacon: name in the advertisement, truncated to what fits.
    size_t n = strlen(c.bleName);
    if (n > 21) n = 21;
    advAdd(data, 0x09, (const uint8_t *)c.bleName, n);
  }

  // Full name always in the scan response.
  advAdd(rsp, 0x09, (const uint8_t *)c.bleName, strlen(c.bleName));

  adv->setScanResponse(true);
  adv->setAdvertisementData(data);
  adv->setScanResponseData(rsp);
  adv->setMinInterval(0x20);
  adv->setMaxInterval(0x30);
  adv->start();

  s_adv = true;
}

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server) override {
    s_conn = true;
    s_secure = false;
    s_adv = false;
    s_connId = server->getConnId();
    hid::setConnected(true);
    Serial.println("[ble] host connected, negotiating security");
  }

  void onDisconnect(BLEServer *server) override {
    (void)server;
    s_conn = false;
    s_secure = false;
    s_adv = false;
    hid::setConnected(false);
    Serial.println("[ble] host disconnected");
    if (s_shouldAdv) s_restartAt = millis() + 500;
  }
};

class SecurityCallbacks : public BLESecurityCallbacks {
  uint32_t onPassKeyRequest() override { return 123456; }
  void onPassKeyNotify(uint32_t passKey) override { (void)passKey; }
  bool onSecurityRequest() override { return true; }
  bool onConfirmPIN(uint32_t pin) override { (void)pin; return true; }

  void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) override {
    s_secure = cmpl.success;
    if (cmpl.success) {
      s_shouldAdv = false;
      Serial.println("[ble] paired and encrypted -- HID ready");
    } else {
      Serial.println("[ble] pairing failed");
    }
  }
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
namespace emul {

void begin() {
  const Settings &c = settings::get();

  BLEDevice::init(c.bleName);
  BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);

  s_server = BLEDevice::createServer();
  s_server->setCallbacks(new ServerCallbacks());

  s_hid = new BLEHIDDevice(s_server);
  // The manufacturer characteristic is created lazily by the getter; calling
  // the setter first dereferences a null pointer (core lib bug, not ours).
  s_hid->manufacturer();
  s_hid->manufacturer("Mayavi");
  s_hid->pnp(0x02, 0x02E5, 0x0001, 0x0100);
  s_hid->hidInfo(0x00, 0x01);
  s_hid->reportMap((uint8_t *)hid::kReportMap, hid::kReportMapLen);

  hid::attach(s_hid);
  s_hid->startServices();

  // Must come after startServices(): setBatteryLevel() notifies, and a
  // characteristic has no service handle until the service is started.
  s_hid->setBatteryLevel(100);

  BLESecurity *sec = new BLESecurity();
  sec->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
  sec->setCapability(ESP_IO_CAP_NONE);  // Just Works, no passkey
  sec->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  sec->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  BLEDevice::setSecurityCallbacks(new SecurityCallbacks());

  Serial.printf("[ble] HID device up as \"%s\"\n", c.bleName);
}

void loop() {
  const uint32_t now = millis();

  if (s_restartAt && now >= s_restartAt) {
    s_restartAt = 0;
    if (s_shouldAdv && !s_conn) {
      applyAdvertising();
      Serial.println("[ble] advertising again");
    }
  }

  if (s_shouldAdv && !s_conn && s_advUntil && now > s_advUntil) {
    Serial.println("[ble] discovery window closed");
    stop();
  }
}

bool start(uint32_t seconds) {
  if (s_conn) return false;

  if (seconds == 0) seconds = settings::get().discoverySec;
  if (seconds < DISCOVERY_MIN_SEC) seconds = DISCOVERY_MIN_SEC;
  if (seconds > DISCOVERY_MAX_SEC) seconds = DISCOVERY_MAX_SEC;

  s_shouldAdv = true;
  s_advUntil = millis() + seconds * 1000;

  applyAdvertising();
  Serial.printf("[ble] advertising \"%s\" for %lu s\n", settings::get().bleName,
                (unsigned long)seconds);
  return true;
}

void stop() {
  s_shouldAdv = false;
  s_advUntil = 0;
  s_restartAt = 0;

  if (s_adv) {
    BLEDevice::getAdvertising()->stop();
    s_adv = false;
  }
  if (s_conn && s_server) {
    s_server->disconnect(s_connId);
  }
  Serial.println("[ble] stopped");
}

bool advertising() { return s_adv; }
bool connected() { return s_conn; }
bool ready() { return s_conn && s_secure; }

int bonds() {
  int n = esp_ble_get_bond_device_num();
  return n < 0 ? 0 : n;
}

uint32_t secondsLeft() {
  if (!s_advUntil || !s_adv) return 0;
  const int32_t left = (int32_t)(s_advUntil - millis());
  return left > 0 ? (uint32_t)(left / 1000) : 0;
}

int profileCount() { return kProfileCount; }

const ProfileDef *profile(int i) {
  if (i < 0 || i >= kProfileCount) return nullptr;
  return &kProfiles[i];
}

int currentProfile() {
  const int i = settings::get().profile;
  return (i >= 0 && i < kProfileCount) ? i : 0;
}

String currentProfileId() {
  const ProfileDef *p = profile(currentProfile());
  return p ? String(p->id) : String("custom");
}

bool setProfile(const String &id) {
  for (int i = 0; i < kProfileCount; i++) {
    if (id.equalsIgnoreCase(kProfiles[i].id)) {
      Settings &c = settings::get();
      c.profile = (uint8_t)i;
      // The profile brings its own identity with it.
      strncpy(c.bleName, kProfiles[i].name, sizeof(c.bleName) - 1);
      c.bleName[sizeof(c.bleName) - 1] = '\0';
      settings::save();
      if (s_adv) applyAdvertising();
      return true;
    }
  }
  return false;
}

void setName(const char *name) {
  Settings &c = settings::get();
  strncpy(c.bleName, name, sizeof(c.bleName) - 1);
  c.bleName[sizeof(c.bleName) - 1] = '\0';
  settings::save();
  if (s_adv) applyAdvertising();
}

void setMode(uint8_t mode) {
  Settings &c = settings::get();
  c.mode = (mode == ADV_BEACON) ? ADV_BEACON : ADV_SWIFT_PAIR;
  settings::save();
  if (s_adv) applyAdvertising();
}

void setAppearance(uint16_t appearance) {
  Settings &c = settings::get();
  c.appearance = appearance;
  // Raw appearance implies the custom profile.
  c.profile = (uint8_t)(kProfileCount - 1);
  settings::save();
  if (s_adv) applyAdvertising();
}

void forgetBonds() {
  int n = esp_ble_get_bond_device_num();
  if (n <= 0) {
    Serial.println("[ble] no stored bonds");
    return;
  }
  if (n > 16) n = 16;

  esp_ble_bond_dev_t list[16];
  if (esp_ble_get_bond_device_list(&n, list) == ESP_OK) {
    for (int i = 0; i < n; i++) {
      esp_ble_remove_bond_device(list[i].bd_addr);
    }
    Serial.printf("[ble] cleared %d bond(s)\n", n);
  }
}

const char *stateText() {
  if (s_conn && s_secure) return "ready";
  if (s_conn) return "connected";
  if (s_adv) return "advertising";
  return "idle";
}

}  // namespace emul
