#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <Adafruit_NeoPixel.h>

// put your wifi credentials in here
#include "credentials.h"

#define PIN D2

const char *mqtt_server = "iot.eclipse.org";
char mqtt_id[33];
unsigned long start;

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
Adafruit_NeoPixel strip = Adafruit_NeoPixel(60, PIN, NEO_GRB + NEO_KHZ800);

class Effect {
 protected:
  static uint32_t color;
  uint32_t interval, next;
 public:
  Effect(uint32_t i) {
    interval = i;
  }
  void setColor(uint32_t c) {
    color = c;
  }
  void refresh(long millis) {
    if (millis == 0)
      next = 0;
    if (millis >= next) {
      next += interval;
      int tick = millis/interval;
      update(tick);
    }
  }
  virtual void update(long tick) = 0;
};

uint32_t Effect::color = 0;

class Solid: public Effect {
 public:
  Solid() : Effect(2^16-1) {}
  void update(long tick) {
    for(int i=0; i < strip.numPixels(); i++) {
      strip.setPixelColor(i, color);
    }
    strip.show();
  }
};

class Chase: public Effect {
 public:
  Chase() : Effect(200) {}
  void update(long tick) {
    for(int i=0; i < strip.numPixels(); i++) {
      uint32_t color = (i + tick) % 3 ? this->color : 0;
      strip.setPixelColor(i, color);
    }
    strip.show();
  }
};

class Twinkle: public Effect {
 public:
  Twinkle() : Effect(200) {}
  void update(long tick) {
    int t = random(strip.numPixels());
    for(int i=0; i < strip.numPixels(); i++) {
      uint32_t color = (i == t) ? 0xffffff : this->color;
      strip.setPixelColor(i, color);
    }
    strip.show();
  }
};

uint32_t component(uint32_t c1, uint32_t c2, uint32_t mask, int i, int n) {
  return ((c1 & mask) * i + (c2 & mask) * (n-i)) / n & mask;
}

uint32_t interpolate(uint32_t c1, uint32_t c2, int i, int n) {
  return component(c1, c2, 0xff0000, i, n)
    | component(c1, c2, 0xff00, i, n)
    | component(c1, c2, 0xff, i, n);
}

class Worm: public Effect {
 public:
  Worm() : Effect(100) {}
  void update(long tick) {
    for(int i=0; i < strip.numPixels(); i++) {
      int n = (i + tick) % (strip.numPixels()/4);
      uint32_t color = n < 6 ? interpolate(this->color, 0, 1<<(6-n), 1<<6) : 0;
      strip.setPixelColor(i, color);
    }
    strip.show();
  }
};

class Glow: public Effect {
 public:
  Glow() : Effect(500) {}
  void update(long tick) {
    for(int i=0; i < strip.numPixels(); i++) {
      int x = tick % 20;
      if (x >= 6)
        x = 6-x;
      uint32_t color = interpolate(0, this->color, x, 6);
      strip.setPixelColor(i, color);
    }
    strip.show();
  }
};

Solid solid;
Chase chase;
Twinkle twinkle;
Worm worm;
Glow glow;
Effect *effect = NULL;
Effect *effects[] = {&chase, &twinkle, &worm};
//Effect *effects[] = {&glow};

void messageReceived(char* topic, unsigned char* payload, unsigned int length);
void randomEffect();
void setupWifi();
void setupOTA();
void setupMqtt();

void setup() {
  Serial.begin(76800);
  Serial.println("Booting");

  strip.begin();
  strip.show(); // Initialize all pixels to 'off'

  setupWifi();
  setupOTA();
  setupMqtt();
  // update the lights to show we're loading
  randomEffect();
  effect->setColor(0xff0000);
  effect->refresh(0);
}

void setupWifi() {
  Serial.print("Connecting to ");
  Serial.println(wifi_ssid);
  WiFi.begin(wifi_ssid, wifi_password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void setupOTA() {
  sprintf(mqtt_id, "cheerlights2812-%06x", ESP.getChipId());
  ArduinoOTA.setHostname(mqtt_id);
  ArduinoOTA.onStart([]() {
//    digitalWrite(RED_LED, LOW);
//    digitalWrite(GREEN_LED, LOW);
//    digitalWrite(BLUE_LED, LOW);
    Serial.println("\nOTA start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA end\n");
//    digitalWrite(RED_LED, LOW);
//    digitalWrite(GREEN_LED, LOW);
//    digitalWrite(BLUE_LED, LOW);
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    int p = (progress * 100 / total);
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
//    digitalWrite(RED_LED, p > 25);
//    digitalWrite(BLUE_LED, p > 50);
//    digitalWrite(GREEN_LED, p > 75);
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
}

void setupMqtt() {
  mqttClient.setServer(mqtt_server, 1883);
  mqttClient.setCallback(messageReceived);
}

void reconnect() {
  // Loop until we're reconnected
  while (!mqttClient.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (mqttClient.connect(mqtt_id)) {
      Serial.println("connected");
      // subscribe
      mqttClient.subscribe("cheerlightsRGB");
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void randomEffect() {
  int i = random(sizeof(effects)/sizeof(Effect*));
  effect = effects[i];
}

void loop() {
  ArduinoOTA.handle();
  if (!mqttClient.connected())
    reconnect();
  mqttClient.loop();

  long now = millis();
  if (now > start + 15000) {
    // change effect
    start = now;
    randomEffect();
  }
  effect->refresh(now - start);
}

void messageReceived(char* topic, unsigned char* payload, unsigned int length) {
  if (length != 7) {
    Serial.print("Bad color: ");
    Serial.println((char*)payload);
    return;
  }
  // parse the hexidecimal value
  char str[7];
  memcpy(str, &payload[1], 6);
  str[6] = '\0';
  Serial.println(str);
  long val = strtol(str, NULL, 16); // base 16 (hex)

  start = millis();
  randomEffect();
  effect->setColor(val);
  effect->refresh(0);
}
