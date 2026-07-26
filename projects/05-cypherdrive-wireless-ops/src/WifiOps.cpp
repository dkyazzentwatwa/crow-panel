#include "WifiOps.h"
#include <CrowPanelShared.h>
#include <ctype.h>

#if USE_WIFI_ACTIVE
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#endif

namespace {
uint8_t boundedCount(uint8_t requested, uint8_t available) {
  return requested < available ? requested : available;
}

// Small common-port set for the connect sweep, with best-guess labels.
struct PortName { uint16_t port; const char *service; };
const PortName kCommonPorts[] = {
    {22, "ssh"},   {53, "dns"},    {80, "http"},   {139, "smb"},
    {443, "https"},{445, "smb"},   {554, "rtsp"},  {1883, "mqtt"},
    {8080, "http-alt"},{8443, "https-alt"},
};
const uint8_t kCommonPortCount = sizeof(kCommonPorts) / sizeof(kCommonPorts[0]);

#if USE_WIFI_ACTIVE
const char *authName(wifi_auth_mode_t mode) {
  switch (mode) {
    case WIFI_AUTH_OPEN: return "open";
    case WIFI_AUTH_WEP: return "wep";
    case WIFI_AUTH_WPA_PSK: return "wpa";
    case WIFI_AUTH_WPA2_PSK: return "wpa2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "wpa/wpa2";
#ifdef WIFI_AUTH_WPA2_ENTERPRISE
    case WIFI_AUTH_WPA2_ENTERPRISE: return "wpa2-enterprise";
#endif
#ifdef WIFI_AUTH_WPA3_PSK
    case WIFI_AUTH_WPA3_PSK: return "wpa3";
#endif
#ifdef WIFI_AUTH_WPA2_WPA3_PSK
    case WIFI_AUTH_WPA2_WPA3_PSK: return "wpa2/wpa3";
#endif
    default: return "unknown";
  }
}
#endif
}  // namespace

void WifiOps::begin() {
#if USE_WIFI_ACTIVE
  configureCrowPanelHostedWiFiPins("wifi-active");
  WiFi.mode(WIFI_STA);
  Logger::info("wifi-active", "USE_WIFI_ACTIVE=1; active scan + join through hosted C6");
#else
  Logger::info("wifi-active", "mock active-Wi-Fi path enabled");
#endif
}

uint8_t WifiOps::scan(WifiNetworkRecord records[], uint8_t maxRecords, Stream &out) {
  if (maxRecords == 0) return 0;

#if USE_WIFI_ACTIVE
  configureCrowPanelHostedWiFiPins("wifi-active");
  WiFi.mode(WIFI_STA);
  // Active scan: async=false, show_hidden=true, passive=FALSE (broadcast probe
  // requests). This is the recon difference from the old passive-only path.
  int found = WiFi.scanNetworks(false, true, false, CYPHERDRIVE_WIFI_SCAN_MS_PER_CHANNEL);
  if (found < 0) {
    out.print(F("[scan:wifi] hosted active scan failed code="));
    out.println(found);
    WiFi.scanDelete();
    return 0;
  }
  uint8_t available = found > 255 ? 255 : (uint8_t)found;
  uint8_t count = boundedCount(maxRecords, available);
  for (uint8_t i = 0; i < count; ++i) {
    records[i].ssid = WiFi.SSID(i);
    records[i].hidden = records[i].ssid.length() == 0;
    if (records[i].hidden) records[i].ssid = "(hidden)";
    records[i].bssid = WiFi.BSSIDstr(i);
    records[i].rssi = WiFi.RSSI(i);
    records[i].channel = (uint8_t)WiFi.channel(i);
    records[i].auth = authName(WiFi.encryptionType(i));
    // Richer detail from the raw AP record (best-effort; guarded).
    const wifi_ap_record_t *rec =
        reinterpret_cast<const wifi_ap_record_t *>(WiFi.getScanInfoByIndex(i));
    if (rec != nullptr) {
      records[i].wps = rec->wps;
      if (rec->phy_11ax) records[i].phy = "ax";
      else if (rec->phy_11n) records[i].phy = "n";
      else if (rec->phy_11g) records[i].phy = "g";
      else if (rec->phy_11b) records[i].phy = "b";
      else records[i].phy = "?";
    }

    out.print(F("[scan:wifi] "));
    out.print(records[i].ssid);
    out.print(F(" bssid="));
    out.print(records[i].bssid);
    out.print(F(" ch="));
    out.print(records[i].channel);
    out.print(F(" rssi="));
    out.print(records[i].rssi);
    out.print(F(" auth="));
    out.print(records[i].auth);
    out.println(F(" source=hosted-c6-active"));
  }
  WiFi.scanDelete();
  return count;
#else
  // Believable mock spread: 2.4 + 5 GHz, mixed security, one hidden SSID, so
  // every screen fills with no radio activity at all. BSSIDs are stable fakes.
  struct MockNet {
    const char *ssid; const char *bssid; uint8_t channel; int32_t rssi;
    const char *auth; bool hidden;
  };
  static const MockNet kMock[] = {
      {"StudioNet", "a4:2b:b0:11:22:01", 6, -41, "wpa2", false},
      {"StudioNet-5G", "a4:2b:b0:11:22:02", 44, -55, "wpa3", false},
      {"GuestLab", "de:ad:be:ef:00:11", 11, -63, "open", false},
      {"PanelBench", "b8:27:eb:aa:bb:cc", 1, -72, "wpa2", false},
      {"CafeMesh", "f0:9f:c2:33:44:55", 36, -68, "wpa2/wpa3", false},
      {"(hidden)", "00:11:22:33:44:55", 149, -77, "wpa2", true},
      {"IoT-Legacy", "5c:cf:7f:12:34:56", 3, -81, "wep", false},
      {"NeighborNet", "e4:f0:42:99:88:77", 9, -88, "wpa", false},
  };
  const uint8_t available = sizeof(kMock) / sizeof(kMock[0]);
  uint8_t count = boundedCount(maxRecords, available);
  for (uint8_t i = 0; i < count; ++i) {
    records[i].ssid = kMock[i].ssid;
    records[i].bssid = kMock[i].bssid;
    records[i].channel = kMock[i].channel;
    records[i].rssi = kMock[i].rssi;
    records[i].auth = kMock[i].auth;
    records[i].hidden = kMock[i].hidden;
    records[i].phy = kMock[i].channel >= 32 ? "ax" : "n";
    records[i].wps = (i % 3 == 0);
    out.print(F("[scan:wifi] "));
    out.print(records[i].ssid);
    out.print(F(" bssid="));
    out.print(records[i].bssid);
    out.print(F(" ch="));
    out.print(records[i].channel);
    out.print(F(" rssi="));
    out.print(records[i].rssi);
    out.print(F(" auth="));
    out.print(records[i].auth);
    out.println(F(" source=mock"));
  }
  return count;
#endif
}

const char *WifiOps::driverName() const {
#if USE_WIFI_ACTIVE
  return "hosted-c6-active";
#else
  return "mock";
#endif
}

bool WifiOps::hardwareEnabled() const { return USE_WIFI_ACTIVE == 1; }

bool WifiOps::join(const String &ssid, const char *password, Stream &out) {
  if (ssid.length() == 0) {
    out.println(F("[join] empty ssid"));
    return false;
  }
  link_.ssid = ssid;
  link_.state = WLINK_JOINING;
  link_.ip = "";
  link_.gateway = "";
  const bool hasPass = password != nullptr && password[0] != '\0';
  out.print(F("[join] "));
  out.print(ssid);
  out.println(hasPass ? F(" (psk)") : F(" (open)"));
#if USE_WIFI_ACTIVE
  configureCrowPanelHostedWiFiPins("wifi-active");
  WiFi.mode(WIFI_STA);
  if (hasPass) {
    WiFi.begin(ssid.c_str(), password);
  } else {
    WiFi.begin(ssid.c_str());
  }
  return true;
#else
  // Mock: associate instantly to a believable RFC1918 lease.
  link_.state = WLINK_CONNECTED;
  link_.ip = "192.168.4.42";
  link_.gateway = "192.168.4.1";
  link_.rssi = -47;
  out.println(F("[join] mock associated 192.168.4.42"));
  return true;
#endif
}

void WifiOps::leave(Stream &out) {
  out.println(F("[join] leaving"));
  link_ = WifiLinkStatus();
#if USE_WIFI_ACTIVE
  WiFi.disconnect(false, false);
#endif
}

void WifiOps::maintain() {
#if USE_WIFI_ACTIVE
  if (link_.state != WLINK_JOINING && link_.state != WLINK_CONNECTED) return;
  wl_status_t s = WiFi.status();
  if (s == WL_CONNECTED) {
    if (link_.state != WLINK_CONNECTED) link_.state = WLINK_CONNECTED;
    link_.ip = WiFi.localIP().toString();
    link_.gateway = WiFi.gatewayIP().toString();
    link_.rssi = WiFi.RSSI();
  } else if (s == WL_CONNECT_FAILED || s == WL_NO_SSID_AVAIL || s == WL_CONNECTION_LOST) {
    link_.state = WLINK_FAILED;
  }
#endif
}

CaptivePortalResult WifiOps::checkCaptivePortal(Stream &out) {
#if USE_WIFI_ACTIVE
  if (link_.state != WLINK_CONNECTED) {
    out.println(F("[captive] no link"));
    return CAPTIVE_OFFLINE;
  }
  WiFiClient client;
  HTTPClient http;
  // A known 204 endpoint: a clean internet answers 204 No Content; a portal
  // rewrites/redirects it. We read only the status, never post anything.
  if (!http.begin(client, "http://connectivitycheck.gstatic.com/generate_204")) {
    out.println(F("[captive] probe begin failed"));
    return CAPTIVE_UNKNOWN;
  }
  http.setConnectTimeout(4000);
  int code = http.GET();
  http.end();
  out.print(F("[captive] probe http="));
  out.println(code);
  if (code == 204) return CAPTIVE_CLEAR;
  if (code > 0) return CAPTIVE_PORTAL;   // 200/redirect => a portal intercepts
  return CAPTIVE_UNKNOWN;
#else
  (void)out;
  out.println(F("[captive] mock: clear (204)"));
  return CAPTIVE_CLEAR;
#endif
}

uint8_t WifiOps::discoverServices(ServiceRecord out_[], uint8_t maxRecords, Stream &out) {
  if (maxRecords == 0) return 0;
#if USE_WIFI_ACTIVE
  if (link_.state != WLINK_CONNECTED) {
    out.println(F("[mdns] no link"));
    return 0;
  }
  // Browse a couple of common service types. MDNS.begin is idempotent enough
  // for a repeated browse from a field tool.
  MDNS.begin("cypherdrive");
  static const char *kTypes[] = {"http", "workstation", "raop", "ipp"};
  uint8_t count = 0;
  for (uint8_t t = 0; t < 4 && count < maxRecords; ++t) {
    int n = MDNS.queryService(kTypes[t], "tcp");
    for (int i = 0; i < n && count < maxRecords; ++i) {
      out_[count].name = MDNS.hostname(i);
      out_[count].type = String("_") + kTypes[t] + "._tcp";
      out_[count].ip = MDNS.address(i).toString();
      out_[count].host = MDNS.hostname(i);
      out_[count].port = MDNS.port(i);
      out.print(F("[mdns] "));
      out.print(out_[count].name);
      out.print(F(" "));
      out.print(out_[count].type);
      out.print(F(" "));
      out.print(out_[count].ip);
      out.print(F(":"));
      out.println(out_[count].port);
      ++count;
    }
  }
  if (count == 0) out.println(F("[mdns] no services found"));
  return count;
#else
  struct MockSvc { const char *name; const char *type; const char *ip; uint16_t port; };
  static const MockSvc kMock[] = {
      {"studio-nas", "_http._tcp", "192.168.4.10", 80},
      {"officeprinter", "_ipp._tcp", "192.168.4.23", 631},
      {"living-speaker", "_raop._tcp", "192.168.4.31", 7000},
      {"bench-pi", "_workstation._tcp", "192.168.4.44", 9},
  };
  const uint8_t available = sizeof(kMock) / sizeof(kMock[0]);
  uint8_t count = boundedCount(maxRecords, available);
  for (uint8_t i = 0; i < count; ++i) {
    out_[i].name = kMock[i].name;
    out_[i].type = kMock[i].type;
    out_[i].ip = kMock[i].ip;
    out_[i].host = kMock[i].name;
    out_[i].port = kMock[i].port;
    out.print(F("[mdns] "));
    out.print(out_[i].name);
    out.print(F(" "));
    out.print(out_[i].type);
    out.print(F(" "));
    out.print(out_[i].ip);
    out.print(F(":"));
    out.println(out_[i].port);
  }
  return count;
#endif
}

uint8_t WifiOps::portScan(const String &target, PortResult out_[], uint8_t maxRecords,
                          Stream &out) {
  if (maxRecords == 0) return 0;
  uint8_t count = boundedCount(maxRecords, kCommonPortCount);
#if USE_WIFI_ACTIVE
  if (link_.state != WLINK_CONNECTED) {
    out.println(F("[portscan] no link"));
    return 0;
  }
  String host = target;
  host.trim();
  if (host.length() == 0) host = link_.gateway;  // default: the gateway
  out.print(F("[portscan] target "));
  out.println(host);
  for (uint8_t i = 0; i < count; ++i) {
    WiFiClient client;
    bool open = client.connect(host.c_str(), kCommonPorts[i].port, 600);
    if (open) client.stop();
    out_[i].port = kCommonPorts[i].port;
    out_[i].service = kCommonPorts[i].service;
    out_[i].open = open;
    out.print(F("[portscan] "));
    out.print(kCommonPorts[i].port);
    out.print(F("/"));
    out.print(kCommonPorts[i].service);
    out.println(open ? F(" OPEN") : F(" closed"));
  }
  return count;
#else
  String host = target;
  host.trim();
  if (host.length() == 0) host = "192.168.4.1";
  out.print(F("[portscan] mock target "));
  out.println(host);
  // Believable: web + smb + mqtt open on the demo gateway, the rest closed.
  for (uint8_t i = 0; i < count; ++i) {
    uint16_t p = kCommonPorts[i].port;
    bool open = (p == 80 || p == 443 || p == 445 || p == 1883);
    out_[i].port = p;
    out_[i].service = kCommonPorts[i].service;
    out_[i].open = open;
    out.print(F("[portscan] "));
    out.print(p);
    out.print(F("/"));
    out.print(kCommonPorts[i].service);
    out.println(open ? F(" OPEN") : F(" closed"));
  }
  return count;
#endif
}

String WifiOps::ouiVendor(const String &mac) {
  String p;
  for (uint16_t i = 0; i < mac.length() && p.length() < 6; ++i) {
    char c = mac[i];
    if (isxdigit((unsigned char)c)) p += (char)toupper(c);
  }
  if (p.length() < 6) return "";
  String oui = p.substring(0, 6);
  struct Oui { const char *oui; const char *vendor; };
  static const Oui kTable[] = {
      {"FCFBFB", "Apple"},      {"3C0754", "Apple"},   {"B827EB", "Raspberry Pi"},
      {"DCA632", "Raspberry Pi"},{"E4F042", "Google"}, {"5CCF7F", "Espressif"},
      {"A42BB0", "TP-Link"},    {"F09FC2", "Ubiquiti"},{"E8DB84", "Espressif"},
      {"B41E52", "Flock Safety"},{"001122", "(lab)"},
  };
  for (const Oui &e : kTable)
    if (oui == e.oui) return e.vendor;
  return "";
}

bool WifiOps::dnsLookup(const String &host, String &ipOut, Stream &out) {
#if USE_WIFI_ACTIVE
  if (link_.state != WLINK_CONNECTED) { out.println(F("[dns] no link")); return false; }
  IPAddress ip;
  if (WiFi.hostByName(host.c_str(), ip)) {
    ipOut = ip.toString();
    out.print(F("[dns] "));
    out.print(host);
    out.print(F(" -> "));
    out.println(ipOut);
    return true;
  }
  out.print(F("[dns] not resolved: "));
  out.println(host);
  return false;
#else
  ipOut = "93.184.216.34";
  out.print(F("[dns] mock "));
  out.print(host);
  out.print(F(" -> "));
  out.println(ipOut);
  return true;
#endif
}

bool WifiOps::httpBanner(const String &url, String &titleOut, String &serverOut, Stream &out) {
  titleOut = "";
  serverOut = "";
#if USE_WIFI_ACTIVE
  if (link_.state != WLINK_CONNECTED) { out.println(F("[banner] no link")); return false; }
  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, url)) { out.println(F("[banner] begin failed")); return false; }
  http.setConnectTimeout(4000);
  const char *hdrs[] = {"Server"};
  http.collectHeaders(hdrs, 1);
  int code = http.GET();
  serverOut = http.header("Server");
  if (code > 0) {
    String body = http.getString();
    int t = body.indexOf("<title");
    if (t >= 0) {
      int gt = body.indexOf('>', t);
      int end = body.indexOf("</title>", gt);
      if (gt >= 0 && end > gt) titleOut = body.substring(gt + 1, end);
      titleOut.trim();
    }
  }
  http.end();
  out.print(F("[banner] http="));
  out.print(code);
  out.print(F(" server="));
  out.print(serverOut);
  out.print(F(" title="));
  out.println(titleOut);
  return code > 0;
#else
  (void)url;
  titleOut = "Example Domain";
  serverOut = "nginx";
  out.println(F("[banner] mock title='Example Domain' server=nginx"));
  return true;
#endif
}

uint8_t WifiOps::hostSweep(uint8_t start, uint8_t end, uint16_t port, String liveOut[],
                           uint8_t maxRecords, Stream &out) {
  if (maxRecords == 0 || end < start) return 0;
#if USE_WIFI_ACTIVE
  if (link_.state != WLINK_CONNECTED) { out.println(F("[sweep] no link")); return 0; }
  String gw = link_.gateway;
  int dot = gw.lastIndexOf('.');
  if (dot < 0) { out.println(F("[sweep] no gateway")); return 0; }
  String base = gw.substring(0, dot + 1);
  out.print(F("[sweep] "));
  out.print(base);
  out.print(start);
  out.print(F("-"));
  out.print(end);
  out.print(F(" :"));
  out.println(port);
  uint8_t live = 0;
  for (uint16_t o = start; o <= end && live < maxRecords; ++o) {
    String ip = base + String(o);
    WiFiClient c;
    bool up = c.connect(ip.c_str(), port, 250);
    if (up) c.stop();
    if (up) {
      liveOut[live++] = ip;
      out.print(F("[sweep] live "));
      out.println(ip);
    }
  }
  out.print(F("[sweep] "));
  out.print(live);
  out.println(F(" live"));
  return live;
#else
  (void)start; (void)end; (void)port;
  static const char *kMock[] = {"192.168.4.1", "192.168.4.10", "192.168.4.23"};
  uint8_t n = 3 < maxRecords ? 3 : maxRecords;
  for (uint8_t i = 0; i < n; ++i) {
    liveOut[i] = kMock[i];
    out.print(F("[sweep] mock live "));
    out.println(kMock[i]);
  }
  return n;
#endif
}
