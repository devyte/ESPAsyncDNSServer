#include <ESP8266WiFi.h>
#include <ESPAsyncDNSServer.h>
#include <ESPAsyncWebServer.h>

// typedef enum {
//   HTTP_GET     = 0b00000001,
//   HTTP_POST    = 0b00000010,
//   HTTP_DELETE  = 0b00000100,
//   HTTP_PUT     = 0b00001000,
//   HTTP_PATCH   = 0b00010000,
//   HTTP_HEAD    = 0b00100000,
//   HTTP_OPTIONS = 0b01000000,
//   HTTP_ANY     = 0b01111111,
// } WebRequestMethod;

const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 1, 1);
AsyncDNSServer dnsServer;
AsyncWebServer webServer(80);

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP("DNSServer example");

  // modify TTL associated  with the domain name (in seconds)
  // default is 60 seconds
  dnsServer.setTTL(300);
  // set which return code will be used for all other domains (e.g. sending
  // ServerFailure instead of NonExistentDomain will reduce number of queries
  // sent by clients)
  // default is AsyncDNSReplyCode::NonExistentDomain
  dnsServer.setErrorReplyCode(AsyncDNSReplyCode::ServerFailure);

  // start DNS server for a specific domain name
  dnsServer.start(DNS_PORT, "*", apIP);

  // simple HTTP server to see that DNS server is working
  webServer.on("/", 0b00000001, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });
  webServer.begin();
  Serial.println(F("setup done"));
}

void loop() {
}
