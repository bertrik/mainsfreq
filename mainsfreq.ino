#include <stdio.h>

#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>

#include "Arduino.h"

#define PIN_MAINS   D0

#define MQTT_HOST   "mosquitto.space.revspace.nl"
#define MQTT_PORT   1883
#define MQTT_TOPIC  "revspace/ac/frequency"

#define PUBLISH_INTERVAL 30
#define BUFFER_SIZE     100

// our mains cycle counter
static volatile unsigned long count = 0;

// state variables
static int secs_prev = 0;
static unsigned long count_prev = 0;
static int buffer[100];

static char esp_id[16];
static WiFiManager wifiManager;
static WiFiClient wifiClient;
static PubSubClient mqttClient(wifiClient);

// mains interrupt, is called approximately 50 times per second
static void mains_interrupt(void) 
{
    count++;
}

static void mqtt_publish(const char *topic, const char *text)
{
    if (!mqttClient.connected()) {
        mqttClient.setServer(MQTT_HOST, MQTT_PORT);
        mqttClient.connect(esp_id);
    }
    if (mqttClient.connected()) {
        Serial.print("Publishing ");
        Serial.print(text);
        Serial.print(" to ");
        Serial.print(topic);
        Serial.print("...");
        int result = mqttClient.publish(topic, text, true);
        Serial.println(result ? "OK" : "FAIL");
    }
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
    wifiManager.autoConnect("ESP-MAINSFREQ");
    
    // initialize buffer with nominal value
    for (int i = 0; i < BUFFER_SIZE; i++) {
        buffer[i] = 50;
    }
    
    // connect interrupt
    pinMode(PIN_MAINS, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_MAINS), mains_interrupt, FALLING);

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
    
        // get a snapshot of the cycle counter
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

        // calculate total cycles over 100 seconds
        int sum = 0;
        for (int i = 0; i < BUFFER_SIZE; i++) {
            sum += buffer[i];
        }
        
        // publish over mqtt
        char value[16];
        sprintf(value, "%2.2f Hz", sum / 100.0);
        mqtt_publish(MQTT_TOPIC, value);
    }
}



