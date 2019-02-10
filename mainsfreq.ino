#include <stdio.h>

#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>

#include "Arduino.h"

#define PIN_LED     D4
#define PIN_MAINS   D5

#define MQTT_HOST   "mosquitto.space.revspace.nl"
#define MQTT_PORT   1883
#define MQTT_TOPIC  "revspace/sensors/ac/frequency"

#define PUBLISH_INTERVAL 10
#define BUFFER_SIZE      50

// our mains interrupt counter
static volatile unsigned long count = 0;

// state variables
static int secs_prev = 0;
static unsigned long count_prev = 0;
static unsigned long msec_prev = 0;
static int buffer[BUFFER_SIZE];

static char esp_id[16];
static WiFiManager wifiManager;
static WiFiClient wifiClient;
static PubSubClient mqttClient(wifiClient);

// mains interrupt, is called approximately 100 times per second
static void mains_interrupt(void) 
{
    unsigned long msec = millis();
    if ((msec - msec_prev) > 8) {
        count++;
        msec_prev = msec;
    }
}

static bool mqtt_publish(const char *topic, const char *text, bool retained)
{
    bool result = false;

    if (!mqttClient.connected()) {
        Serial.print("Connecting MQTT...");
        mqttClient.setServer(MQTT_HOST, MQTT_PORT);
        result = mqttClient.connect(esp_id, topic, 0, retained, "offline");
        Serial.println(result ? "OK" : "FAIL");
    }
    if (mqttClient.connected()) {
        Serial.print("Publishing ");
        Serial.print(text);
        Serial.print(" to ");
        Serial.print(topic);
        Serial.print("...");
        result = mqttClient.publish(topic, text, retained);
        Serial.println(result ? "OK" : "FAIL");
    }
    return result;
}

void setup(void)
{
    // welcome message
    Serial.begin(115200);
    Serial.println("AC mains frequency counter");

    // get ESP id
    sprintf(esp_id, "%08X", ESP.getChipId());
    Serial.print("ESP ID: ");
    Serial.println(esp_id);

    // connect to wifi
    Serial.println("Starting WIFI manager ...");
    wifiManager.setConfigPortalTimeout(120);
    wifiManager.autoConnect("ESP-MAINSFREQ");
    
    // initialize buffer with nominal value
    for (int i = 0; i < BUFFER_SIZE; i++) {
        buffer[i] = 100;
    }
    
    // connect interrupt
    pinMode(PIN_MAINS, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_MAINS), mains_interrupt, FALLING);
    
    // LED
    pinMode(PIN_LED, OUTPUT);

    // sync to second
    int secs;
    secs_prev = millis() / 1000;
    do {
        secs = millis() / 1000;
    } while (secs == secs_prev);
    count_prev = count;
    secs_prev = secs;
}
    

void loop(void)
{
    static int secs_pub = 0;
    static int idx = 0;

    int secs = millis() / 1000;

    // new second?
    if (secs != secs_prev) {
        secs_prev = secs;
    
        // get a snapshot of the interrupt counter
        unsigned long count_copy = count;

        // calculate increase
        unsigned long count_diff = count_copy - count_prev;
        count_prev = count_copy;
        
        // store it
        buffer[idx] = count_diff;
        idx = (idx + 1) % BUFFER_SIZE;
    }
    
    // publish once in a while
    if ((secs - secs_pub) > PUBLISH_INTERVAL) {
        secs_pub = secs;

        // calculate total interrupts in buffer
        int sum = 0;
        for (int i = 0; i < BUFFER_SIZE; i++) {
            sum += buffer[i];
        }
        
        // publish over mqtt
        char value[16];
        sprintf(value, "%2.2f Hz", sum / 100.0);
        mqtt_publish(MQTT_TOPIC, value, true);

        // verify network connection and reboot on failure
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("Restarting ESP...");
            ESP.restart();
        }
    }
    
    // update LED
    int led = (count / 50) & 1;
    digitalWrite(PIN_LED, led);
    
    // keep MQTT alive
    mqttClient.loop();
}


