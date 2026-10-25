/*
 * AZ3166 DevKit -> Home Assistant MQTT node (standalone, no PC involved).
 *
 * Connects to Wi-Fi on its own, publishes sensor telemetry to the MQTT
 * broker on the Pi and registers itself in Home Assistant via MQTT
 * discovery. Availability is a retained LWT so HA shows "unavailable"
 * when the board loses power.
 *
 * Build: arduino-cli + AZ3166 platform (see README in this folder).
 */

#include "AZ3166WiFi.h"
#include "IoT_DevKit_HW.h"
#include "net_config.h"

static const char *AVAIL_TOPIC = MQTT_BASE "/availability";
static const char *STATE_TOPIC = MQTT_BASE "/state";
static const char *DISCO_PREFIX = "homeassistant";

#define STATE_INTERVAL_MS 10000UL
#define WIFI_RETRY_MS 10000UL
#define MQTT_RETRY_MS 5000UL

static WiFiClient mqttSock;
static bool mqttUp = false;
static unsigned long lastState = 0;
static unsigned long lastTry = 0;
static char jb[1000];

/* ------------------------------------------------------------------ */
/* newlib compatibility: the prebuilt SDK archive was built against    */
/* newlib 2.4 while the Arduino 4.8.3 toolchain ships newlib 2.1       */
/* ------------------------------------------------------------------ */
extern "C" {
const char *__locale_ctype_ptr(void) { return __ctype_ptr__; }

char *itoa(int value, char *str, int base)
{
    char *p = str;
    char tmp[34];
    int i = 0;
    unsigned int v;
    bool neg = false;
    if (base == 10 && value < 0) { neg = true; v = (unsigned int)(-value); } else v = (unsigned int)value;
    do {
        int d = v % base;
        tmp[i++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        v /= base;
    } while (v);
    if (neg) tmp[i++] = '-';
    while (i) *p++ = tmp[--i];
    *p = '\0';
    return str;
}

char *utoa(unsigned int value, char *str, int base)
{
    char *p = str;
    char tmp[34];
    int i = 0;
    do {
        int d = value % base;
        tmp[i++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        value /= base;
    } while (value);
    while (i) *p++ = tmp[--i];
    *p = '\0';
    return str;
}

/* the Arduino core calls these through C++ (mangled) declarations, the
   prebuilt SDK archive calls them as plain C: provide both symbols */
char *itoa_cxx(int, char *, int) __asm__("_Z4itoaiPci");
char *itoa_cxx(int value, char *str, int base) { return itoa(value, str, base); }

char *utoa_cxx(unsigned int, char *, int) __asm__("_Z4utoajPci");
char *utoa_cxx(unsigned int value, char *str, int base) { return utoa(value, str, base); }
}

/* ------------------------------------------------------------------ */
/* minimal MQTT 3.1.1 client (QoS0 publish, retained flag, LWT)        */
/* ------------------------------------------------------------------ */
static unsigned char pkt[1100];

static int putVarint(unsigned char *b, unsigned int n)
{
    int i = 0;
    do {
        unsigned char d = n % 128;
        n /= 128;
        if (n) d |= 0x80;
        b[i++] = d;
    } while (n);
    return i;
}

static int putStr(unsigned char *b, const char *s)
{
    unsigned int l = strlen(s);
    b[0] = (l >> 8) & 0xFF;
    b[1] = l & 0xFF;
    memcpy(b + 2, s, l);
    return (int)l + 2;
}

/* the AZ3166 WiFiClient puts the socket in non-blocking mode: writes can be
   partial and reads return 0 until data arrives, so both need retry loops */
static bool sendAll(const unsigned char *d, int n)
{
    int sent = 0;
    unsigned long t0 = millis();
    while (sent < n) {
        if (!mqttSock.connected())
            return false;
        int w = (int)mqttSock.write(d + sent, n - sent);
        if (w > 0) {
            sent += w;
            t0 = millis();
            continue;
        }
        if (millis() - t0 > 10000)
            return false;
        delay(5);
    }
    return true;
}

static bool mqttPublish(const char *topic, const char *payload, bool retain)
{
    unsigned int plen = strlen(payload);
    unsigned int vlen = 2 + strlen(topic) + plen;
    pkt[0] = retain ? 0x31 : 0x30;
    int o = 1 + putVarint(pkt + 1, vlen);
    o += putStr(pkt + o, topic);
    memcpy(pkt + o, payload, plen);
    o += plen;
    return sendAll(pkt, o);
}

static bool mqttConnect()
{
    mqttSock.stop();
    if (!mqttSock.connect(MQTT_HOST, MQTT_PORT)) {
        Serial.println("[mqtt] tcp connect failed");
        return false;
    }

    unsigned char body[512];
    int b = 0;
    b += putStr(body + b, "MQTT");
    body[b++] = 4;                                   /* protocol level 3.1.1 */
    body[b++] = 0x02 | 0x04 | 0x20 | 0x80 | 0x40;    /* clean, will, will-retain, user, pass */
    body[b++] = 0;                                   /* keepalive 60s */
    body[b++] = 60;
    b += putStr(body + b, MQTT_CLIENT_ID);
    b += putStr(body + b, AVAIL_TOPIC);
    b += putStr(body + b, "offline");
    b += putStr(body + b, MQTT_USER);
    b += putStr(body + b, MQTT_PASS);

    pkt[0] = 0x10;
    int hl = 1 + putVarint(pkt + 1, b);
    memcpy(pkt + hl, body, b);
    if (!sendAll(pkt, hl + b))
        return false;

    unsigned char r[4] = {0, 0, 0, 0};
    int got = 0;
    unsigned long t0 = millis();
    while (got < 4 && millis() - t0 < 5000) {
        int n = mqttSock.read(r + got, 4 - got);
        if (n > 0)
            got += n;
        else
            delay(10);
    }
    if (got == 4 && r[0] == 0x20) {
        if (r[3] == 0x00) {
            Serial.println("[mqtt] connected");
            return true;
        }
        Serial.print("[mqtt] CONNACK refused, code ");
        Serial.println(r[3]);
        return false;
    }
    Serial.println("[mqtt] CONNACK timeout");
    return false;
}

/* ------------------------------------------------------------------ */
/* Home Assistant discovery                                            */
/* ------------------------------------------------------------------ */
struct Disc {
    const char *field;
    const char *name;
    const char *unit;
    const char *dclass;
    const char *icon;
    bool diagnostic;
};

static const Disc DISCS[] = {
    {"temperature",   "Temperature",             "\xC2\xB0" "C", "temperature", NULL, false},
    {"humidity",      "Humidity",                "%",            "humidity",    NULL, false},
    {"pressure",      "Pressure",                "hPa",          "pressure",    NULL, false},
    {"accel_total",   "Acceleration magnitude",  "g",            NULL,          "mdi:chart-bell-curve", false},
    {"accel_x",       "Acceleration X",          "g",            NULL,          "mdi:axis-x-arrow", false},
    {"accel_y",       "Acceleration Y",          "g",            NULL,          "mdi:axis-y-arrow", false},
    {"accel_z",       "Acceleration Z",          "g",            NULL,          "mdi:axis-z-arrow", false},
    {"gyro_x",        "Gyroscope X",             "\xC2\xB0/s",   NULL,          "mdi:rotate-right", true},
    {"gyro_y",        "Gyroscope Y",             "\xC2\xB0/s",   NULL,          "mdi:rotate-right", true},
    {"gyro_z",        "Gyroscope Z",             "\xC2\xB0/s",   NULL,          "mdi:rotate-right", true},
    {"mag_x",         "Magnetometer X (raw)",    NULL,           NULL,          "mdi:magnet", true},
    {"mag_y",         "Magnetometer Y (raw)",    NULL,           NULL,          "mdi:magnet", true},
    {"mag_z",         "Magnetometer Z (raw)",    NULL,           NULL,          "mdi:magnet", true},
};
#define NDISCS ((int)(sizeof(DISCS) / sizeof(DISCS[0])))

static bool publishDiscovery()
{
    for (int i = 0; i < NDISCS; i++) {
        const Disc &d = DISCS[i];
        char topic[96];
        char unit[64] = "";
        char dcls[64] = "";
        char icon[64] = "";
        char ecat[48] = "";

        snprintf(topic, sizeof(topic), "%s/sensor/az3166_%s/config", DISCO_PREFIX, d.field);
        if (d.unit) snprintf(unit, sizeof(unit), ",\"unit_of_measurement\":\"%s\"", d.unit);
        if (d.dclass) snprintf(dcls, sizeof(dcls), ",\"device_class\":\"%s\"", d.dclass);
        if (d.icon) snprintf(icon, sizeof(icon), ",\"icon\":\"%s\"", d.icon);
        if (d.diagnostic) snprintf(ecat, sizeof(ecat), ",\"entity_category\":\"diagnostic\"");

        snprintf(jb, sizeof(jb),
                 "{\"name\":\"%s\",\"unique_id\":\"az3166_%s\","
                 "\"state_topic\":\"%s\",\"value_template\":\"{{ value_json.%s }}\","
                 "\"availability_topic\":\"%s\",\"payload_available\":\"online\","
                 "\"payload_not_available\":\"offline\",\"state_class\":\"measurement\"%s%s%s%s,"
                 "\"device\":{\"identifiers\":[\"az3166_devkit\"],\"name\":\"AZ3166 DevKit\","
                 "\"manufacturer\":\"MXChip\",\"model\":\"AZ3166 IoT DevKit (STM32F412)\","
                 "\"sw_version\":\"mqtt-node-1.0\"}}",
                 d.name, d.field, STATE_TOPIC, d.field, AVAIL_TOPIC, unit, dcls, icon, ecat);

        if (!mqttPublish(topic, jb, true)) {
            Serial.print("[mqtt] discovery failed at ");
            Serial.println(d.field);
            return false;
        }
        delay(15);
    }
    Serial.print("[mqtt] published ");
    Serial.print(NDISCS);
    Serial.println(" discovery configs");
    return true;
}

/* ------------------------------------------------------------------ */
/* telemetry                                                           */
/* ------------------------------------------------------------------ */
static void publishState()
{
    int ax, ay, az, gx, gy, gz, mx, my, mz;
    getDevKitAcceleratorValue(&ax, &ay, &az);   /* mg */
    getDevKitGyroscopeValue(&gx, &gy, &gz);     /* mdps */
    getDevKitMagnetometerValue(&mx, &my, &mz);  /* raw */

    float t = getDevKitTemperatureValue(0);
    float h = getDevKitHumidityValue();
    float p = getDevKitPressureValue();
    float ag = sqrtf((float)ax * ax + (float)ay * ay + (float)az * az) / 1000.0f;

    snprintf(jb, sizeof(jb),
             "{\"temperature\":%.2f,\"humidity\":%.2f,\"pressure\":%.2f,"
             "\"accel_total\":%.3f,\"accel_x\":%.3f,\"accel_y\":%.3f,\"accel_z\":%.3f,"
             "\"gyro_x\":%.2f,\"gyro_y\":%.2f,\"gyro_z\":%.2f,"
             "\"mag_x\":%d,\"mag_y\":%d,\"mag_z\":%d}",
             t, h, p, ag,
             ax / 1000.0f, ay / 1000.0f, az / 1000.0f,
             gx * 0.004375f, gy * 0.004375f, gz * 0.004375f,
             mx, my, mz);

    if (mqttPublish(STATE_TOPIC, jb, false)) {
        Serial.print("[mqtt] ");
        Serial.println(jb);
    } else {
        mqttUp = false;
    }
}

/* ------------------------------------------------------------------ */
static void showStatus(bool wifi, bool mqtt)
{
    char line[24];
    Screen.print(0, "AZ3166 MQTT");
    if (wifi) {
        IPAddress ip = WiFi.localIP();
        Screen.print(1, ip.get_address());
    } else {
        Screen.print(1, "no Wi-Fi");
    }
    Screen.print(2, mqtt ? "HA: online" : "HA: offline");
    snprintf(line, sizeof(line), "%.1fC  %.0f%%", getDevKitTemperatureValue(0), getDevKitHumidityValue());
    Screen.print(3, line);
}

static bool wifiJoin()
{
    Serial.print("[wifi] joining ");
    Serial.println(WIFI_SSID);
    Screen.print(1, "Wi-Fi...");
    int st = WiFi.begin((char *)WIFI_SSID, WIFI_PASS);
    if (st == WL_CONNECTED) {
        digitalWrite(LED_WIFI, HIGH);
        Serial.print("[wifi] ip ");
        Serial.println(WiFi.localIP().get_address());
        return true;
    }
    digitalWrite(LED_WIFI, LOW);
    Serial.print("[wifi] failed, status ");
    Serial.println(st);
    return false;
}

void setup()
{
    Serial.begin(115200);
    int rc = initIoTDevKit(1);
    Serial.print("[hw] initIoTDevKit rc=");
    Serial.println(rc);
    Screen.print(0, "AZ3166 MQTT");
    Screen.print(2, "Initialized");
}

void loop()
{
    /* Wi-Fi first */
    if (WiFi.status() != WL_CONNECTED) {
        mqttUp = false;
        if (millis() - lastTry > WIFI_RETRY_MS) {
            lastTry = millis();
            wifiJoin();
        }
        showStatus(false, false);
        delay(1000);
        return;
    }
    digitalWrite(LED_WIFI, HIGH);

    /* then MQTT */
    if (!mqttUp || !mqttSock.connected()) {
        mqttUp = false;
        if (millis() - lastTry > MQTT_RETRY_MS) {
            lastTry = millis();
            if (mqttConnect()) {
                if (publishDiscovery()) {
                    mqttPublish(AVAIL_TOPIC, "online", true);
                    mqttUp = true;
                    digitalWrite(LED_AZURE, HIGH);
                    lastState = 0;
                }
            }
        }
        showStatus(true, false);
        delay(500);
        return;
    }

    if (millis() - lastState > STATE_INTERVAL_MS) {
        lastState = millis();
        publishState();
    }

    showStatus(true, mqttUp);
    delay(200);
}
