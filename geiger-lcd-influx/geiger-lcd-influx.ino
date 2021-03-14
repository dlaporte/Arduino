#include <SPI.h>
#include <LiquidCrystal_I2C.h>
#include <ESP8266WiFi.h>
#include <InfluxDb.h>
#include <RunningAverage.h>

#define INFLUXDB_HOST ""
#define WIFI_SSID ""
#define WIFI_PASS ""
#define DATABASE ""
#define MEASUREMENT ""
#define DEVICE ""
#define ID "Geiger_Counter"

// ESP8266-based geiger counter with display and InfluxDB export
// David LaPorte, 14 March 2021
//
// Interfaces with a DIY geiger counter available at RH Electronics (link below)
// or from AliExpress
//
// Based in part on code by RH Electronics:
//
// https://rhelectronics.net/store/radiation-detector-geiger-counter-diy-kit-second-edition.html
//
// and Mr. Cactus:
//
// http://cactusprojects.com/iot-geiger/
//
// modified to use an ESP8266 and an I2C display

// I2C address of 16x2 LCD, determine by running an i2c scanning, eg:
// https://gist.github.com/nadavmatalon/a8025ae948730a135ae7bd33cc804468

#define I2C_ADDR 0x3F

// set to 0 to reduce logged events
#define DEBUG 1

//Logging period in milliseconds, recommended value 15000-60000.
#define LOG_PERIOD 60000
//Maximum logging period
#define MAX_PERIOD 60000

// GPIO pin for piezo buzzer
#define BUZZER 2
// GPIO pin for geiger counter
#define GEIGER 13

#define ALARM_THRESH 40

int loop_count = 0;
int cpm = 0;
int count = 0;
int cpm_max = 0;
int cpm_1hr_avg = 0;
int cpm_ravg = 0;
int multiplier = MAX_PERIOD / LOG_PERIOD;;
int wifi_status;

unsigned long current_millis;
unsigned long previous_millis; //variable for time measurement

void ICACHE_RAM_ATTR impulse() {
  count++;
  if (DEBUG) {
    Serial.println("got one");
  }
}

Influxdb influx(INFLUXDB_HOST);
LiquidCrystal_I2C lcd(I2C_ADDR, 16, 2);
RunningAverage raMinute(60);

void setup() {
  Serial.begin(9600);
  delay(1000);
  Serial.println("");
  Serial.println("");
  Serial.println("Setup Routine of ESP8266 Geiger Counter");

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);

  lcd.print("  LaPorte Inc. ");
  lcd.setCursor(0, 1);
  lcd.print(" Geiger Counter ");
  delay(2000);
  cleanDisplay();

  count = 0;
  cpm = 0;

  attachInterrupt(digitalPinToInterrupt(GEIGER), impulse, FALLING);

  raMinute.clear();

  influx.setDb(DATABASE);

  wifi_status = WiFi.status();
  if (wifi_status != WL_CONNECTED) {
    new_connection();
  } else {
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP());
    delay(2000);
    cleanDisplay();
  }

  if (DEBUG) {
    Serial.println("Setup Complete.");
  }

  lcd.setCursor(0, 0);
  lcd.print("CPM:");
  lcd.setCursor(0, 1);
  lcd.print("uSv/hr:");

}

void loop() {
  current_millis = millis();
  if (current_millis - previous_millis > LOG_PERIOD) {
    cpm = count * multiplier;

    raMinute.addValue(cpm);
    cpm_ravg = raMinute.getAverage();

    if (cpm_ravg > ALARM_THRESH) {
      tone(BUZZER, 1000);
    } else {
      noTone(BUZZER);
    }

    if (cpm > cpm_max) {
      cpm_max = cpm;
    }

    Serial.print("CPM: ");
    Serial.println(cpm);

    lcd.setCursor(5, 0);
    lcd.print(cpm); //Display CPM
    lcd.setCursor(8, 1);
    lcd.print(cpm_ravg * 0.0057);

    Serial.println("Attempting to write to DB");
    count = 0;

    InfluxData row(MEASUREMENT);
    row.addTag("Device", DEVICE);
    row.addTag("ID", ID);
    row.addValue("CPM", cpm);
    row.addValue("loop_count", loop_count);
    row.addValue("RandomValue", random(0, 100));

    wifi_status = WiFi.status();
    while ( wifi_status != WL_CONNECTED ) {
      new_connection();
    }

    influx.write(row);

    if (DEBUG) {
      Serial.println("Wrote Data.");
    }

    previous_millis = current_millis;
    loop_count++;
  }
}

void cleanDisplay () {
  lcd.clear();
  lcd.setCursor(0, 0);
}

void new_connection() {

  wifi_status = WiFi.status();

  if (wifi_status != WL_CONNECTED) {

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    int loops = 0;
    int retries = 0;

    while (wifi_status != WL_CONNECTED) {
      retries++;
      if ( retries == 300 ) {
        if (DEBUG) {
          Serial.println("No connection after 300 steps, powercycling the WiFi radio. I have seen this work when the connection is unstable");
        }
        WiFi.disconnect();
        delay( 10 );
        WiFi.forceSleepBegin();
        delay( 10 );
        WiFi.forceSleepWake();
        delay( 10 );
        WiFi.begin( WIFI_SSID, WIFI_PASS );
      }
      if ( retries == 600 ) {
        if (DEBUG) {
          Serial.println("No connection after 600 steps. WiFi connection failed, disabled WiFi and waiting for a minute");
        }
        WiFi.disconnect( true );
        delay( 1 );
        WiFi.mode( WIFI_OFF );
        WiFi.forceSleepBegin();
        delay( 10 );
        retries = 0;

        if ( loops == 3 ) {
          if (DEBUG) {
            Serial.println("That was 3 loops, still no connection so let's go to deep sleep for 2 minutes");
          }
          Serial.flush();
          ESP.deepSleep(120000000, WAKE_RF_DISABLED);
        }
      }
      delay(50);
      wifi_status = WiFi.status();
    }

    wifi_status = WiFi.status();
    Serial.print("WiFi connected, IP address: ");
    Serial.println(WiFi.localIP());

    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP());
    delay(2000);
    cleanDisplay();
  }
}
