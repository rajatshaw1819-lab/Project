#include <WiFi.h>
#include <WiFiManager.h>          // 🔹 ADDED
#include <Firebase_ESP_Client.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <LiquidCrystal_I2C.h>

#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

/********* FIREBASE *********/
#define API_KEY         "AIzaSyDabqFzWBUy_qrccSp671CFiMQ1I53hPaI"
#define DATABASE_URL    "https://iot-irrigation2526-default-rtdb.firebaseio.com/"

#define USER_EMAIL     "technkit17@gmail.com"
#define USER_PASSWORD  "123456"

/********* PINS *********/
#define TDS_PIN        39
#define TURBIDITY_PIN  36
#define ONE_WIRE_BUS   32
#define BUZZER_PIN     4

/* ========= OBJECTS ========= */
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

LiquidCrystal_I2C lcd(0x27, 16, 2);

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

WiFiManager wm;   // 🔹 WiFiManager object

/* ========= CALIBRATION ========= */
float tdsFactor = 1.0;
float turbOffset = 0.0;
float tempOffset = 0.0;

/* ========= TIMING ========= */
unsigned long lastCalibRead = 0;
unsigned long lastSend = 0;

/* ========= SETUP ========= */
void setup() {
  Serial.begin(115200);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  lcd.begin();
  lcd.backlight();
  lcd.setCursor(0,0);
  lcd.print("Water Quality");

  /* ===== WiFiManager ===== */
  lcd.setCursor(0,1);
  lcd.print("WiFi Config...");

  wm.setConfigPortalTimeout(180); // 3 minutes

  bool res = wm.autoConnect("WaterQuality_AP", "12345678");

  if (!res) {
    lcd.clear();
    lcd.print("WiFi Failed");
    delay(3000);
    ESP.restart();
  }

  lcd.clear();
  lcd.print("WiFi Connected");

  Serial.println(WiFi.localIP());

  /* ===== Firebase config ===== */
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  sensors.begin();
}

/* ========= READ CALIBRATION ========= */
void readCalibration() {
  if (Firebase.RTDB.getFloat(&fbdo, "/waterQuality/calibration/tdsFactor"))
    tdsFactor = fbdo.floatData();

  if (Firebase.RTDB.getFloat(&fbdo, "/waterQuality/calibration/turbOffset"))
    turbOffset = fbdo.floatData();

  if (Firebase.RTDB.getFloat(&fbdo, "/waterQuality/calibration/tempOffset"))
    tempOffset = fbdo.floatData();
}

/* ========= LOOP ========= */
void loop() {

  /* Read calibration every 20s */
  if (millis() - lastCalibRead > 20000) {
    readCalibration();
    lastCalibRead = millis();
  }

  /* Read & send data every 5s */
  if (millis() - lastSend > 5000 && Firebase.ready()) {

    /* --- RAW SENSOR READINGS --- */
    int tdsADC = analogRead(TDS_PIN);
    int turbADC = analogRead(TURBIDITY_PIN);

    sensors.requestTemperatures();
    float tempRaw = sensors.getTempCByIndex(0);

    float tdsRaw = (tdsADC / 4095.0) * 1000.0;
    float turbRaw = (turbADC / 4095.0) * 10.0;

    /* --- CALIBRATED VALUES --- */
    float tdsCal = tdsRaw * tdsFactor;
    float turbCal = turbRaw + turbOffset;
    float tempCal = tempRaw + tempOffset;

    /* --- LCD DISPLAY --- */
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("TDS:");
    lcd.print((int)tdsCal);
    lcd.print("ppm");

    lcd.setCursor(0,1);
    lcd.print("T:");
    lcd.print(tempCal,1);
    lcd.print((char)223);
    lcd.print("C");

    /* --- BUZZER ALERT --- */
    if (tdsCal > 500 || turbCal > 5) {
      digitalWrite(BUZZER_PIN, HIGH);
    } else {
      digitalWrite(BUZZER_PIN, LOW);
    }

    /* --- FIREBASE UPLOAD --- */
    FirebaseJson json;

    json.set("raw/tds", tdsRaw);
    json.set("raw/turbidity", turbRaw);
    json.set("raw/temperature", tempRaw);

    json.set("calibrated/tds", tdsCal);
    json.set("calibrated/turbidity", turbCal);
    json.set("calibrated/temperature", tempCal);

    json.set("timestamp", millis());

    if (!Firebase.RTDB.setJSON(&fbdo, "/waterQuality", &json)) {
      Serial.println(fbdo.errorReason());
    }

    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Turbidity:");
    lcd.setCursor(0,1);
    lcd.print(turbCal,0);
    lcd.print(" NTU");

    lastSend = millis();
  }
}
