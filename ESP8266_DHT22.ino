/* This is Meteo station, which measuring temperature and humidity with DHT22 (A2302).
 * It will make AP to config it to your WiFi and then sending data to Thingspeak.
 * It also has simple web interface, which provide you information of current temperature and humidity.
 * It's tested and suitable with 16x2 I2C LCD display and ESP8266-201 SDK DEV board. 
 * I2C LCD is connected SDA->GPIO4 SCL->GPIO05. Powered 5V and have same GND with chip
 */
#include <FS.h>                   //this needs to be first, or it all crashes and burns...
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <DNSServer.h>
#include <ESP8266WebServer.h> 
#include <WebSocketsServer.h>
#include <Hash.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <Wire.h> 
#include <LiquidCrystal_I2C.h>    //use NewLiquidCrystal library from https://arduino-info.wikispaces.com/LCD-Blue-I2C
#include <NTPTimeClient.h>        //needed for time update
#include "DHT.h"                  //nedded for reading fom DHT sensor

#define DHTTYPE DHT22             //defining DHT type
#define DHTPIN  16                //defining which pin is DHT connected
#define TEMP_LED 12               //defining pin which is LED connected
//APname and APpsswd can't together exceed 15 char
#define APname "APconf"              //defining AP name
#define APpsswd "123456789"        //defining AP password
#define buttonON 2                  //defining ON button pin
//#define DEBUG 

ESP8266WebServer server(80);
WebSocketsServer webSocket(81);

// TimeClient settings
const float UTC_OFFSET = 1;

NTPTimeClient timeClient(UTC_OFFSET);

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
int buttonState = 0;

//Set the LCD address to 0x27 for a 16 chars and 2 line display
LiquidCrystal_I2C lcd(0x27, 16, 2);
 
// Initialize DHT sensor 

DHT dht(DHTPIN, DHTTYPE, 11); // 11 works fine for ESP8266
float humidity, temp_c, temp_f;  // Values read from sensor

//define your default values here, if there are different values in config.json, they are overwritten.
char mqtt_server[40] = "api.thingspeak.com";
char mqtt_port[40] = "Your WriteAPI";
//const char* host = "api.thingspeak.com";
//const char* writeAPIKey = "YourWriteAPI";

//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  #ifdef DEBUG
  Serial.println("Should save config");
  #endif
  shouldSaveConfig = true;
}

unsigned long pMillis = 0;        // will store last time 
unsigned long timeLCDon = 0;        // will store last time was LCD backlight on


void backlightLCDon(){        //interupt function, which makes lcd backlight on 
      lcd.backlight();
      #ifdef DEBUG
      Serial.println("on");
      #endif
      timeLCDon = millis();
      return; 
}

//DHT dht(DHT_PIN, DHT_TYPE);

void printToLCD(){     //printing AP name, AP password and AP IP address to config on display during config mode
  lcd.setCursor(0, 0);
  lcd.print(APname);
  lcd.print(" ");
  lcd.print(APpsswd);
  lcd.setCursor(0, 1);
  lcd.print(WiFi.softAPIP());
  #ifdef DEBUG
  Serial.println("lcd print");
  #endif
}

//getting updated time and date from NTP server and printing to lcd
void updateData(){
  timeClient.updateTime();
  lcd.setCursor(0, 0);
  lcd.print(timeClient.getFormattedDate("."));
  lcd.print(" ");
  lcd.print(timeClient.getFormattedTime());
  #ifdef DEBUG
  Serial.println(timeClient.getFormattedDate("."));
  Serial.println(timeClient.getFormattedTime());
  #endif
}

void readSensor(){
  humidity = dht.readHumidity();          // Read humidity (percent)
  temp_c = dht.readTemperature();         // Read temperature as Celsius
  temp_f = dht.readTemperature(true);     // Read temperature as Farenheit
  
  if (isnan(humidity) || isnan(temp_c) || isnan(temp_f)){ //check, if values have been read correctly
      #ifdef DEBUG
      Serial.println("Failed to read from DHT sensor!");
      #endif
      lcd.setCursor(0, 1);
      lcd.print("Failed to read");
      return;
  } else {
      //blinking led, that temperature has been read
      digitalWrite(TEMP_LED, HIGH);
      delay(499);
      digitalWrite(TEMP_LED, LOW);
      return;
  }
}

void valuesToLCD() {
   //Printing data on LCD display
      lcd.setCursor(0, 1);    
      lcd.write(1);
      lcd.print(temp_c);
      lcd.print((char)223); 
      lcd.print("C ");
      lcd.write(2);
      lcd.print(humidity);
      lcd.print("%");
      return;
}

void serialPrintValues(){
      Serial.print("Humidity: "); 
      Serial.print(humidity);
      Serial.print(" %\t");
      Serial.print("Temperature: "); 
      Serial.print(temp_c);
      Serial.print(" C\t");
      Serial.print("Temperature: "); 
      Serial.print(temp_f);
      Serial.print(" F\n");
      return;
}

void saveToHTML(){
   //saving data to web page
   webPage = "<!DOCTYPE><html><head><meta charset=\" UTF-8 \"><title>Meteo Stanice</title><style> body{text-align:center;margin:10px auto;} h1, p {font-family: Arial} table{margin:auto;border-collapse:collapse;font-family:Sans-serif;font-size: 25px;} table,th,td{border:1px solid black;padding:5px;text-align:center;}</style></head><body><h1>Meteo stanice</h1><p>Pro aktuální hodnoty, obnovte stránku</p><table><thead><td>Teplota v &degF</td><td>Teplota v &degC</td><td>Vlhkost</td></thead><tbody><td>"+String((float)temp_f)+" &degF</td><td>"+String((float)temp_c)+" &degC</td><td>"+String((float)humidity)+"%</td></tbody></table></body></html>";
   return;
}

void sendToThingspeak()  {
      //checking if can connect to server(to Thingspeak mqtt_server is adrress, httpPort is writeAPI key)
      WiFiClient client;
      const int httpPort = 80;
      if (!client.connect(mqtt_server, httpPort)) {
        return;
      }
      
      //sending data to Thingspeak
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
      return;           
}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  #ifdef DEBUG
  Serial.println();
  #endif
  interrupts();          //initialize interrupt
  dht.begin();           // initialize temperature sensor
  lcd.begin();          // initialize the LCD
  pinMode(TEMP_LED, OUTPUT); // set LED to output
  
  pinMode(buttonON, INPUT);   //setting button as input
  attachInterrupt(digitalPinToInterrupt(buttonON), backlightLCDon , HIGH);    //adding interrupt on pressing on BUTTON
 
  //SPIFFS.format();      //clean FS, for testing
  #ifdef DEBUG
  Serial.println("mounting FS..."); //read configuration from FS 
  #endif

  if (SPIFFS.begin()) {
    #ifdef DEBUG
    Serial.println("mounted file system");
    #endif
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      #ifdef DEBUG
      Serial.println("reading config file");
      #endif
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        #ifdef DEBUG
        Serial.println("opened config file");
        #endif
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          #ifdef DEBUG
          Serial.println("\nparsed json");
          #endif
          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          //strcpy(blynk_token, json["blynk_token"]);

        } else {
          #ifdef DEBUG
          Serial.println("failed to load json config");
          #endif
        }
      }
    }
  } else {
    #ifdef DEBUG
    Serial.println("failed to mount FS");
    #endif
  }
  //end read

    


  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_mqtt_server("server", "api.thingspeak.com", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "yourWriteAPI", mqtt_port, 40);
 // WiFiManagerParameter custom_blynk_token("blynk", "blynk token", blynk_token, 32);

  WiFiManager wifiManager;      //Local intialization. Once its business is done, there is no need to keep it around

  wifiManager.setSaveConfigCallback(saveConfigCallback);  //set config save notify callback

  //wifiManager.setSTAStaticIPConfig(IPAddress(10,0,1,99), IPAddress(10,0,1,1), IPAddress(255,255,255,0));    //you can set static IP
  
  //add all your parameters here
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  //wifiManager.addParameter(&custom_blynk_token);

  /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////  
  //wifiManager.resetSettings();  //reset settings - for testing
  
  //wifiManager.setMinimumSignalQuality();    //set minimu quality of signal so it ignores AP's under that quality defaults to 8%
  
  //sets timeout until configuration portal gets turned off useful to make it all retry or go to sleep in seconds
  wifiManager.setTimeout(300);

  printToLCD();
  
  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect(APname, APpsswd)) {
    #ifdef DEBUG
    Serial.println("failed to connect and hit timeout");
    #endif
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  #ifdef DEBUG
  Serial.println("connected...yeey :)");
  #endif
  lcd.setCursor(0, 0);
  lcd.print("Connected Wifi              ");
  lcd.setCursor(0, 1);
  lcd.print(WiFi.localIP());  

  //read updated parameters
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  //strcpy(blynk_token, custom_blynk_token.getValue());

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    #ifdef DEBUG
    Serial.println("saving config");
    #endif
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      #ifdef DEBUG
      Serial.println("failed to open config file for writing");
      #endif
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }

  #ifdef DEBUG
  Serial.println("local ip");
  Serial.println(WiFi.localIP());
  #endif
  
  server.on("/", [](){    //shut on webserver and sending clean webpage
    server.send(200, "text/html", webPage);
  });

  server.begin();        //startingweb server
  #ifdef DEBUG
  Serial.println("HTTP server started");  
  #endif
  webPage += "<!DOCTYPE html>";
  webPage += "<html>";
  webPage += "<body>";
  webPage += "<p>Teplota v F: "+String((float)temp_f)+" &degF </p>";
  webPage += "<p>Teplota v C: "+String((float)temp_c)+" &degC</p>";
  webPage += "<p>Vlhkost: "+String((float)humidity)+"%</p>";
  webPage += "</body>";
  webPage += "</html>";
 
  lcd.createChar(1, icon_termometer);   //creating char for LCD
  lcd.createChar(2, icon_water);
  delay(5000);

   readSensor();
   valuesToLCD();
   #ifdef DEBUG
   serialPrintValues();
   #endif
   sendToThingspeak();
   saveToHTML();
  }


  
void loop() {
      server.handleClient();    //checking if there is some client request
      updateData();
      if (millis() - pMillis >= 300000) {
        readSensor();
        valuesToLCD();
        #ifdef DEBUG
        serialPrintValues();
        #endif
        sendToThingspeak();
        saveToHTML();
      }
      if(millis() - timeLCDon >= 10000){
        lcd.noBacklight();
        #ifdef DEBUG
        Serial.println("LCD off"); 
        #endif
      }
}
