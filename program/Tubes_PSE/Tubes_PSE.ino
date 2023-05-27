#if defined(ESP32)
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#endif
#include <Firebase_ESP_Client.h>
#include "EmonLib.h"
#include "time.h"
#include "ThingSpeak.h"
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

//thingspeak
#define SECRET_CH_ID 1806610      // replace 0000000 with your channel number
#define SECRET_WRITE_APIKEY "P91FN4Z8U0W3PR4L"   // replace XYZ with your channel write API Key
#define WIFI_SSID "Blynk"
#define WIFI_PASSWORD "D*n*ng*l"

//firebase
#define API_KEY "AIzaSyCxeYNGg0JktGUM9vARwmdYAe1qZRDYTuQ"
#define DATABASE_URL "https://proyek-pse-genap-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define USER_EMAIL "D*n*ng*ldi7@gm*il.com"
#define USER_PASSWORD "******" //ganti dengan password anda

EnergyMonitor emon1;
WiFiClient  client;
LiquidCrystal_I2C lcd = LiquidCrystal_I2C(0x27, 20, 4);
unsigned long myChannelNumber = SECRET_CH_ID;
const char * myWriteAPIKey = SECRET_WRITE_APIKEY;
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 0;
const int   daylightOffset_sec = 3600 * 7;
unsigned long sendDataPrevMillis = 0;
float totalIDR = 0;
float totalKWH = 0;

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
struct Data
{
  float totalCostPerSecond;
  float totalInKiloWatts;
};

void setup()
{
  Serial.begin(115200);
  emon1.voltage(34, 234.26, 1.7);  //input pin, calibration, phase_shift, default 234.26
  emon1.current(35, 27.775);       // input pin, calibration.
  lcd.begin();
  lcd.clear();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print("..");
    delay(300);
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();
  
  // Init and get the time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  printLocalTime();

  Serial.printf("Firebase Client v%s\n\n", FIREBASE_CLIENT_VERSION);
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  config.token_status_callback = tokenStatusCallback; // see addons/TokenHelper.h
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  //membaca data harga di firebase
  Firebase.begin(&config, &auth);
  Firebase.RTDB.getFloat(&fbdo, "/Pengeluaran/harga");
  totalIDR = fbdo.floatData();
  Serial.print("Pengeluaran rupiah:");
  Serial.println(totalIDR);

  //membaca data pengeluaran meteran di firebase
  Firebase.RTDB.getFloat(&fbdo, "/Pengeluaran/meteran");
  totalKWH = fbdo.floatData();
  Serial.print("Pengeluaran meteran:");
  Serial.println(totalKWH);
  
  //disconnect WiFi as it's no longer needed
  Firebase.reconnectWiFi(true);
  
  //thingspeak
  WiFi.mode(WIFI_STA);
  ThingSpeak.begin(client);  // Initialize ThingSpeak
}
void loop()
{
  emon1.calcVI(20, 2000);        // Calculate all. No.of half wavelengths (crossings), time-out
  float irms = emon1.Irms;// * 0.327398;
  //float irms = Irms1 - 0.22583;
  float supplyVoltage   = emon1.Vrms;             //extract Vrms into Variable
  if (supplyVoltage <= 80 || supplyVoltage >= 300)
  {
    supplyVoltage = 0.0;
  }
  Serial.println(irms);
  //Serial.println(supplyVoltage);
  printLocalTime();
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time");
    return;
  }
  char timeDay[10];
  strftime(timeDay, 10, "%A", &timeinfo);
  char timeHour[3];
  strftime(timeHour, 3, "%H", &timeinfo);
  char timeMinute[3];
  strftime(timeMinute, 3, "%M", &timeinfo);
  char timeSecond[3];
  strftime(timeSecond, 3, "%S", &timeinfo);
  char timeDate[3];
  strftime(timeDate, 3, "%d", &timeinfo);
  char timeMonth[15];
  strftime(timeMonth, 15, "%B", &timeinfo);
  char timeYear[5];
  strftime(timeYear, 5, "%Y", &timeinfo);
  String kalender = String() + timeDay + ", " + timeDate + " " + timeMonth + " " + timeYear;
  String waktu = String() + timeHour + ":" + timeMinute;
  
  //tampil waktu
  if ((timeinfo.tm_mday == 28) && (timeinfo.tm_hour == 23) && ( timeinfo.tm_min == 0) && (timeinfo.tm_sec <= 10))
  {
    totalIDR = 0; //mereset pengeluaran
    totalKWH = 0; //mereset meteraan
  }

  lcd.print(kalender);
  lcd.setCursor(0, 1); lcd.print(waktu);
  lcd.setCursor(0, 2); lcd.print("Arus: "); lcd.print(irms); lcd.print("A");
  lcd.setCursor(0, 3); lcd.print("Tegangan: "); lcd.print(supplyVoltage); lcd.print("V");

  float S = supplyVoltage * irms; //VA
  float P = S * 0.8; //Watt
  float Q = sqrt(((S * S) - (P * P))); //VAR
  float pf = P / S; //power faktor

  Data RoC = costElectricityCounter(P);
  totalIDR = totalIDR + RoC.totalCostPerSecond;
  totalKWH = totalKWH + RoC.totalInKiloWatts;
  ThingSpeak.setField(4, totalKWH);
  ThingSpeak.setField(5, totalIDR);
  ThingSpeak.setField(6, irms);
  ThingSpeak.setField(7, supplyVoltage);

//    if (Firebase.ready() && (millis() - sendDataPrevMillis > 5000 || sendDataPrevMillis == 0))
//    {
//      sendDataPrevMillis = millis();
//      FirebaseJson json;
//      json.add("harga", totalIDR);
//      json.add("meteran", totalKWH);
//      json.add("arus", irms);
//      json.add("tegangan", supplyVoltage);
//      json.add("daya_aktif", P);
//      json.add("kalender", kalender);
//      json.add("waktu", waktu);
//      Serial.printf("Set json... %s\n", Firebase.RTDB.setJSON(&fbdo, "/Pengeluaran", &json) ? "ok" : fbdo.errorReason().c_str());
//      ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
//    }
}
Data costElectricityCounter(float watts)
{
  /*KWH = (wattxjam):1000
    BIAYA = KWH x 1444.7*/

  float hour = 1;
  float hourInSeconds = 3600;
  float totalWattPerSecond = watts * (hour / hourInSeconds); // watt * second
  float totalInKiloWatts = totalWattPerSecond / 1000; // watt : kilowatt
  float totalCostPerSecond = totalInKiloWatts * 1444.70; // cost per 1 kwh
  Data result;

  result.totalCostPerSecond = totalCostPerSecond;
  result.totalInKiloWatts = totalInKiloWatts;
  return result;
}
void printLocalTime()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time");
    return;
  }
}
