#include <FS.h>                   //this needs to be first, or it all crashes and burns...
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>
#include <Hash.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <Wire.h> 
#include <LiquidCrystal_I2C.h>
#include "DHT.h"
#define DHTTYPE DHT22
#define DHTPIN  16
#define TEMP_LED 12
#define APname "ESP"
#define APpsswd "password"


ESP8266WebServer server(80);
WebSocketsServer webSocket(81);

byte icon_termometer[8] = //icon for termometer
{
    B00100,
    B01010,
    B01010,
    B01110,
    B01110,
    B11111,
    B11111,
    B01110
};

byte icon_water[8] = //icon for water droplet 
{
    B00100,
    B00100,
    B01010,
    B01010,
    B10001,
    B10001,
    B10001,
    B01110,
};

String webPage;
int pocet = 0;
//Set the LCD address to 0x27 for a 16 chars and 2 line display
LiquidCrystal_I2C lcd(0x27, 16, 2);
 
// Initialize DHT sensor 

DHT dht(DHTPIN, DHTTYPE, 11); // 11 works fine for ESP8266
float humidity, temp_c, temp_f;  // Values read from sensor

//define your default values here, if there are different values in config.json, they are overwritten.
char mqtt_server[40] = "api.thingspeak.com";
char mqtt_port[40] = "YourWriteAPI";
//char blynk_token[34] = "";
//const char* host = "api.thingspeak.com";
//const char* writeAPIKey = "YourWriteAPI";

//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

//DHT dht(DHT_PIN, DHT_TYPE);

void printToLCD(){
  lcd.setCursor(0, 0);
  lcd.print(APname);
  lcd.print(" ");
  lcd.print(APpsswd);
  lcd.setCursor(0, 1);
  lcd.print(WiFi.softAPIP());
  Serial.println("lcd print");
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println();
  dht.begin();           // initialize temperature sensor
  // initialize the LCD
  lcd.begin();
  //Wire.begin(0, 2);
  pinMode(TEMP_LED, OUTPUT);
  
  //clean FS, for testing
  //SPIFFS.format();

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");

          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          //strcpy(blynk_token, json["blynk_token"]);

        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read

    


  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_mqtt_server("server", "api.thingspeak.com", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "yourWriteAPI", mqtt_port, 40);
 // WiFiManagerParameter custom_blynk_token("blynk", "blynk token", blynk_token, 32);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //set static ip
  //wifiManager.setSTAStaticIPConfig(IPAddress(10,0,1,99), IPAddress(10,0,1,1), IPAddress(255,255,255,0));
  
  //add all your parameters here
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  //wifiManager.addParameter(&custom_blynk_token);

  /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////  
  //reset settings - for testing
  //wifiManager.resetSettings();
  
  
  //set minimu quality of signal so it ignores AP's under that quality
  //defaults to 8%
  //wifiManager.setMinimumSignalQuality();
  
  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  wifiManager.setTimeout(300);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect(APname, APpsswd)) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");
  lcd.setCursor(0, 0);
  lcd.print("Pripojeno k WiFi");
  lcd.setCursor(0, 1);
  lcd.print("Connected to Wifi");

  //read updated parameters
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  //strcpy(blynk_token, custom_blynk_token.getValue());

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    //json["blynk_token"] = blynk_token;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }

  Serial.println("local ip");
  Serial.println(WiFi.localIP());
  
  server.on("/", [](){
    server.send(200, "text/html", webPage);
  });

  server.begin();
  Serial.println("HTTP server start");  
  webPage += "<!DOCTYPE html>";
  webPage += "<html>";
  webPage += "<body>";
  webPage += "<p>Teplota v F: "+String((float)temp_f)+" &degF </p>";
  webPage += "<p>Teplota v C: "+String((float)temp_c)+" &degC</p>";
  webPage += "<p>Vlhkost: "+String((float)humidity)+"%</p>";
  webPage += "</body>";
  webPage += "</html>";
 
  lcd.createChar(1, icon_termometer);
  lcd.createChar(2, icon_water);
  
  }
  
void loop() {
  humidity = dht.readHumidity();          // Read humidity (percent)
  temp_c = dht.readTemperature();         // Read temperature as Celsius
  temp_f = dht.readTemperature(true);     // Read temperature as Farenheit
  
  if (isnan(humidity) || isnan(temp_c) || isnan(temp_f)){ 
    Serial.println("Failed to read from DHT sensor!");
    lcd.setCursor(0, 1);
    lcd.print("Chyba pri cteni");
    return;
  } 
      Serial.print("Humidity: "); 
      Serial.print(humidity);
      Serial.print(" %\t");
      Serial.print("Temperature: "); 
      Serial.print(temp_c);
      Serial.print(" C\t");
      Serial.print("Temperature: "); 
      Serial.print(temp_f);
      Serial.print(" F\n");
      digitalWrite(TEMP_LED, HIGH);
      delay(500);
      digitalWrite(TEMP_LED, LOW);
    
      webPage = "<!DOCTYPE><html><head><meta charset=\" UTF-8 \"><title>Meteo Stanice</title><style> body{text-align:center;margin:10px auto;} h1, p {font-family: Arial} table{margin:auto;border-collapse:collapse;font-family:Sans-serif;font-size: 25px;} table,th,td{border:1px solid black;padding:5px;text-align:center;}</style></head><body><h1>Meteo stanice</h1><p>Pro aktualní hodnoty teplot, obnovte stránku</p><table><thead><td>Teplota v &degF</td><td>Teplota v &degC</td><td>Vlhkost</td></thead><tbody><td>"+String((float)temp_f)+" &degF</td><td>"+String((float)temp_c)+" &degC</td><td>"+String((float)humidity)+"%</td></tbody></table></body></html>";
    
      WiFiClient client;
      const int httpPort = 80;
      if (!client.connect(mqtt_server, httpPort)) {
        return;
      }
  
      String url = "/update?key=";
      url+=mqtt_port;
      url+="&field1=";
      url+=String(temp_c);
      url+="&field2=";
      url+=String(humidity);
      url+="&field3=";
      url+=String(temp_f);
      url+="\r\n";
    
      // Request to the server
      client.print(String("GET ") + url + " HTTP/1.1\r\n" +
                 "Host: " + mqtt_server + "\r\n" + 
                 "Connection: close\r\n\r\n");
    lcd.setCursor(0, 0);
    lcd.print("IP:");
    lcd.print(WiFi.localIP());
    lcd.print("    ");  
    lcd.setCursor(0, 1);
    lcd.write(1);
    lcd.print(temp_c);
    lcd.print((char)223); 
    lcd.print("C  ");
    //lcd.setCursor(9, 1);
    lcd.write(2);
    lcd.print(humidity);
    lcd.print("%");
  //}

   for(pocet = 0; pocet <= 150; pocet++){
    server.handleClient();
    delay(2000);
    Serial.print(pocet);
    Serial.print("\t");
   }
 pocet = 0;            
   
}
