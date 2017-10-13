#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <functional>
#include "switch.h"
#include "UpnpBroadcastResponder.h"
#include "CallbackFunction.h"
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <TaskScheduler.h>

MDNSResponder mdns;
WiFiServer server(80);

//#define _TASK_SLEEP_ON_IDLE_RUN
Scheduler ts;

//change these 3 items 
const char* moduleNameRemoteUpdate = "ecoplug1";
const char* alexaDeviceName = "Ecoplug one";  //alexa name to be spoken
int alexaPort = 90;                           //make port 2 numbers higher

//these can stay constant
const char* remoteUpdatePassword = "";
String ssid = "ENTER-YOUR-SSID-HERE";                     //the default WIFI SSID name
String password = "ENTER-YOUR-WIFI-PASSWORD-HERE";        //the default wifi password
const char* testSSID = "MilesSwitchSetup";
const char* testPW = "miles";
String st;
String epass = "";
String esid = "";
bool systemSetup = false;
bool procNewServer = false;
bool switchIsOn = false;

// Set Relay Pins
#define LED_PIN 2         //sonoff led=13
#define relayOne 15        // gpio 0 is pin 6 on esp-01. GPIO12 is pin 10 on ESP8266EX. sonoff relay=12.
//#define relayTwo LED_PIN   //gpio 2 is pin 4 on esp-01. GPIO13 is pin 12 on ESP8266EX. Take off for ecoplug?

#define resetButton 13     //sonoff gpio 0 is the switch

/* https://github.com/scottjgibson/esp8266Switch/blob/master/esp8266Switch.ino
const int PIN_RELAY = 15;
const int PIN_LED = 2;
const int PIN_BUTTON = 13;
 */

//ESP8266EX Pin  Function  GPIO  Connected to
//9 – MTMS  GPIO  GPIO14  J1 Pin 5
//10 – MTDI   GPIO  GPIO12  Relay (HIGH to turn on)
//12 – MTCK   GPIO  GPIO13  LED (LOW to turn on)
//15 – GPIO0  Flash   GPIO0   Tactile switch (LOW when switch is pressed)
//25 – RDX  UART RXD  GPIO3   J1 Pin 2
//26 – TXD  UART TXD  GPIO1   J1 Pin 3

// prototypes
void testWifi();
void handleUpdate();
void checkPin();
void restartUnit();
Task cRestartUnit(1000, TASK_ONCE, &restartUnit, &ts);
Task cCheckPin(500, TASK_FOREVER, &checkPin, &ts);
Task cWIFI(10000, TASK_FOREVER, &testWifi, &ts);
Task cUpdate(TASK_IMMEDIATE, TASK_FOREVER, &handleUpdate, &ts);

//on/off callbacks 
void lightOneOn();
void lightOneOff();

UpnpBroadcastResponder upnpBroadcastResponder;
Switch *lightOne = NULL;
unsigned int debounceTime = 0;  // the debounce counter

void setup()
{
    // Initialize LED pin
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, 0); // 0 is on
  
   pinMode(resetButton, INPUT_PULLUP);
   EEPROM.begin(512);

  // Initialize switch relay pin
  digitalWrite(relayOne, 0); //1 is off. 0 is on
  pinMode(relayOne, OUTPUT);
  digitalWrite(relayOne, 0); // low turns switch off on sonoff 

  Serial.begin(115200);
    myDelay(1000);
   digitalWrite(LED_PIN, 1); // 0 is on
   
  Serial.println("Running 8266");
 
  // read eeprom for ssid and pass
  Serial.println("Reading EEPROM ssid");
  esid = "";
  //int lengthOfEsid = EEPROM.read(500)
  for (int i = 0; i < 32; ++i)
    {
     byte myNewChar = EEPROM.read(i);
     if (myNewChar != 0xFF){
        esid += char(myNewChar);
     }
      myDelay(10);
    }
  Serial.print("SSID: ");
  Serial.println(esid);
  Serial.println("Reading EEPROM pass");
  epass = "";
  for (int i = 32; i < 96; ++i)
    {
       byte myNewChar = EEPROM.read(i);
       if (myNewChar != 0xFF){
          epass += char(myNewChar);
       }
       myDelay(10);
    }
  Serial.print("PASS: ");
  Serial.println(epass); 

  digitalWrite(LED_PIN, 0); // 0 is on  
    
  initRemoteUpdate(); 
  cUpdate.enable(); 
  //see if reset button is pushed and not low start normal operation.
  //If it is low start webserver to set wifi

    
    // Initialise wifi connection
    connectWifi();
    
    enableAlexa();
    cWIFI.enable();
    cCheckPin.enable();
}
 
void loop()
{
   //ArduinoOTA.handle();
   ts.execute();
}

void handleUpdate(){
  ArduinoOTA.handle();

   if ( WiFi.status() == WL_CONNECTED )
   {
      upnpBroadcastResponder.serverLoop();     
      lightOne->serverLoop();  
   }
}

void lightOneOn() {
    Serial.print("Switch 1 turn on ...");
    digitalWrite(relayOne, 1);   // high turns switch on
    //digitalWrite(LED_PIN, 0);   // low turns led on for ecoplug opposite for wemos
    switchIsOn = true;
}

void lightOneOff() {
    Serial.print("Switch 1 turn off ...");
    digitalWrite(relayOne, 0);   // low turns switch off
    //digitalWrite(LED_PIN, 1);   // high turns led off
    switchIsOn = false;
}

void checkPin(){
    if (!digitalRead(resetButton)){
      debounceTime++;

      if (debounceTime == 1){
        if (switchIsOn){
           lightOneOff();
        }
        else{
          lightOneOn();
        }
      }
      //make sure we hit this 3 times at least
      else if (debounceTime == 3){   // was 2 before 10/13
        digitalWrite(LED_PIN, 0);   // high turns led off
        if (!procNewServer){
          procNewServer = true;
          cWIFI.disable();
          cUpdate.disable();
          setupAP(); 
        }
      }
      else if (debounceTime < 5){
        digitalWrite(LED_PIN, 0); // 0 is on
      }
      else if (debounceTime > 5){
        digitalWrite(LED_PIN, 1); // 0 is on
        cRestartUnit.enableDelayed(2000);
      }
      else{
        digitalWrite(LED_PIN, 1); // 0 is on
      }
    }
    else{
      digitalWrite(LED_PIN, 1); // 0 is on
      debounceTime = 0;
    }
}

void restartUnit(){
 
    WiFi.disconnect();
    myDelay(101);
    WiFi.mode(WIFI_STA);
    myDelay(101);
     ESP.restart();  //reset unit
     myDelay(1000);
     ESP.reset();  //reset unit
}

void enableAlexa(){
    if ( WiFi.status() == WL_CONNECTED )
    {
      if (!systemSetup){
        upnpBroadcastResponder.beginUdpMulticast();
        
        // Define your switches here. Max 14
        // Format: Alexa invocation name, local port no, on callback, off callback
        lightOne = new Switch(alexaDeviceName, alexaPort, lightOneOn, lightOneOff);
        
        Serial.println("Adding switches upnp broadcast responder");
        upnpBroadcastResponder.addDevice(*lightOne);
  
         digitalWrite(LED_PIN, 1); // 0 is on
         systemSetup = true;
      }
    } 
}

void initRemoteUpdate(){
  
  Serial.println("Booting");
  WiFi.mode(WIFI_STA);
  WiFi.begin(esid.c_str(), epass.c_str());
  int cntr = 0;
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    myDelay(1500);
    //ESP.restart();
    cntr++;
    if (cntr > 8){
      break;
    }
  }

  if (cntr > 8){
      WiFi.begin(ssid.c_str(), password.c_str());
      while (WiFi.waitForConnectResult() != WL_CONNECTED) {
        Serial.println("Connection Failed! Rebooting...");
        myDelay(1000);
        //ESP.restart();
        cntr++;
        if (cntr > 16){
          break;
        }
      }
      if (cntr <= 16){
        writeFailSafeToEeeprom();
      }
  }

  // Port defaults to 8266
  ArduinoOTA.setPort(8266);
     
  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(moduleNameRemoteUpdate);

  // No authentication by default
  //ArduinoOTA.setPassword((const char *)HostUpdatePassword);
  ArduinoOTA.setPassword(remoteUpdatePassword);
  
  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
}

void testWifi(){
  connectWifi();
  enableAlexa();
  if (Serial.available() > 0) { checkSerial(); }
}

void writeFailSafeToEeeprom(){
          esid = ssid;
        epass = password;
        Serial.println("writing eeprom ssid:");
        for (int i = 0; i < ssid.length(); ++i)
          {
            EEPROM.write(i, ssid[i]);
            Serial.print("Wrote: ");
            Serial.println(ssid[i]); 
            myDelay(100);
          }
          myDelay(100);
        Serial.println("writing eeprom pass:"); 
        for (int i = 0; i < password.length(); ++i)
          {
            EEPROM.write(32+i, password[i]);
            Serial.print("Wrote: ");
            Serial.println(password[i]); 
            myDelay(100);
          }    
        EEPROM.commit(); 
}

// connect to wifi – returns true if successful or false if not
bool connectWifi(){
  boolean state = true;
  int i = 0;

  if (WiFi.status() != WL_CONNECTED){
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(esid.c_str(), epass.c_str());
    Serial.println("");
    Serial.println("Connecting to WiFi");
  
    // Wait for connection
    Serial.print("Connecting ...");
    while (WiFi.status() != WL_CONNECTED) {
      myDelay(500);
      Serial.print(".");
      if (i > 40){
        state = false;
        break;
      }
      i++;
    }

    if (!state){
      state = true;
      WiFi.begin(ssid.c_str(), password.c_str());
      Serial.println("");
      Serial.println("Connecting to WiFi");
    
      // Wait for connection
      Serial.print("Connecting ...");
      while (WiFi.status() != WL_CONNECTED) {
        myDelay(500);
        Serial.print(".");
        if (i > 40){
          state = false;
          break;
        }
        i++;
      }
      //this is my backup username. if it works save it to eeprom
      if (state){
        writeFailSafeToEeeprom();
      }
    }
    
    if (state){
      Serial.println("");
      Serial.print("Connected to ");
      Serial.println(ssid);
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
    }
    else {
      Serial.println("");
      Serial.println("Connection failed.");
    }
  }

  return state;
}

void setupAP(void) {
  digitalWrite(LED_PIN, 0); // 0 is on
     
  WiFi.mode(WIFI_STA);
  //WiFi.disconnect();


   myDelay(100);                 // give us some slack ;-)
  int n = WiFi.scanNetworks();
  Serial.println("scan done");
  if (n == 0)
    Serial.println("no networks found");
  else
  {
    Serial.print(n);
    Serial.println(" networks found");
    for (int i = 0; i < n; ++i)
     {
      // Print SSID and RSSI for each network found
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.print(")");
      Serial.println((WiFi.encryptionType(i) == ENC_TYPE_NONE)?" ":"*");
      delay(10);
     }
  }
  Serial.println(""); 
  st = "<ul>";
  for (int i = 0; i < n; ++i)
    {
      // Print SSID and RSSI for each network found
      st += "<li>";
      st +=i + 1;
      st += ": ";
      st += WiFi.SSID(i);
      st += " (";
      st += WiFi.RSSI(i);
      st += ")";
      st += (WiFi.encryptionType(i) == ENC_TYPE_NONE)?" ":"*";
      st += "</li>";
    }
  st += "</ul>";

  WiFi.mode(WIFI_AP);
 myDelay(100);

  WiFi.softAP(testSSID,testPW);
  //WiFi.mode(WIFI_AP);
  for(uint8_t t = 7; t > 0; t--) {
      Serial.print("Waiting ");
      Serial.flush();
        myDelay(500);
  }

       
  Serial.println("softap");
  Serial.println("");
  launchWeb(1);
  Serial.println("over");
  
        
  cWIFI.enable();
  cUpdate.enable();
  procNewServer = false;
}


void launchWeb(int webtype) {
          Serial.println("");
          Serial.println("WiFi connected");
          Serial.println(WiFi.localIP());
          Serial.println(WiFi.softAPIP());
          Serial.println("Setting up MDNS responder! ");
          while(1) { 
            if (!mdns.begin("esp8266", WiFi.localIP())) {
              Serial.print(".");
              myDelay(1000);
            }
            else{
              break;
            }
          }
          Serial.println("mDNS responder started");
          // Start the server
          server.begin();
          Serial.println("Server started");   
          int b = 20;
          int c = 0;
          while(b == 20) { 
             b = mdns1(webtype);
           }

       Serial.println("Leaving Launch Web");   
}

int mdns1(int webtype)
{
  // Check for any mDNS queries and send responses
  mdns.update();
  
  // Check if a client has connected
  WiFiClient client = server.available();
  if (!client) {
    myDelay(100);
    return(20);
  }
  Serial.println("");
  Serial.println("New client");

  // Wait for data from client to become available
  while(client.connected() && !client.available()){
    myDelay(1);
   }
  
  // Read the first line of HTTP request
  String req = client.readStringUntil('\r');
  
  // First line of HTTP request looks like "GET /path HTTP/1.1"
  // Retrieve the "/path" part by finding the spaces
  int addr_start = req.indexOf(' ');
  int addr_end = req.indexOf(' ', addr_start + 1);
  if (addr_start == -1 || addr_end == -1) {
    Serial.print("Invalid request: ");
    Serial.println(req);
    return(20);
   }
  req = req.substring(addr_start + 1, addr_end);
  Serial.print("Request: ");
  Serial.println(req);
  client.flush(); 
  String s;
  bool kickoutFunction = false;
  
  if ( webtype == 1 ) {
      if (req == "/")
      {
        IPAddress ip = WiFi.softAPIP();
        String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
        s = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html>Hello from Miles's Nerd Palace!<br> Please Enter the WIFI Station and Password and Hit Submit below.<br> You are connected to at ";
        s += ipStr;
        s += "For wireless module ";
        s += (String)moduleNameRemoteUpdate;
        s += "<p>";
        s += st;
        s += "<form method='get' action='a'><label>Enter SSID and Password and Click Submit<br>WIFI SSID: </label><input name='ssid' length=32><label><br>PASSWORD: </label><input name='pass' length=64><label><br></label><input type='submit'></form>";
        s += "</html>\r\n\r\n";
        Serial.println("Sending 200");
      }
      else if ( req.startsWith("/a?ssid=") ) {
        // /a?ssid=blahhhh&pass=poooo
        //Serial.println("clearing eeprom");
        //for (int i = 0; i < 96; ++i) { EEPROM.write(i, 0); }
        String qsid; 
        qsid = req.substring(8,req.indexOf('&'));
        Serial.println(qsid);
        Serial.println("");
        String qpass;
        qpass = req.substring(req.lastIndexOf('=')+1);
        Serial.println(qpass);
        Serial.println("");
        
        Serial.println("writing eeprom ssid:");
        for (int i = 0; i < qsid.length(); ++i)
          {
            EEPROM.write(i, qsid[i]);
            Serial.print("Wrote: ");
            Serial.println(qsid[i]); 
            myDelay(10);
          }
        for (int i = qsid.length() ; i < 32;  ++i){
          byte val = EEPROM.read(i);
          if (val != 0xFF){
             EEPROM.write(i, 0xFF);
             myDelay(10);
          }
        }
        
        Serial.println("writing eeprom pass:"); 
        for (int i = 0; i < qpass.length(); ++i)
          {
            EEPROM.write(32+i, qpass[i]);
            Serial.print("Wrote: ");
            Serial.println(qpass[i]); 
            myDelay(10);
          }  

        for (int i = qpass.length()+32 ; i < 96;  ++i){
          byte val = EEPROM.read(i);
          if (val != 0xFF){
             EEPROM.write(i, 0xFF);
             myDelay(10);
          }
        }
        
        EEPROM.commit();
        s = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html>Congrats you set the new Wifi and Password!<br>";
        s += "Found ";
        s += req;
        s += "<p> WIFI Data saved... reset to boot into new wifi</html>\r\n\r\n";
        kickoutFunction = true;
      }
      else
      {
        s = "HTTP/1.1 404 Not Found\r\n\r\n";
        Serial.println("Sending 404");
      }
  } 
  else
  {
      if (req == "/")
      {
        s = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html>Hello from ESP8266";
        s += "<p>";
        s += "</html>\r\n\r\n";
        Serial.println("Sending 200");
      }
      else if ( req.startsWith("/cleareeprom") ) {
        s = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n<html>Hello from ESP8266";
        s += "<p>Clearing the EEPROM<p>";
        s += "</html>\r\n\r\n";
        Serial.println("Sending 200");  
        //Serial.println("clearing eeprom");
        //for (int i = 0; i < 96; ++i) { EEPROM.write(i, 0); }
        //EEPROM.commit();
      }
      else
      {
        s = "HTTP/1.1 404 Not Found\r\n\r\n";
        Serial.println("Sending 404");
      }       
  }
  client.print(s);
  
  if (kickoutFunction){
    Serial.println("Done with client");
    //digitalWrite(LED_PIN, 1); // 0 is on
    cRestartUnit.enableDelayed(3000);
  }
 
  return(20);
  
}


//toggles watchdog
void myDelay(int ms) {
  int i;
  for(i=1;i!=ms;i++) {
     delay(1);
     if(i%100 == 0) { 
        ESP.wdtFeed(); 
        yield(); 
        ts.execute();
     }
  }
}

void checkSerial(){
  if (Serial.available() > 0) {

    // read incoming serial data:
    char inChar = Serial.read();
    Serial.println(inChar);
    if (inChar == 'O'){
       lightOneOn();
    }
    else if (inChar == 'o'){
       lightOneOff();
    }
  }
}
