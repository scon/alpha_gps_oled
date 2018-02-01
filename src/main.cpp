#include <Arduino.h>

//WiFi libs
#include <ESP8266WiFi.h>
#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic

//ADS 1115 libs
#include <Adafruit_ADS1015.h>

//I2C libs
#include <Wire.h>

// GPS libs
#include <TinyGPS++.h>

// The TinyGPS++ object
TinyGPSPlus gps;

//GeoHash libs
#include <arduino-geohash.h>
GeoHash hasher(8);

// Wifi Config
char MEASUREMENT_NAME[34] = "alphasense";
const char* AutoConnectAPName = "AutoConnectAP";
const char* AutoConnectAPPW = "password";

// OLED libs
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#define OLED_RESET 4
Adafruit_SSD1306 display(LED_BUILTIN);

// Config Messung
const int MessInterval = 5000; // Zeit in ms zwischen den einzelnen gemittelten Messwerten
const int Messwerte_Mittel = 100; // Anzahl der Messungen, die zu einem einzelnen Messwert gemittelt werden
const int MessDelay = MessInterval / Messwerte_Mittel; /* Pause zwischen Messungen, die zu einem Messwert gemittelt werden -> alle "MessDelay" ms ein Messwert,
bis Messwerte_Mittel mal gemessen wurde, dann wird ein Mittelwert gebildet und ausgegeben. */
unsigned long time;
int WarmUp = 10000;

// Sensor Config
const char* SN1 = "NO2_WE";
const char* SN2 = "O3_WE";
const char* SN3 = "NO_WE";
const char* TEMP = "PT1000";
const char* SN1_AE = "NO2_AE";
const char* SN2_AE = "O3_AE";
const char* SN3_AE = "NO_AE";

// Server Config
const char* InfluxDB_Server_IP = "130.149.67.141";
const int InfluxDB_Server_Port = 8086;
const char* InfluxDB_Database = "MESSCONTAINER";

//GPS Variablen
String latitude, longitude, Geohash, Position;


// Initialisiert WiFiClient als "client"
WiFiClient client;
// Initialisiert I2C ADS1115 als "ads"; PORTS am ESP8266: SDA = D2, SCL = D1
Adafruit_ADS1115 ads_A(0x48);
Adafruit_ADS1115 ads_B(0x49);

// Umrechnung der 16Bit Werte des ADS1115 mit dem entsprechenden GAIN abhängigen Faktor
float Umrechnungsfaktor;
String Daten;

// String getGPS(){
//   gps.encode(Serial.read());
//     if(gps.location.isUpdated()){
//       Geohash = hasher.encode(gps.location.lat(), gps.location.lng());
//       const char* geohash = hasher.encode(gps.location.lat(), gps.location.lng());
//       Serial.println(geohash);
//       latitude = String(gps.location.lat(),6);
//       longitude = String(gps.location.lng(),6);
//       String GPSString = "geohash=" + String(geohash)+" lat=" + latitude + ",lng=" + longitude;
//       Serial.println(GPSString);
//       display.println(GPSString);
//       return GPSString;
//     }
//   }


float getUmrechnungsfaktor(){
  float Faktor;
  if(ads_A.getGain()==GAIN_TWOTHIRDS){
    Faktor = 0.1875;
  }
  if(ads_A.getGain()==GAIN_ONE){
    Faktor = 0.125;
  }
  if(ads_A.getGain()==GAIN_TWO){
    Faktor = 0.0625;
  }
  if(ads_A.getGain()==GAIN_FOUR){
    Faktor = 0.03125;
  }
  return Faktor;
}
void WiFiStart(){
  /* Baut Verbindung mit dem letzten bekannten WiFi auf, wenn nicht vorhanden
  wird eigener AP erstellt -> IP: 192.168.4.1*/
  WiFiManager wifiManager;
  wifiManager.autoConnect(AutoConnectAPName, AutoConnectAPPW);
}
void Verbindungstest(){
  // Verbindungstest zum InfluxSB Server
  int conState = client.connect(InfluxDB_Server_IP, InfluxDB_Server_Port);
  // Verbindung erfolgreich
  if (conState > 0) {
    Serial.println("Verbindung zum InfluxDB Server hergestellt");
    client.stop();
  }
  //Verbindung nicht erfolgreich
  else {
    Serial.print("Keine Verbindung zum InfluxDB Server, Error #");
    Serial.println(conState);
  }
}
String Messung(String Postionsstring){
  float SN1_value, SN2_value, SN3_value, Temp_value;
  float SN1_Integral = 0;
  float SN2_Integral = 0;
  float SN3_Integral = 0;
  float TEMP_Integral = 0;

  float SN1_AE_value,SN2_AE_value,SN3_AE_value;
  float SN1_AE_Integral = 0;
  float SN2_AE_Integral = 0;
  float SN3_AE_Integral = 0;

  //Messung über i = Messwerte_Mittel Messwerte
  for (int i = 0; i < Messwerte_Mittel; i++) {
    // Abrufen der Analogeinganswerte am ADS1115
    // Abrufen der an den Pins anliegenden Werte &
    //Integration über Anzahl der zu mittelnden Messwerte

    //ADC_A
    SN1_AE_Integral+=ads_A.readADC_SingleEnded(1);  // NO2   Aux 1
    SN2_AE_Integral+=ads_A.readADC_SingleEnded(2);  // O3/NO Aux 2
    SN3_AE_Integral+=ads_A.readADC_SingleEnded(3); // NO    Aux 3
    TEMP_Integral+=ads_A.readADC_SingleEnded(0);  // PT+

    //ADC_B
    SN1_Integral+=ads_B.readADC_SingleEnded(1); // NO2   Work 1
    SN2_Integral+=ads_B.readADC_SingleEnded(2); // O3/NO Work 2
    SN3_Integral+=ads_B.readADC_SingleEnded(3); // NO    Work 3

    delay(15);
  }

  SN1_Integral = SN1_Integral / Messwerte_Mittel; // Bildung des arithmetischen Mittels
  SN2_Integral = SN2_Integral / Messwerte_Mittel;
  SN3_Integral = SN3_Integral / Messwerte_Mittel;
  TEMP_Integral = TEMP_Integral / Messwerte_Mittel;
  SN1_AE_Integral = SN1_AE_Integral / Messwerte_Mittel; // Bildung des arithmetischen Mittels
  SN2_AE_Integral = SN2_AE_Integral / Messwerte_Mittel;
  SN3_AE_Integral = SN3_AE_Integral / Messwerte_Mittel;

  Umrechnungsfaktor = getUmrechnungsfaktor();

  SN1_value = Umrechnungsfaktor * SN1_Integral;
  SN2_value = Umrechnungsfaktor * SN2_Integral;
  SN3_value = Umrechnungsfaktor * SN3_Integral;
  Temp_value = Umrechnungsfaktor * TEMP_Integral;

  SN1_AE_value = Umrechnungsfaktor * SN1_AE_Integral;
  SN2_AE_value = Umrechnungsfaktor * SN2_AE_Integral;
  SN3_AE_value = Umrechnungsfaktor * SN3_AE_Integral;

  // Messwerte in String zusammenbauen
  String content = String(MEASUREMENT_NAME) + ",host=esp8266,"+ Postionsstring + "," +
   SN1 +    "=" + String(SN1_value , 4) + "," +
   SN2 +    "=" + String(SN2_value, 4) + "," +
   SN3 +    "=" + String(SN3_value,4) + "," +
   TEMP +   "=" + String(Temp_value, 4) + "," +
   SN1_AE + "=" + String(SN1_AE_value, 4) + "," +
   SN2_AE + "=" + String(SN2_AE_value, 4) + "," +
   SN3_AE + "=" + String(SN3_AE_value, 4);

  return content;
}
void Upload(String Uploadstring){
  // Print the buffer on the serial line to see how it looks
  Serial.print("Sending following dataset to InfluxDB: ");
  Serial.println(Uploadstring);

  //send to InfluxDB
  // Verbindungstest mit dem InfluxDB Server connect() liefert bool false / true als return
  int Verbindungsstatus = client.connect(InfluxDB_Server_IP, InfluxDB_Server_Port);

  // Kann keine Verbindung hergestellt werden -> Fehlerausgabe incl. Fehlernummer
  if(Verbindungsstatus <= 0) {
    Serial.print("Keine Verbindung zum InfluxDB Server, Error #");
    Serial.println(Verbindungsstatus);
    return;
  }

  //Sende HTTP Header und Buffer
  if (millis()-time>=WarmUp) {
    client.println("POST /write?db=MESSCONTAINER HTTP/1.1");
    client.println("User-Agent: esp8266/0.1");
    client.println("Host: localhost:8086");
    client.println("Accept: */*");
    client.print("Content-Length: " + String(Uploadstring.length()) + "\r\n");
    client.println("Content-Type: application/x-www-form-urlencoded");
    client.println(); /*HTTP Header und Body müssen durch Leerzeile getrennt werden*/
    client.println(Uploadstring); // Übermittlung des eigentlichen Data-Strings
    client.flush(); //
    delay(100); //wait for server to process data

    // Antwort des Servers wird gelesen, ausgegeben und anschließend die Verbindung geschlossen
    Serial.println("Antwort des Servers");
    while(client.available()) { // Empfange Antwort
      Serial.print((char)client.read());
    }
  }
  Serial.println();
  client.stop();
}
void setup() {
  Serial.begin(9600);
  // Startet Kommunikation mit IDC über Port 4 und 5 auf ESP8266 und setzt Gain des ADCs
  ads_A.setGain(GAIN_TWO);        // 1x gain   +/- 4.096V  1 bit = 2mV      0.125mV
  ads_B.setGain(GAIN_TWO);
  ads_A.begin();
  ads_B.begin();

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C addr 0x3D (for the 128x64)
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0,0);

  WiFiStart();

  //if you get here you have connected to the WiFi
  display.print("Verbindung hergestellt");
  display.display();
  delay(1000);
  display.clearDisplay();
  display.println("Connected!");
  display.println(" ");
  display.println("IP:");
  display.println(WiFi.localIP());
  display.println(" ");
  display.print("Searching satellites");
  display.display();
  time = millis();
  display.clearDisplay();
  display.display();
}

void loop() {
  display.clearDisplay();
  display.setCursor(0,0);
  while (Serial.available()){
    gps.encode(Serial.read());
    if (gps.location.isUpdated()){
      latitude = String(gps.location.lat(),6);
      longitude = String(gps.location.lng(),6);
      Geohash = hasher.encode(gps.location.lat(), gps.location.lng());
      Position = "geohash="+ Geohash + " lat=" + latitude + ",lng=" + longitude;
    }
  }

  display.println(Position);
  display.display();

  Daten = Messung(Position);
  display.clearDisplay();
  display.println(Daten);
  display.display();
  Upload(Daten);
}
