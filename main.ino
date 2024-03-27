#include <Arduino.h>
#if defined(ESP32)
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#endif
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include <Stepper.h>
#include "time.h"
#include <LiquidCrystal_I2C.h>
#include <FastLED.h>

#define WIFI_SSID "WIFI_USERNAME" //hotspot works as well
#define WIFI_PASSWORD "WIFI_PASSWORD" //hotspot works as well

//Firebase
#define API_KEY "API_KEY"
#define DATABASE_URL "DATABASE_URL"

#define USER_EMAIL "ryalisuchir@gmail.com"
#define USER_PASSWORD "USER_PASSWORD"

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

unsigned long sendDataPrevMillis = 0;
int intValue;
bool signupOK = true;

//Stepper
const int steps_per_rev = 2048;
const int servoSpeed = 10;

#define BUZZER_PIN 33
#define BUZZER_CHANNEL 0

#define IN1_1 13
#define IN2_1 12
#define IN3_1 14
#define IN4_1 27
Stepper motor1(steps_per_rev, IN1_1, IN2_1, IN3_1, IN4_1);

#define IN1_2 25
#define IN2_2 32
#define IN3_2 15
#define IN4_2 2
Stepper motor2(steps_per_rev, IN1_2, IN2_2, IN3_2, IN4_2);

#define IN1_3 5
#define IN2_3 18
#define IN3_3 19
#define IN4_3 4
Stepper motor3(steps_per_rev, IN1_2, IN2_2, IN3_2, IN4_2);

//LCD
int lcdColumns = 16;
int lcdRows = 2;
LiquidCrystal_I2C lcd(0x27, lcdColumns, lcdRows);

//Time
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -3600 * 6;
const int daylightOffset_sec = 3600;

#define NUM_LEDS 47
CRGB leds[NUM_LEDS];

void setup() {

  Serial.begin(115200);

  // //Buzzer
  pinMode(BUZZER_PIN, OUTPUT);

  //Stepper
  motor1.setSpeed(servoSpeed);
  motor2.setSpeed(servoSpeed);
  motor3.setSpeed(servoSpeed);

  //Wifi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();

  //Firebase
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  config.token_status_callback = tokenStatusCallback;
  Firebase.reconnectNetwork(true);
  fbdo.setBSSLBufferSize(4096 /* Rx buffer size in bytes from 512 - 16384 */, 1024 /* Tx buffer size in bytes from 512 - 16384 */);

  Firebase.begin(&config, &auth);

  Firebase.setDoubleDigits(5);

  //LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Next Dosage at: ");
  lcd.setCursor(0, 1);
  lcd.print(getNextDispenseMessage(getPointer()));

  // FastLED.addLeds<NEOPIXEL, 26>(leds, NUM_LEDS);
  FastLED.addLeds<NEOPIXEL, 23>(leds, NUM_LEDS);
  FastLED.setBrightness(20);


  //Time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
}

void loop() {
  delay(1000);
  int pointer = getPointer();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Next dosage at: ");
  lcd.setCursor(0, 1);
  lcd.print(getNextDispenseMessage(pointer));

  // Serial.print(pointer);
  Serial.println(getNextPills(pointer));

  unsigned long nextDispenseTime = getNextDispenseEpochTime(pointer);
  if (nextDispenseTime < 1) {
    return;
  }
  turnOff();

  if (nextDispenseTime < getEpochTime()) {
    Serial.print("the time is now: ");
    Serial.println(getEpochTime());
    lcd.clear();
    lcd.print("Dispensing Pills");

    tone(BUZZER_PIN, 1000);

    int pills = getNextPills(pointer);
    Serial.print("Pills to dispense are ");
    Serial.println(pills);
    turnRed();
    switch (pills) {
      case 1:
        motor1.step(2048);  // 2048 before
        break;
      case 2:
        motor2.step(2048);
        break;
      case 3:
        motor3.step(2048);
        break;
      case 12:
        motor1.step(2048);
        motor2.step(2048);
        break;
      case 13:
        motor1.step(2048);
        motor3.step(2048);
        break;
      case 23:
        motor2.step(2048);
        motor3.step(2048);
        break;
      case 123:
        motor1.step(2080);
        motor2.step(2170);
        motor3.step(2210);
        break;
    }

    digitalWrite(IN1_1, LOW);
    digitalWrite(IN2_1, LOW);
    digitalWrite(IN3_1, LOW);
    digitalWrite(IN4_1, LOW);

    digitalWrite(IN1_2, LOW);
    digitalWrite(IN2_2, LOW);
    digitalWrite(IN3_2, LOW);
    digitalWrite(IN4_2, LOW);

    digitalWrite(IN1_3, LOW);
    digitalWrite(IN2_3, LOW);
    digitalWrite(IN3_3, LOW);
    digitalWrite(IN4_3, LOW);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Next Dosage At: ");
    lcd.setCursor(0, 1);
    lcd.print(getNextDispenseMessage(pointer + 1));
    setPointer(pointer + 1);

    turnGreen();
    turnOff();
    noTone(BUZZER_PIN);
    //delay(10000);
  } else {
    Serial.print("Time untill next dispense: ");
    Serial.println(nextDispenseTime - getEpochTime());
    Serial.println("----------------------------------");
  }

  delay(1000);
}

int getPointer() {
  if (Firebase.ready() && signupOK) {
    if (Firebase.RTDB.getInt(&fbdo, "/test/pointer")) {
      if (fbdo.dataType() == "int") {
        int pointer = fbdo.intData();
        return pointer;
      }
    } else {
      Serial.println(fbdo.errorReason());
      return -1;
    }
  }
}

void setPointer(int value) {
  if (Firebase.ready() && signupOK) {
    if (Firebase.RTDB.setInt(&fbdo, "/test/pointer", value)) {
    }
  }
}

unsigned long getNextDispenseEpochTime(int pointer) {
  if (Firebase.ready() && signupOK) {
    if (Firebase.RTDB.getArray(&fbdo, "/test/time")) {
      FirebaseJsonArray &arr = fbdo.jsonArray();
      String in = String(arr.raw());


      in.remove((in.length() - 1), 2);
      in.remove(0, 1);
      in.replace(']', ' ');
      in.replace('"', ' ');
      in.replace(' , ', '_');

      String strs[20];
      int StringCount = 0;


      while (in.length() > 0) {
        int index = in.indexOf(',');
        if (index == -1)  // No space found
        {
          strs[StringCount++] = in;
          break;
        } else {
          strs[StringCount++] = in.substring(0, index);
          in = in.substring(index + 1);
        }
      }

      return strtoul(strs[pointer].c_str(), NULL, 10);

    } else {
      Serial.println(fbdo.errorReason());
    }
  }
  return 0;
}

String getNextDispenseMessage(int pointer) {
  if (Firebase.ready() && signupOK) {
    if (Firebase.RTDB.getArray(&fbdo, "/test/dateTimeArray")) {
      FirebaseJsonArray &arr = fbdo.jsonArray();
      String in = String(arr.raw());

      in.remove((in.length() - 1), 2);
      in.remove(0, 1);
      in.replace('"', ' ');

      String strs[20];
      int StringCount = 0;

      while (in.length() > 0) {
        int index = in.indexOf(',');
        if (index == -1)  // No space found
        {
          strs[StringCount++] = in;
          break;
        } else {
          strs[StringCount++] = in.substring(0, index);
          in = in.substring(index + 1);
        }
      }

      String message = strs[pointer];
      message.remove(0, 1);
      if (message != "") {
        return message;
      } else {
        return "no dosage set...";
      }


    } else {
      Serial.println(fbdo.errorReason());
    }
  }
  return "";
}

int getNextPills(int pointer) {
  if (Firebase.ready() && signupOK) {
    if (Firebase.RTDB.getArray(&fbdo, "/test/pills")) {
      FirebaseJsonArray &arr = fbdo.jsonArray();
      String in = String(arr.raw());


      in.remove((in.length() - 1), 2);
      in.remove(0, 1);
      in.replace('"', ' ');

      String strs[20];
      int StringCount = 0;

      Serial.println(in);

      while (in.length() > 0) {
        int index = in.indexOf(',');
        if (index == -1)  // No space found
        {
          strs[StringCount++] = in;
          break;
        } else {
          strs[StringCount++] = in.substring(0, index);
          in = in.substring(index + 1);
        }
      }

      return atoi(strs[pointer].c_str());

    } else {
      Serial.println(fbdo.errorReason());
    }
  }
  return -1;
}

//Time
unsigned long getEpochTime() {
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    //Serial.println("Failed to obtain time");
    return (0);
  }
  time(&now);
  return now;
}

//LEDS
void turnRed() {
  FastLED.setBrightness(20);
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB::Red;
  }
  FastLED.show();
}

void turnGreen() {
  FastLED.setBrightness(5);
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB::Green;
  }
  FastLED.show();
}

void turnOff() {
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB::Black;
  }
  FastLED.show();
}
