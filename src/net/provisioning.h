// Wi-Fi + MQTT setup portal (WiFiManager) and the BOOT long-press that reopens it.
// The decision logic above the ARDUINO guard is pure C++ so native tests cover it.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "settings.h"

namespace olvar {

constexpr uint32_t kPortalTimeoutMs = 5UL * 60UL * 1000UL;
constexpr uint32_t kLongPressMs = 3000;
constexpr uint32_t kButtonDebounceMs = 30;

constexpr size_t kApSsidSize = 19;
void apSsid(const uint8_t mac[6], char out[kApSsidSize]);

// Look-alikes (0/O, 1/l/I) are left out because the password is typed from the serial log.
constexpr size_t kApPasswordSize = 11;
void apPasswordFromRandom(uint32_t r0, uint32_t r1, char out[kApPasswordSize]);

bool shouldOpenPortal(bool hasWifiCredentials, bool portalFlag, bool settingsValid);

// Raw text of the portal fields. An empty password keeps the current one, so the stored
// password is never sent back to the browser.
struct PortalForm {
  const char* device_id;
  const char* mqtt_host;
  const char* mqtt_port;
  const char* mqtt_pass;
};

// Trims the text fields and lowercases device_id (phone keyboards capitalise the first letter),
// then validates. On failure *err names the rule and `out` is unspecified.
bool settingsFromForm(const PortalForm& form, const Settings& current, Settings& out, const char** err);

enum class PortalStep {
  Wait,
  Save,        // form valid and Wi-Fi known: save, clear the flag, restart
  WifiFailed,  // a network was submitted but joining it failed
  NeedWifi,    // form valid but no network chosen and none saved
  Reopen,      // joined the new network with an invalid form; WiFiManager has closed the AP
};
PortalStep portalStep(bool formValid, bool ssidSubmitted, bool joinedNewWifi, bool hasWifiCredentials);

// What the save failure leaves behind: once a new network is joined WiFiManager has already
// closed the AP, so the only way back to the form is a restart into the portal.
PortalStep afterSaveFailed(bool joinedNewWifi);

// WiFiManager pastes field values unescaped into value='...'. A value is shown again only if it
// contains none of ' " < > &, else `fallback` (if that is safe), else "".
bool safeForHtmlAttr(const char* s);
const char* redisplayValue(const char* submitted, const char* fallback);

// Idle time counts from the last moment a phone was associated with the AP.
bool portalExpired(uint32_t lastActivityMs, uint32_t nowMs);

// After a timeout the board restarts; it boots normally only if it can actually run.
bool bootNormallyAfterTimeout(bool hasWifiCredentials, const Settings& current);

// Debounced BOOT-button detector. Fires once on release after a hold of at least kLongPressMs.
class LongPressDetector {
 public:
  bool update(bool pressed, uint32_t nowMs);

 private:
  bool started_ = false;
  bool armed_ = false;
  bool raw_ = false;
  bool stable_ = false;
  uint32_t rawSinceMs_ = 0;
  uint32_t pressedAtMs_ = 0;
};

}  // namespace olvar

#ifdef ARDUINO
#include <WiFiManager.h>

#include "../storage/settings_store.h"

namespace olvar {

bool hasWifiCredentials();

// Polls GPIO0 from loop() only; never at reset, where a held BOOT selects the ROM bootloader.
class BootButton {
 public:
  void begin();
  bool poll();

 private:
  LongPressDetector detector_;
};

class Provisioning {
 public:
  explicit Provisioning(SettingsStore& store) : store_(store) {}

  bool needed(const Settings& current);
  // Restarts after a save or a timeout.
  [[noreturn]] void run(const Settings& current);

 private:
  void onParamsSaved();
  void resetFields(const Settings& shown);

  SettingsStore& store_;
  WiFiManager wm_;
  Settings current_;
  Settings pending_;
  bool formValid_ = false;
  bool ssidSubmitted_ = false;
  char banner_[192] = {};
  char portText_[6] = {};
  WiFiManagerParameter* devId_ = nullptr;
  WiFiManagerParameter* host_ = nullptr;
  WiFiManagerParameter* port_ = nullptr;
  WiFiManagerParameter* pass_ = nullptr;
};

}  // namespace olvar
#endif  // ARDUINO
