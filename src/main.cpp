#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266SSDP.h>
#include <PubSubClient.h>
#include <NeoPixelBus.h>
#include "jscolor.h"
#include "Parameters.h"
#include "RtcFlags.h"

const char CP_SSID[] PROGMEM = "ESP01_RGB";
const char CP_PSWD[] PROGMEM = "1029384756";

const char PARAM_WIFI_SSID_NAME[] PROGMEM = "wifi_ssid";
const char PARAM_WIFI_SSID_TITLE[] PROGMEM = "WiFi SSID";
const char PARAM_WIFI_PSWD_NAME[] PROGMEM = "wifi_pswd";
const char PARAM_WIFI_PSWD_TITLE[] PROGMEM = "WiFi password";
const char PARAM_MQTT_SERVER_NAME[] PROGMEM = "mqtt_server";
const char PARAM_MQTT_SERVER_TITLE[] PROGMEM = "MQTT broker";
const char PARAM_MQTT_PORT_NAME[] PROGMEM = "mqtt_port";
const char PARAM_MQTT_PORT_TITLE[] PROGMEM = "MQTT port";
const uint16_t PARAM_MQTT_PORT_DEF = 1883;
const char PARAM_MQTT_CLIENT_NAME[] PROGMEM = "mqtt_client";
const char PARAM_MQTT_CLIENT_TITLE[] PROGMEM = "MQTT client";
const char PARAM_MQTT_CLIENT_DEF[] PROGMEM = "ESP01_RGB";
const char PARAM_MQTT_USER_NAME[] PROGMEM = "mqtt_user";
const char PARAM_MQTT_USER_TITLE[] PROGMEM = "MQTT user";
const char PARAM_MQTT_PSWD_NAME[] PROGMEM = "mqtt_pswd";
const char PARAM_MQTT_PSWD_TITLE[] PROGMEM = "MQTT password";
const char PARAM_MQTT_TOPIC_NAME[] PROGMEM = "mqtt_topic";
const char PARAM_MQTT_TOPIC_TITLE[] PROGMEM = "MQTT topic";
const char PARAM_MQTT_TOPIC_DEF[] PROGMEM = "/RGB";
const char PARAM_MQTT_RETAINED_NAME[] PROGMEM = "mqtt_retain";
const char PARAM_MQTT_RETAINED_TITLE[] PROGMEM = "MQTT retained";
const bool PARAM_MQTT_RETAINED_DEF = false;
const char PARAM_PIXELS_NAME[] PROGMEM = "pixels";
const char PARAM_PIXELS_TITLE[] PROGMEM = "Pixels count";
const uint16_t PARAM_PIXELS_DEF = 1;
const char PARAM_COLOR_NAME[] PROGMEM = "color";
const uint32_t PARAM_COLOR_DEF = 0x00404040; // White

const paraminfo_t PARAMS[] PROGMEM = {
  PARAM_STR(PARAM_WIFI_SSID_NAME, PARAM_WIFI_SSID_TITLE, 33, NULL),
  PARAM_PASSWORD(PARAM_WIFI_PSWD_NAME, PARAM_WIFI_PSWD_TITLE, 33, NULL),
  PARAM_STR(PARAM_MQTT_SERVER_NAME, PARAM_MQTT_SERVER_TITLE, 33, NULL),
  PARAM_U16(PARAM_MQTT_PORT_NAME, PARAM_MQTT_PORT_TITLE, PARAM_MQTT_PORT_DEF),
  PARAM_STR(PARAM_MQTT_CLIENT_NAME, PARAM_MQTT_CLIENT_TITLE, 33, PARAM_MQTT_CLIENT_DEF),
  PARAM_STR(PARAM_MQTT_USER_NAME, PARAM_MQTT_USER_TITLE, 33, NULL),
  PARAM_PASSWORD(PARAM_MQTT_PSWD_NAME, PARAM_MQTT_PSWD_TITLE, 33, NULL),
  PARAM_STR(PARAM_MQTT_TOPIC_NAME, PARAM_MQTT_TOPIC_TITLE, 33, PARAM_MQTT_TOPIC_DEF),
  PARAM_BOOL(PARAM_MQTT_RETAINED_NAME, PARAM_MQTT_RETAINED_TITLE, PARAM_MQTT_RETAINED_DEF),
  PARAM_U16_CUSTOM(PARAM_PIXELS_NAME, PARAM_PIXELS_TITLE, PARAM_PIXELS_DEF, 1, 1024, EDITOR_TEXT(4, 4, false, false, false)),
  PARAM_U32_CUSTOM(PARAM_COLOR_NAME, NULL, PARAM_COLOR_DEF, 0, 4294967295, EDITOR_NONE())
};

const uint32_t COLOR_WIFI = 0x00000040; // Blue
const uint32_t COLOR_MQTT = 0x00004000; // Green
const uint32_t COLOR_ERROR = 0x00400000; // Red

typedef NeoPixelBus<NeoGrbFeature, NeoEsp8266Uart1Ws2812xMethod> strip_t;

Parameters *params;
strip_t *strip = NULL;
ESP8266WebServer *http = NULL;
WiFiClient *client = NULL;
PubSubClient *mqtt = NULL;
RgbColor color;

static void halt() {
  ESP.deepSleep(0);
}

static void restart() {
  ESP.restart();
}

static void rgbToColor(RgbColor &color, uint32_t rgb) {
  color.R = rgb >> 16;
  color.G = rgb >> 8;
  color.B = rgb;
}

static uint32_t colorToRgb(const RgbColor color) {
  return (color.R << 16) | (color.G << 8) | color.B;
}

static String longToHex(uint32_t value) {
  String result;

  for (uint8_t i = 2; i < 8; ++i) {
    uint8_t b = (value >> (4 * (7 - i))) & 0x0F;

    if (b < 10)
      result += char(b + '0');
    else
      result += char(b - 10 + 'A');
  }
  return result;
}

static void setStripColor(RgbColor color, bool publish = false) {
  strip->ClearTo(color);
  strip->Show();
  if (publish && mqtt && mqtt->connected()) {
    mqtt->publish((char*)params->value(PARAM_MQTT_TOPIC_NAME), longToHex(colorToRgb(color)).c_str(), *(bool*)params->value(PARAM_MQTT_RETAINED_NAME));
  }
}

static void httpRootPage() {
  String page;

  page = F("<!DOCTYPE html>\n"
    "<html>\n"
    "<head>\n"
    "<title>ESP01-RGB</title>\n"
    "<style>\n"
    "body{background-color:#eee;}\n"
    "</style>\n"
    "<script type=\"text/javascript\">\n"
    "function getXmlHttpRequest(){\n"
    "let x;\n"
    "try{\n"
    "x=new ActiveXObject(\"Msxml2.XMLHTTP\");\n"
    "}catch(e){\n"
    "try{\n"
    "x=newActiveXObject(\"Microsoft.XMLHTTP\");\n"
    "}catch(E){\n"
    "x=false;\n"
    "}\n"
    "}\n"
    "if((!x)&&(typeof XMLHttpRequest!='undefined')){\n"
    "x=new XMLHttpRequest();\n"
    "}\n"
    "return x;\n"
    "}\n"
    "function openUrl(u,m){\n"
    "let x=getXmlHttpRequest();\n"
    "x.open(m,u,false);\n"
    "x.send(null);\n"
    "if(x.status!=200){\n"
    "alert(x.responseText);\n"
    "return false;\n"
    "}\n"
    "return true;\n"
    "}\n"
    "function refreshData(){\n"
    "let request=getXmlHttpRequest();\n"
    "request.open('GET','/color?dummy='+Date.now(),true);\n"
    "request.onreadystatechange=function(){\n"
    "if((request.readyState==4)&&(request.status==200)){\n"
    "let data=JSON.parse(request.responseText);\n"
    "document.getElementById('color').value=data.color;\n"
    "}\n"
    "}\n"
    "request.send(null);\n"
    "}\n"
    "setInterval(refreshData,500);\n"
    "</script>\n"
    "<script src=\"jscolor.min.js.gz\"></script>\n"
    "</head>\n"
    "<body>\n"
    "<form action=\"/color\" method=\"post\">\n"
    "Color: <input data-jscolor=\"\" name=\"color\" id=\"color\" value=\"");
  page += longToHex(colorToRgb(color));
  page += F("\" onchange=\"openUrl('/color?color='+this.value,'put');\">\n"
    "<p>\n"
    "<input type=\"submit\" value=\"Save color\">\n"
    "<input type=\"button\" value=\"Setup\" onclick=\"location.href='/setup'\">\n"
    "<input type=\"button\" value=\"Restart!\" onclick=\"if(confirm('Are you sure to restart?')){location.href='/restart';}\">\n"
    "</form>\n"
    "</body>\n"
    "</html>");
  http->send(200, F("text/html"), page);
}

static bool isHexChar(char c) {
  return ((c >= '0') && (c <= '9')) || ((c >= 'A') && (c <= 'F')) || ((c >= 'a') && (c <= 'f'));
}

static uint8_t hexToByte(char c) {
  if ((c >= '0') && (c <= '9'))
    return c - '0';
  if ((c >= 'A') && (c <= 'F'))
    return c - 'A' + 10;
  if ((c >= 'a') && (c <= 'f'))
    return c - 'a' + 10;
  return 0;
}

static void httpColorPage() {
  if (http->method() == HTTP_GET) {
    String page = F("{\"color\":\"");

    page.concat(longToHex(colorToRgb(color)));
    page.concat(F("\"}"));
    http->send(200, F("text/json"), page);
  } else if (http->method() == HTTP_POST) {
    String page;
    bool error = true;

    page = F("<!DOCTYPE html>\n"
      "<html>\n"
      "<head>\n"
      "<style>\n"
      "body{background-color:#eee;}\n"
      "</style>\n"
      "<meta http-equiv=\"refresh\" content=\"1;URL=/\">\n"
      "</head>\n"
      "<body>\n");
    if (http->hasArg(F("color"))) {
      String param = http->arg(F("color"));

      if (param.length() == 6) {
        uint32_t rgb = 0;

        for (uint8_t i = 0; i < 6; ++i) {
          if (isHexChar(param[i]))
            rgb |= ((uint32_t)hexToByte(param[i]) << (4 * (5 - i)));
          else {
            rgb = (uint32_t)-1;
            break;
          }
        }
        if (rgb != (uint32_t)-1) {
          params->set(PARAM_COLOR_NAME, &rgb);
          params->update();
          error = false;
        }
      }
    }
    if (error) {
      page.concat(F("Bad argument!"));
    } else {
      page.concat(F("OK"));
    }
    page.concat(F("\n</body>\n"
      "</html>"));
    http->send(error ? 400 : 200, F("text/html"), page);
  } else if (http->method() == HTTP_PUT) {
    bool error = true;

    if (http->hasArg(F("color"))) {
      String param = http->arg(F("color"));

      if (param.length() == 6) {
        uint32_t rgb = 0;

        for (uint8_t i = 0; i < 6; ++i) {
          if (isHexChar(param[i]))
            rgb |= ((uint32_t)hexToByte(param[i]) << (4 * (5 - i)));
          else {
            rgb = (uint32_t)-1;
            break;
          }
        }
        if (rgb != (uint32_t)-1) {
          rgbToColor(color, rgb);
          setStripColor(color, true);
          error = false;
        }
      }
    }
    http->send(error ? 400 : 200, F("text/plain"), error ? PSTR("Bad argument!") : PSTR("OK"));
  } else {
    http->send_P(405, PSTR("text/plain"), PSTR("Method Not Allowed!"));
  }
}

static void httpJSColorPage() {
  http->sendHeader(F("Content-Encoding"), F("gzip"));
  http->send_P(200, PSTR("text/javascript"), (const char*)JSCOLOR_JS, sizeof(JSCOLOR_JS));
}

static void httpPageNotFound() {
  http->send_P(404, PSTR("text/plain"), PSTR("Page not found!"));
}

static void httpRestartPage() {
  http->send_P(200, PSTR("text/html"), PSTR("<!DOCTYPE html>\n"
    "<html>\n"
    "<head>\n"
    "<title>Restart</title>\n"
    "<style>\n"
    "body{background-color:#eee;}\n"
    "</style>\n"
    "<meta http-equiv=\"refresh\" content=\"15;URL=/\">\n"
    "</head>\n"
    "<body>\n"
    "Restarting...\n"
    "</body>\n"
    "</html>"));
  http->client().stop();
  restart();
}

void setup() {
  WiFi.persistent(false);

  params = new Parameters(PARAMS, ARRAY_SIZE(PARAMS));
  if ((! params) || (! params->begin()))
    halt();

  bool paramIncomplete = (! *(char*)params->value(PARAM_WIFI_SSID_NAME)) || (! *(char*)params->value(PARAM_WIFI_PSWD_NAME)) || (! *(uint16_t*)params->value(PARAM_PIXELS_NAME));

  if (paramIncomplete || ((ESP.getResetInfoPtr()->reason != REASON_SOFT_RESTART) && (! RtcFlags::getFlag(0)))) {
    RtcFlags::setFlag(0);
    if (! paramsCaptivePortal(params, CP_SSID, CP_PSWD, paramIncomplete ? 0 : 60, [&](cpevent_t event, void *param) {
      switch (event) {
        case CP_INIT:
          strip = new strip_t(1);
          if (strip) {
            strip->Begin();
          }
          break;
        case CP_DONE:
        case CP_RESTART:
          if (strip) {
            strip->ClearTo(RgbColor(0, 0, 0));
            strip->Show();
            delete strip;
            strip = NULL;
          }
          break;
        case CP_IDLE:
          if (strip) {
            RgbColor color;

            if ((millis() % (((ESP8266WiFiClass*)param)->softAPgetStationNum() > 0 ? 500 : 250)) < 50) {
              color.R = color.G = color.B = 0x80;
            } else {
              color.R = color.G = color.B = 0;
            }
            if (strip->GetPixelColor(0) != color) {
              strip->SetPixelColor(0, color);
              strip->Show();
            }
          }
          break;
        default:
          break;
      }
    }))
      halt();
  }
  RtcFlags::clearFlag(0);
  if ((! *(char*)params->value(PARAM_WIFI_SSID_NAME)) || (! *(char*)params->value(PARAM_WIFI_PSWD_NAME)) || (! *(uint16_t*)params->value(PARAM_PIXELS_NAME)))
    restart();

  http = new ESP8266WebServer();
  if (! http)
    halt();
  http->onNotFound(httpPageNotFound);
  http->on(F("/"), HTTP_GET, httpRootPage);
  http->on(F("/index.html"), HTTP_GET, httpRootPage);
  http->on(F("/jscolor.min.js.gz"), HTTP_GET, httpJSColorPage);
  http->on(F("/color"), httpColorPage);
  http->on(F("/setup"), [&]() {
    params->handleWebPage(*http);
  });
  http->on(F("/restart"), HTTP_GET, httpRestartPage);
  http->on(F("/description.xml"), HTTP_GET, [&]() {
    SSDP.schema(http->client());
  });

  SSDP.setSchemaURL(F("description.xml"));
  SSDP.setHTTPPort(80);
  SSDP.setName(F("ESP01 RGB"));
  SSDP.setSerialNumber(F("12345678"));
  SSDP.setURL(F("index.html"));
  SSDP.setModelName(F("ESP01S RGB LED"));
  SSDP.setModelNumber(F("v1.0"));
  SSDP.setManufacturer(F("NoName Ltd."));
  SSDP.setDeviceType(F("upnp:rootdevice"));

  if (*(char*)params->value(PARAM_MQTT_SERVER_NAME) && *(char*)params->value(PARAM_MQTT_CLIENT_NAME) && *(char*)params->value(PARAM_MQTT_TOPIC_NAME)) {
    client = new WiFiClient();
    if (! client)
      halt();
    mqtt = new PubSubClient(*client);
    if (! mqtt)
      halt();
    mqtt->setServer((char*)params->value(PARAM_MQTT_SERVER_NAME), *(uint16_t*)params->value(PARAM_MQTT_PORT_NAME));
    mqtt->setCallback([&](char *topic, uint8_t *payload, unsigned int length) {
      if (! strcmp(topic, (char*)params->value(PARAM_MQTT_TOPIC_NAME))) {
        uint32_t rgb = 0;

        while (length) {
          uint8_t d;

          if ((*payload >= '0') && (*payload <= '9'))
            d = *payload - '0';
          else if ((*payload >= 'A') && (*payload <= 'F'))
            d = *payload - 'A' + 10;
          else if ((*payload >= 'a') && (*payload <= 'f'))
            d = *payload - 'a' + 10;
          else
            break;
          rgb = (rgb << 4) | d;
          ++payload;
          --length;
        }
        if (! length) { // No error
          rgbToColor(color, rgb);
          setStripColor(color);
        }
      }
    });
  }

  strip = new strip_t(*(uint16_t*)params->value(PARAM_PIXELS_NAME));
  if (! strip)
    halt();
  strip->Begin();
  rgbToColor(color, *(uint32_t*)params->value(PARAM_COLOR_NAME));
  setStripColor(color);

  WiFi.mode(WIFI_STA);
  {
    const char *name = (char*)params->value(PARAM_MQTT_CLIENT_NAME);

    if (*name) {
      WiFi.hostname(name);
    }
  }
}

void loop() {
  const uint32_t WIFI_TIMEOUT = 30000; // 30 sec.
  const uint32_t MQTT_TIMEOUT = 30000; // 30 sec.

  static uint32_t lastWiFiTry = 0;
  static uint32_t lastMqttTry = 0;

  if (! WiFi.isConnected()) {
    if ((! lastWiFiTry) || (millis() - lastWiFiTry >= WIFI_TIMEOUT)) {
      uint32_t start;

      SSDP.end();
      WiFi.begin((char*)params->value(PARAM_WIFI_SSID_NAME), (char*)params->value(PARAM_WIFI_PSWD_NAME));
      start = millis();
      while ((! WiFi.isConnected()) && (millis() - start < WIFI_TIMEOUT)) {
        if (millis() % 1000 < 500)
          strip->SetPixelColor(0, RgbColor((COLOR_WIFI >> 16) & 0xFF, (COLOR_WIFI >> 8) & 0xFF, COLOR_WIFI & 0xFF));
        else
          strip->SetPixelColor(0, RgbColor(0, 0, 0));
        strip->Show();
        delay(500);
      }
      if (WiFi.isConnected()) {
        strip->SetPixelColor(0, color);
        strip->Show();
        http->begin();
        SSDP.begin();
        lastWiFiTry = 0;
      } else {
        WiFi.disconnect();
        strip->SetPixelColor(0, RgbColor((COLOR_ERROR >> 16) & 0xFF, (COLOR_ERROR >> 8) & 0xFF, COLOR_ERROR & 0xFF));
        strip->Show();
        lastWiFiTry = millis();
      }
    }
  } else {
    http->handleClient();
    if (mqtt) {
      if (! mqtt->connected()) {
        if ((! lastMqttTry) || (millis() - lastMqttTry >= MQTT_TIMEOUT)) {
          const char *user, *pswd;
          bool connected;

          strip->SetPixelColor(0, RgbColor((COLOR_MQTT >> 16) & 0xFF, (COLOR_MQTT >> 8) & 0xFF, COLOR_MQTT & 0xFF));
          strip->Show();
          user = (char*)params->value(PARAM_MQTT_USER_NAME);
          pswd = (char*)params->value(PARAM_MQTT_PSWD_NAME);
          if (*user && *pswd)
            connected = mqtt->connect((char*)params->value(PARAM_MQTT_CLIENT_NAME), user, pswd);
          else
            connected = mqtt->connect((char*)params->value(PARAM_MQTT_CLIENT_NAME));
          if (connected) {
            mqtt->subscribe((char*)params->value(PARAM_MQTT_TOPIC_NAME));
            strip->SetPixelColor(0, color);
            strip->Show();
            lastMqttTry = 0;
          } else {
            strip->SetPixelColor(0, RgbColor((COLOR_ERROR >> 16) & 0xFF, (COLOR_ERROR >> 8) & 0xFF, COLOR_ERROR & 0xFF));
            strip->Show();
            lastMqttTry = millis();
          }
        }
      } else {
        mqtt->loop();
      }
    }
  }
//  delay(1);
}
