/*
 * AZ3166 Selbsttest - beweist, was die Hardware wirklich kann.
 * Laeuft ohne WLAN-Zugangsdaten und ohne Broker.
 *
 * Seriell 115200. Ausgabe:
 *   FW-Version, MAC, WLAN-Scan (beweist 2,4-GHz-Radio)
 *   HTS221 Temp/Feuchte, LPS22HB Druck, LSM6DSL Accel+Gyro, LIS2MDL Magnetfeld
 *   Tasten, RGB-LED-Sequenz, OLED
 */
#include <string.h>
#include <stdio.h>

#include "AZ3166WiFi.h"
#include "OledDisplay.h"
#include "RGB_LED.h"
#include "DevI2C.h"
#include "HTS221Sensor.h"
#include "LPS22HBSensor.h"
#include "LSM6DSLSensor.h"
#include "LIS2MDLSensor.h"

#include "newlib_shim.h"   /* __locale_ctype_ptr + itoa/utoa for the SDK archives */

static DevI2C        *i2c = NULL;
static HTS221Sensor  *sTH = NULL;
static LPS22HBSensor *sBaro = NULL;
static LSM6DSLSensor *sMotion = NULL;
static LIS2MDLSensor *sMag = NULL;
static RGB_LED        led;

static void hr(const char *t) { Serial.printf("\r\n--- %s ---\r\n", t); delay(80); }

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\r\n\r\n########## AZ3166 SELFTEST ##########");

    Screen.init();
    Screen.clean();
    Screen.print(0, "SELFTEST");
    Screen.print(1, "startet...");

    hr("System");
    Serial.printf("firmwareVersion : %s\r\n", WiFi.firmwareVersion());

    unsigned char mac[6] = {0};
    WiFi.macAddress(mac);
    Serial.printf("WiFi MAC        : %02X:%02X:%02X:%02X:%02X:%02X\r\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    hr("WLAN-Scan (2,4 GHz)");
    Screen.print(2, "WLAN scan...");
    int n = WiFi.scanNetworks();
    Serial.printf("Netzwerke gefunden: %d\r\n", n);
    for (int i = 0; i < n && i < 10; i++) {
        const char *s = WiFi.SSID((unsigned char)i);
        Serial.printf("  [%d] rssi=%d enc=%d ssid=", i, WiFi.RSSI((unsigned char)i),
                      WiFi.encryptionType((unsigned char)i));
        delay(60);
        Serial.print(s ? s : "(null)");
        delay(60);
        Serial.printf("  len=%d\r\n", s ? (int)strlen(s) : -1);
        delay(120);
    }

    hr("I2C / Sensoren");
    i2c = new DevI2C(D14, D15);
    if (!i2c) { Serial.println("I2C: FEHLER"); }

    sTH = new HTS221Sensor(*i2c);
    int r1 = sTH->init(NULL);
    Serial.printf("HTS221  init: %d %s\r\n", r1, r1 == 0 ? "OK" : "FEHLER");

    sBaro = new LPS22HBSensor(*i2c);
    int r2 = sBaro->init(NULL);
    Serial.printf("LPS22HB init: %d %s\r\n", r2, r2 == 0 ? "OK" : "FEHLER");

    sMotion = new LSM6DSLSensor(*i2c, D4, D5);
    int m1 = sMotion->init(NULL); sMotion->enableAccelerator(); sMotion->enableGyroscope();
    Serial.printf("LSM6DSL init: %d %s\r\n", m1, m1 == 0 ? "OK" : "FEHLER");

    sMag = new LIS2MDLSensor(*i2c);
    int m2 = sMag->init(NULL);
    Serial.printf("LIS2MDL init: %d %s\r\n", m2, m2 == 0 ? "OK" : "FEHLER");

    hr("RGB-LED: rot / gruen / blau");
    led.setColor(255, 0, 0);   delay(400);
    led.setColor(0, 255, 0);   delay(400);
    led.setColor(0, 0, 255);   delay(400);
    led.turnOff();

    hr("Tasten (A/B, aktiv LOW)");
#ifdef USER_BUTTON_A
    pinMode(USER_BUTTON_A, INPUT);
    pinMode(USER_BUTTON_B, INPUT);
    Serial.printf("A=%d B=%d  (gedrueckt = 0)\r\n",
                  digitalRead(USER_BUTTON_A), digitalRead(USER_BUTTON_B));
#else
    Serial.println("USER_BUTTON_A nicht definiert");
#endif

    Screen.clean();
    Screen.print(0, "SELFTEST ok");
}

void loop() {
    float t = NAN, h = NAN, p = NAN;
    int axes[3] = {0, 0, 0};
    int gyr[3] = {0, 0, 0};
    int mag[3] = {0, 0, 0};

    if (sTH)     { sTH->getTemperature(&t); sTH->getHumidity(&h); }
    if (sBaro)   { sBaro->getPressure(&p); }
    if (sMotion) { sMotion->getXAxes(axes); sMotion->getGAxes(gyr); }
    if (sMag)    { sMag->getMAxes(mag); }

    float ax = axes[0] / 1000.0f, ay = axes[1] / 1000.0f, az = axes[2] / 1000.0f;
    float g  = sqrtf(ax * ax + ay * ay + az * az);

    Serial.printf("T=%.2fC  RH=%.2f%%  P=%.2fhPa  |a|=%.3fg  "
                  "acc=(%.2f,%.2f,%.2f)  gyr=(%d,%d,%d)  mag=(%d,%d,%d)\r\n",
                  t, h, p, g, ax, ay, az, gyr[0], gyr[1], gyr[2],
                  mag[0], mag[1], mag[2]);

    char l0[26], l1[26];
    snprintf(l0, sizeof(l0), "%.1fC %.0f%% %.0fhPa", t, h, p);
    snprintf(l1, sizeof(l1), "%.2fg", g);
    Screen.print(0, l0);
    Screen.print(1, l1);

    delay(2000);
}
