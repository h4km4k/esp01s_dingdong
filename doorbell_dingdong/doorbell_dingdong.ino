#include <ESP8266WiFi.h>
#include <HardwareSerial.h>
#include <EEPROM.h>
#include <ESP8266mDNS.h>

//GPIO
#define RELAY_PORT 0
#define INPUT_PORT 3

//Output
bool relay = false;
bool ledOn = false;
unsigned long relayActivationTime = 0; // Zeitpunkt der Aktivierung des Relais
bool relayActive = false; // Status des Relais

//Input
int inputValue = 0;
unsigned long lastReadTime = 0;
const unsigned long readInterval = 50;
const unsigned long buttonPressDelay = 50; // Mindestdauer für einen Druck
const unsigned long ledDuration = 30000;    // LED-Dauer
const unsigned long timeoutDuration = 5000; // Timeout-Dauer für Eingaben
bool doorbellPressed = false;
char morseCode[8]; // C-String für Morsecode (max. 7 Zeichen + nullterminierend)
unsigned long lastInputTime = 0; // Letzte Eingabezeit
unsigned long pressStartTime = 0; // Zeitpunkt des Drucks



//WiFi
#define MAX_CLIENTS 10
WiFiServer server(80);
int wifiScanCount = 0;
int32_t ssidVolume[50];
WiFiClient* clients[MAX_CLIENTS] = { NULL };
char clientsCurrentLine[MAX_CLIENTS][256];  // Puffer für die aktuelle Zeile
char* clientsHTTPRequest[MAX_CLIENTS] = { NULL };

//HTTP Protocol
int httpMethod = 0;

//RS232
String inputString = "";         // a string to hold incoming data
boolean stringComplete = false;  // whether the string is complete

int ssid_adr = 0;         // len = 40  + 5 jeweils Abstand geht gut zum Rechnen
int password_adr = 45;    // len = 30
int ip_adr = 80;          // len = 15
int subnet_adr = 100;     // len = 15
int defaultgw_adr = 120;  // len = 15
int dns_adr = 140;        // len = 15
int ipType_adr = 160;     //len = 1
int powerOn_adr = 165;    //len = 1
int mDNS_adr = 170;       //len=20
//next at 195

String ssid = "";
String password = "";
String ipType = "";
String ip = "";
String subnet = "";
String defaultgw = "";
String dns = "";
String powerOn = "";
String mDNS_name = "";

//EEPROM Struct
struct
{
  uint val = 0;
  char str[80] = "";
} data;

void setup() {
  EEPROM.begin(256);

  inputString.reserve(200);
  ssid.reserve(40);
  password.reserve(30);
  ipType.reserve(1);
  ip.reserve(15);
  subnet.reserve(15);
  defaultgw.reserve(15);
  dns.reserve(15);
  powerOn.reserve(1);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  pinMode(RELAY_PORT, OUTPUT);
  digitalWrite(RELAY_PORT, HIGH);

  pinMode(INPUT_PORT, INPUT);

  memset(morseCode, 0, sizeof(morseCode)); // Setze den C-String auf null

  Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY);

  readValuesFromEeprom();

  if (powerOn == "1") {
    digitalWrite(RELAY_PORT, LOW);
    relay = true;
  }

  wifiScanCount = scanWiFi();

  if (!startWiFiClient()) {
    startWifiAccessPoint();
  }

  MDNS.begin(mDNS_name);

  server.begin();

  MDNS.addService("http", "tcp", 80);
}

void loop() {
  if (WiFi.getMode() == WIFI_AP && WiFi.status() != WL_CONNECTED) {
    startWiFiClient();
  }

  MDNS.update();

  WiFiClient client = server.available();

  checkRS232();

  // Client zwischenspeichern
  if (client) {
    for (int i = 0; i < MAX_CLIENTS; ++i) {
      if (clients[i] == NULL) {
        clients[i] = new WiFiClient(client);
        memset(clientsCurrentLine[i], 0, sizeof(clientsCurrentLine[i]));  // Zeilenpuffer zurücksetzen
        break;
      }
    }
  }

  // Clients durcharbeiten
  for (int i = 0; i < MAX_CLIENTS; ++i) {
    if (clients[i] != NULL && clients[i]->connected()) {
      if (clients[i]->available()) {
        char c = clients[i]->read();

        if (c != '\r' && c != '\n') {             // Zeile lesen
          strncat(clientsCurrentLine[i], &c, 1);  // Zeichen zur aktuellen Zeile hinzufügen
          //digitalWrite(LED_BUILTIN, LOW);
        }
        if (c == '\n') {  // Zeile abgeschlossen
          if (strncmp(clientsCurrentLine[i], "GET ", 4) == 0) {
            // Freigeben des alten HTTP-Requests
            if (clientsHTTPRequest[i] != NULL) {
              delete[] clientsHTTPRequest[i];
            }
            clientsHTTPRequest[i] = new char[strlen(clientsCurrentLine[i]) + 1];
            strcpy(clientsHTTPRequest[i], clientsCurrentLine[i]);  // Kopiere die aktuelle Zeile
          }
          answerRequest(clientsCurrentLine[i], *clients[i], clientsHTTPRequest[i]);

          // Zeilenpuffer zurücksetzen
          memset(clientsCurrentLine[i], 0, sizeof(clientsCurrentLine[i]));
        }
      }
    } else if (clients[i] != NULL) {
      clients[i]->flush();
      clients[i]->stop();
      delete clients[i];  // Speicher freigeben
      clients[i] = NULL;

      // Freigabe des HTTP-Requests
      if (clientsHTTPRequest[i] != NULL) {
        delete[] clientsHTTPRequest[i];
        clientsHTTPRequest[i] = NULL;
      }
    }
  }

  inputloop();
}

void answerRequest(String currentLine, WiFiClient client, String httpRequest) {
  if (currentLine.length() == 0)  //Ende des HTTP Request
  {
    if (httpRequest.startsWith("GET / "))  // Startseite
    {
      sendStartPage(client);
      httpMethod = 1;
    }
    else if (httpRequest.startsWith("GET /1s "))  // Startseite
    {
      digitalWrite(RELAY_PORT, LOW);
      relay = true;
      delay(1000);
      digitalWrite(RELAY_PORT, HIGH);
      relay = false;
      sendStartPage(client);
      httpMethod = 1;
    } else if (httpRequest.startsWith("GET /2s "))  // Startseite
    {
      digitalWrite(RELAY_PORT, LOW);
      relay = true;
      delay(2000);
      digitalWrite(RELAY_PORT, HIGH);
      relay = false;
      sendStartPage(client);
      httpMethod = 1;
    } else if (httpRequest.startsWith("GET /3s "))  // Startseite
    {
      digitalWrite(RELAY_PORT, LOW);
      relay = true;
      delay(3000);
      digitalWrite(RELAY_PORT, HIGH);
      relay = false;
      sendStartPage(client);
      httpMethod = 1;
    } else if (httpRequest.startsWith("GET /4s "))  // Startseite
    {
      digitalWrite(RELAY_PORT, LOW);
      relay = true;
      delay(4000);
      digitalWrite(RELAY_PORT, HIGH);
      relay = false;
      sendStartPage(client);
      httpMethod = 1;
    } else if (httpRequest.startsWith("GET /5s "))  // Startseite
    {
      digitalWrite(RELAY_PORT, LOW);
      relay = true;
      delay(5000);
      digitalWrite(RELAY_PORT, HIGH);
      relay = false;
      sendStartPage(client);
      httpMethod = 1;
    } else if (httpRequest.startsWith("GET /6s "))  // Startseite
    {
      digitalWrite(RELAY_PORT, LOW);
      relay = true;
      delay(6000);
      digitalWrite(RELAY_PORT, HIGH);
      relay = false;
      sendStartPage(client);
      httpMethod = 1;
    } else if (httpRequest.startsWith("GET /OFF "))  // Startseite
    {
      digitalWrite(RELAY_PORT, HIGH);
      relay = false;
      sendStartPage(client);
      httpMethod = 1;
    } else if (httpRequest.startsWith("GET /settings "))  // Startseite
    {
      wifiScanCount = scanWiFi();
      sendSettingsPage(client);
      httpMethod = 1;
    } else if (httpRequest.startsWith("GET /save?"))  //Nach Save Request
    {
      saveSettings(httpRequest);
      sendSavedPage(client);
      httpMethod = 5;
    }

    if (httpMethod == 1 || httpMethod == 5) {
      client.flush();
      client.stop();
      if (httpMethod == 5) {
        delay(1000);
        ESP.restart();
      }
      httpMethod = 0;
      //digitalWrite(LED_BUILTIN, HIGH);
    }
  }
}

void saveSettings(String inputData) {
  bool foundStart = false;
  String token = "";

  ssid = "";
  password = "";
  ip = "";
  subnet = "";
  defaultgw = "";
  dns = "";
  ipType = "";
  powerOn = "";
  String mdns = "";

  for (int i = 0; i < inputData.length(); i++) {
    char c = inputData.charAt(i);
    token += c;

    if (foundStart == false && c == '?') {
      foundStart = true;
      token = "";
    } else if (foundStart == true && (c == '&' || c == ' ') && token.startsWith("ssid=")) {
      ssid = token.substring(5, token.length() - 1);
      ;
      ssid = urldecode(ssid);
      token = "";
    } else if (foundStart == true && (c == '&' || c == ' ') && token.startsWith("password=")) {
      password = token.substring(9, token.length() - 1);
      ;
      password = urldecode(password);
      token = "";
    } else if (foundStart == true && (c == '&' || c == ' ') && token.startsWith("ipType=")) {
      ipType = token.substring(7, token.length() - 1);
      ;
      ipType = urldecode(ipType);
      token = "";
    } else if (foundStart == true && (c == '&' || c == ' ') && token.startsWith("ip=")) {
      ip = token.substring(3, token.length() - 1);
      ;
      ip = urldecode(ip);
      token = "";
    } else if (foundStart == true && (c == '&' || c == ' ') && token.startsWith("subnet=")) {
      subnet = token.substring(7, token.length() - 1);
      ;
      subnet = urldecode(subnet);
      token = "";
    } else if (foundStart == true && (c == '&' || c == ' ') && token.startsWith("defaultgw=")) {
      defaultgw = token.substring(10, token.length() - 1);
      ;
      defaultgw = urldecode(defaultgw);
      token = "";
    } else if (foundStart == true && (c == '&' || c == ' ') && token.startsWith("dns=")) {
      dns = token.substring(4, token.length() - 1);
      ;
      dns = urldecode(dns);
      token = "";
    } else if (foundStart == true && (c == '&' || c == ' ') && token.startsWith("mdns=")) {
      mdns = token.substring(5, token.length() - 1);
      mdns = urldecode(mdns);
      token = "";
    } else if (foundStart == true && (c == '&' || c == ' ') && token.startsWith("powerOn=")) {
      powerOn = token.substring(8, token.length() - 1);
      ;
      powerOn = urldecode(powerOn);
      token = "";
    }
  }
  saveEEPROM(ssid, ssid_adr, 40);
  saveEEPROM(password, password_adr, 30);
  saveEEPROM(ip, ip_adr, 15);
  saveEEPROM(subnet, subnet_adr, 15);
  saveEEPROM(defaultgw, defaultgw_adr, 15);
  saveEEPROM(dns, dns_adr, 15);
  saveEEPROM(ipType, ipType_adr, 1);
  saveEEPROM(powerOn, powerOn_adr, 1);
  saveEEPROM(mdns, mDNS_adr, 20);
  EEPROM.commit();

  readValuesFromEeprom();
}

void readValuesFromEeprom() {
  ssid = readEEPROM(ssid_adr, 40);
  password = readEEPROM(password_adr, 30);
  ip = readEEPROM(ip_adr, 15);
  subnet = readEEPROM(subnet_adr, 15);
  defaultgw = readEEPROM(defaultgw_adr, 15);
  dns = readEEPROM(dns_adr, 15);
  ipType = readEEPROM(ipType_adr, 1);
  powerOn = readEEPROM(powerOn_adr, 1);
  mDNS_name = readEEPROM(mDNS_adr, 20);

  if (mDNS_name.length() == 0) {
    String mdns = "myrelaycard";

    saveEEPROM(mdns, mDNS_adr, 20);
    EEPROM.commit();
    readValuesFromEeprom();
  }
}

void saveEEPROM(String in, int adr, int len) {
  clearDataStructure();
  in.toCharArray(data.str, in.length() + 1);
  data.val = len;
  EEPROM.put(adr, data);
}

String readEEPROM(int adr, int len) {
  clearDataStructure();
  data.val = len;
  EEPROM.get(adr, data);
  for (int i = 0; i < len; i++) {
    if (data.str[i] != 255 && data.str[i] != 63) {
      return data.str;
    }
  }
  return "";
}

void clearDataStructure() {
  data.val = 0;
  strncpy(data.str, "", 80);
}

void checkRS232() {
  ESPserialEvent();
  if (stringComplete) {
    if (inputString.startsWith("setON")) {
      digitalWrite(RELAY_PORT, LOW);
      relay = true;
      Serial.println("successful set ON");
    } else if (inputString.startsWith("setOFF")) {
      digitalWrite(RELAY_PORT, HIGH);
      relay = false;
      Serial.println("successful set OFF");
    }

    stringComplete = false;
    inputString = "";
  }
}

void ESPserialEvent() {
  while (Serial.available()) {
    char inChar = (char)Serial.read();
    inputString += inChar;
    if (inChar == '\n') {
      stringComplete = true;
    }
  }
}

int scanWiFi() {
  int networksFound = WiFi.scanNetworks(false, true);
  for (int i = 0; i < networksFound && i < 50; i++) {
    ssidVolume[i] = WiFi.RSSI(i);
  }
  return networksFound;
}

void startWifiAccessPoint() {
  Serial.println();
  Serial.println("Startup as AccessPoint with Name: Relay-Modul");

  IPAddress local_IP(192, 168, 1, 1);
  IPAddress gateway(192, 168, 1, 1);
  IPAddress subnet(255, 255, 255, 0);

  WiFi.softAP("Relay-Modul");
  WiFi.softAPConfig(local_IP, gateway, subnet);
}

bool startWiFiClient() {
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);

  if (ipType.startsWith("1")) {
    IPAddress ipAddress;
    ipAddress.fromString(ip);
    IPAddress gatewayAddress;
    gatewayAddress.fromString(defaultgw);
    IPAddress dnsAddress;
    dnsAddress.fromString(dns);
    IPAddress subnetAddress;
    subnetAddress.fromString(subnet);

    WiFi.config(ipAddress, gatewayAddress, dnsAddress, subnetAddress);
  }

  WiFi.begin(ssid, password);

  int count = 0;
  while (WiFi.status() != WL_CONNECTED && count < 20) {
    delay(500);
    Serial.print(".");
    count++;
  }

  if (count >= 20) {
    return false;
  }

  Serial.println("");
  Serial.print("WiFi connected to: ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  if (mDNS_name.length() > 0) {
    Serial.print("mDNS address: http://");
    Serial.print(mDNS_name);
    Serial.println(".local");
  }

  return true;
}

String urldecode(String str) {

  String encodedString = "";
  char c;
  char code0;
  char code1;
  for (int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (c == '+') {
      encodedString += ' ';
    } else if (c == '%') {
      i++;
      code0 = str.charAt(i);
      i++;
      code1 = str.charAt(i);
      c = (h2int(code0) << 4) | h2int(code1);
      encodedString += c;
    } else {
      encodedString += c;
    }

    yield();
  }
  return encodedString;
}

unsigned char h2int(char c) {
  if (c >= '0' && c <= '9') {
    return ((unsigned char)c - '0');
  }
  if (c >= 'a' && c <= 'f') {
    return ((unsigned char)c - 'a' + 10);
  }
  if (c >= 'A' && c <= 'F') {
    return ((unsigned char)c - 'A' + 10);
  }
  return (0);
}

void sendSavedPage(WiFiClient client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:text/html");
  client.println();
  client.println("<HTML><HEAD>");
  client.println("<title>NTP Client</title>");
  sendFavicon(client);
  if (ipType.startsWith("1")) {
    client.print("<meta http-equiv=\"refresh\" content=\"10; URL=http://");
    client.print(ip);
    client.println("/\">");
  }
  client.println("</HEAD><BODY>");
  client.println("<h2>SETTINGS saved</h2>");
  if (ipType.startsWith("1")) {
    client.println("Please wait, in 10 seconds you will be redirected to the Start Page...");
    client.print("<br><br><a href=\"http://");
    client.print(ip);
    client.println("/\">Start Page</a>");
  } else {
    client.println("Please enter the IP which has set by DHCP from your Router or the mDNS name followed by .local");
  }

  sendStyle(client);
  client.println("</BODY></HTML>");
  client.println();
  client.flush();
}

void sendFavicon(WiFiClient client) {
  client.print("<link href=\"data:image/x-icon;base64,AAABAAMAEBAAAAEAIABoBAAANgAAACAgAAABACAAKBEAAJ4EAAAwMAAAAQAgAGgmAADGFQAAKAAAABAAAAAgAAAAAQAgAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAKenAH6pqQD4qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qakA+KenAH6pqQD4qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+pqQD4qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/+fOev+0sBX/y71D/+bNeP/ax2L/4stx/+zRhf+wrQz/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP/nznr/yLs8/9rGYP/Vw1b/6tCB/8i8Pv/w04z/wbgw/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/1cNW/9rGYP/JvD7/5s14/9XEV//Zxl//28di/9TCU/+vrQv/qqoA/6qqAP+qqgD/qqoA/6qqAP+vrAr/0MBN/8K4Mf/s0YX/trEa//TWlf/DuTL/6M99/8i8PP/t0of/7dKH/6+tC/+qqgD/qqoA/6qqAP+qqgD/rawH/+XNd/+xrg///Nul/7CtDf/CuDD/urMh/8O5M/+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+/tiv/3clo/+7Tif/nz3z/48xy/721Jv+/tir/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qakA+KqqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qakA+KmpAH2pqQD4qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qakA+KenAH4AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAKAAAACAAAABAAAAAAQAgAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAP//AAGoqAB8qqoA56mpAP6qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+pqQD+qqoA56ioAHz//wABqKgAfKqqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6ioAHyoqADnqqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qKgA56mpAP6qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+pqQD+qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP/YxVz/28di/7SwFf+qqgD/qqoA/82+Rv/gym3/xro4/6yrBf/Yxl3/3chn/7+2K//bx2L/28di/62sCP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/+vRgv//3ar/y71C/6qqAP+4shz//92q///dqv/z1pL/5898///dqv/72qL/8tWR//7cqf//3ar/v7Yq/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/2MZe///dqv/dyGb/qqoA/721Jv//3ar/+dmf/6qqAP/lzXf//92q/9/KbP+rqwP/8NOM///dqv/QwU7/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP/Guzn//92q/+/Ti/+qqgD/r60K//7cqf//3ar/sq4R/8i7PP//3ar/7tKJ/6qqAP/SwlL//92q/+PMcv+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/7SwFf//3ar//tyn/66sCP+qqgD/8tSP///dqv/EuTX/trEY///dqv/93Kb/rasG/8G3Lv//3ar/9daW/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA//fYmv//3ar/v7Yq/6qqAP/fymv//92q/9bEWf+qqgD/+Nmd///dqv+9tSf/r60K//7cqf//3an/sq4Q/6+tC/+8tCT/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+rqgL/1sRa/7axGf+qqgD/5c12///dqv/RwU7/qqoA/82/R///3ar/6M99/6qqAP/nznr//92q/8/ASv+qqgD/8tSP///dqv/iy3H/3slp//7cqf+5sx//qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/721J///3ar/trEX/6qqAP/SwlH//92q/+PMc/+qqgD/urQi//jZnP/z1ZL/qqoA/9LCUP/42Zz/3Mhl/6qqAP/byGT/+Nmc/93IZ//nz3z/8dSO/7GuD/+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/ubMe///dqv/Atyz/qqoA/8C3Lf//3ar/9teY/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/8NSM/+fPev+sqwX/r6wK//7cqP//3ar/tbAW/7mzHv/BuC7/9NaU/9jFW/++tin/+dmf/8C3Lf+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+3sRr/8tWQ//bYmv/jzHP//Nuk///dqv/+3Kj//92q///dqv//3ar/1cNW/7mzH//u0oj/u7Qi/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+rqwP/xbo3/9nGXv/gym3/38pr/9fFW//LvkT/xbo2/8q9Qf+8tSX/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qakA/qqqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6mpAP6oqADnqqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qKgA56qqAHuqqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+oqAB8//8AAaqqAHupqQDmqakA/qqqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6mpAP6pqQDmqKgAfP//AAEAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAKAAAADAAAABgAAAAAQAgAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAACfnwAQqqoAXaqqAMapqQD3qakA/qqqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6mpAP6pqQD3qqoAyampAGKfnwAQAAAAALKyAAqpqQCVqqoA9qqqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAPmpqQCVn58AEKenAFeqqgD5qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD5qakAYqioAMWqqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qKgAyaioAPaqqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA9qmpAP6qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qakA/qqqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/8K4Mf/EujX/xLo1/7OvE/+qqgD/qqoA/6qqAP+urAj/wLcs/8/AS//CuDD/sa4P/6qqAP+zrxL/ybw//82+R/+7tCP/sa4P/8S6Nf/EujX/xLo1/6yrBf+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/+zRhf//3ar//92q/9TCVP+qqgD/qqoA/6uqAv/PwEn/+tqi///dqv/93Kb/6M99/7y1Jf/iy3H//tyo//7dqf/42Z3/4cpu//7cqf//3ar//92q/7+2Kv+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/9bEWf//3ar//92q/+nQgP+qqgD/qqoA/66sCf/mznn//92q///dqv/y1ZD/7NKG//XXl//62qD//92q//3cpv/qz4D/8tSQ//7c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AP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/x7s6///dqv/nznv/sa4O/6qqAP+qqgD/08JS///dqv//3ar/69CD/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/s68T//vaov/215n/urQh/6qqAP+qqgD/v7Yr///dqv//3ar/+9qi/62sCP+qqgD/qqoA/6qqAP+1sRf/zsBJ/8S5Nf+rqgL/sq4Q/8/ASv/CuDD/rKsE/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/9bEWv/+3Kn/69GE/7KuEP+qqgD/rasG//zbpv//3ar//92q/8C3Lf+4shz/yLs9/9HBTv/nznv//dyn//XXlv+5sh3/2MZd//3cp//y1ZH/tbAX/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6+tCv/lzXb//Nul//nZoP/eyWn/0MBM//fYmf//3ar//92q//zbpf//3ar//92q///dqv//3ar//92q//PVkv+0sBX/0MBM//jZnf/q0IH/s68S/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+zrxP/0cFP/+rRgv/62qH//typ///dqv//3ar//92q//7cqf/726P/9NaV/+/TjP/w1Iz/99ia/+LLb/+qqgD/rKsG/8O5Mv+5sx7/qqoB/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoB/6+tC/+1sBb/wLct/8m8Pv/KvUH/xrs5/761KP+0sBX/sq4Q/7CuDf+wrg3/sq8R/7axGv+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6mpAP6qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qakA/qioAPaqqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA9qmpAMGqqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qKgAxqqqAFGqqgD2qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD2qqoAXbKyAAqpqQCVqqoA9qqqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAPmpqQCVn58AEAAAAACysgAKqqoAUampAMGpqQD1qakA/qqqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6qqAP+qqgD/qqoA/6mpAP6pqQD1qakAxKenAFeysgAKAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA==\" rel=\"icon\" type=\"image/x-icon\" />");
}

void sendStartPage(WiFiClient client) {
  //Serial.println(ESP.getFreeHeap());
  //Serial.println(ESP.getHeapFragmentation());
  client.println("HTTP/1.1 200 OK");
  //client.println("Content-type:text/html");
  client.println();
  //lient.println("<HTML><HEAD>");
  client.println("<!DOCTYPE html><html>");
  client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
  client.println("<meta charset=\"utf-8\">"); //<meta http-equiv=\"refresh\" content=\"10\">");
  client.println("<title>Dooropener</title>");
  sendFavicon(client);
  client.println("</HEAD><BODY>");
  sendStyle(client);
  client.println("<h1>Door</h1>");
  client.println("<hr>");
  client.println("<h2>Dooropener:</h2>");
  client.print("<input type=\"submit\" value=\"1s\" style=\"width:90px;height:55px\" onClick=\"location.href='/1s'\"");
  if (relay) {
    client.print(" disabled");
  }
  client.println(">");
  client.print("&nbsp;");
  client.print("<input type=\"submit\" value=\"2s\" style=\"width:90px;height:55px\" onClick=\"location.href='/2s'\"");
  if (relay) {
    client.print(" disabled");
  }
  client.println(">");
  client.print("&nbsp;");
  client.print("<input type=\"submit\" value=\"3s\" style=\"width:90px;height:55px\" onClick=\"location.href='/3s'\"");
  if (relay) {
    client.print(" disabled");
  }
  client.println(">");
  client.print("&nbsp;");
  client.print("<input type=\"submit\" value=\"4s\" style=\"width:90px;height:55px\" onClick=\"location.href='/4s'\"");
  if (relay) {
    client.print(" disabled");
  }
  client.println(">");
  client.print("&nbsp;");
  client.print("<input type=\"submit\" value=\"5s\" style=\"width:90px;height:55px\" onClick=\"location.href='/5s'\"");
  if (relay) {
    client.print(" disabled");
  }
  client.println(">");
  client.print("&nbsp;");
  client.print("<input type=\"submit\" value=\"6s\" style=\"width:90px;height:55px\" onClick=\"location.href='/6s'\"");
  if (relay) {
    client.print(" disabled");
  }
  client.println(">");
  // client.print("&nbsp;");
  // client.print("<input type=\"submit\" value=\"OFF\" style=\"width:200px;height:55px\" onClick=\"location.href='/OFF'\"");
  // if (relay) {
  //   client.print(" disabled");
  // }
  //client.println(">");
  client.println("<br><br><hr>");
  client.println("<h2>Doorbell:</h2>");
  client.print("&nbsp;");
  client.print("<input type=\"submit\" value=\"The doorbell rang\" style=\"width:300px;height:55px\"");
  if (ledOn) {
    client.print(" disabled");
  }
  client.println(">");
  //client.print("<br><a href=\"/settings\">Settings</a>");
  client.println("<br><br><hr>");
  client.println("</BODY></HTML>");
  client.println();
  client.flush();
}

void sendSettingsPage(WiFiClient client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:text/html");
  client.println();
  client.println("<HTML><HEAD>");
  client.println("<title>WiFi Relay</title>");
  sendFavicon(client);
  client.println("</HEAD><BODY>");
  sendStyle(client);
  client.println("<h1>WiFi Relay Settings:</h1>");
  client.println("<hr>");

  client.println("<h2>WiFi:</h2>");
  client.println("<form action=\"/save\" method=\"GET\">");
  client.println("<table><tr><td>");
  client.println("SSID:</td><td>");
  client.println("<select name=\"ssid\" size=\"1\">");
  for (int i = 0; i < wifiScanCount; i++) {
    client.print("<option");
    if (ssid == WiFi.SSID(i)) {
      client.print(" selected=\"selected\"");
    }
    client.print(" value=\"");
    client.print(WiFi.SSID(i).c_str());
    client.print("\">");
    client.print(WiFi.SSID(i).c_str());
    client.print("    (");
    client.print(ssidVolume[i]);
    client.print("dBm)");
    client.println("</option>");
  }
  client.println("</select>");
  client.println("</td></tr><tr><td>");
  client.println("Password:</td><td>");
  client.print("<input type=\"password\" name=\"password\" maxlength=\"30\" size=\"30\" value=\"");
  client.print(password);
  client.println("\"></td></tr>");
  client.println("</table>");
  client.println("<br><hr>");

  client.println("<h2>IP:</h2>");
  client.println("<table><tr><td>");
  client.println("Type:</td><td>");
  client.println("<select id=\"ipType\" name=\"ipType\" size=\"1\" onchange=\"changeIPType()\">");
  client.print("<option");
  if (ipType.startsWith("1")) {
    client.print(" selected=\"selected\"");
  }
  client.print(" value=\"1\">Static</option>");
  client.print("<option ");
  if (ipType.startsWith("2")) {
    client.print(" selected=\"selected\"");
  }
  client.println(" value=\"2\">DHCP</option>");
  client.println("</select>");
  client.println("</td></tr><tr><td>");
  client.println("IP:</td><td>");
  client.print("<input type=\"text\" id=\"ip\" name=\"ip\" minlength=\"7\" maxlength=\"15\" size=\"15\" pattern=\"^((\\d{1,2}|1\\d\\d|2[0-4]\\d|25[0-5])\\.){3}(\\d{1,2}|1\\d\\d|2[0-4]\\d|25[0-5])$\" value=\"");
  client.print(ip);
  client.println("\">");
  client.println("</td></tr><tr><td>");
  client.println("Subnet:</td><td>");
  client.print("<input type=\"text\" id=\"subnet\" name=\"subnet\" minlength=\"7\" maxlength=\"15\" size=\"15\" pattern=\"^((\\d{1,2}|1\\d\\d|2[0-4]\\d|25[0-5])\\.){3}(\\d{1,2}|1\\d\\d|2[0-4]\\d|25[0-5])$\" value=\"");
  client.print(subnet);
  client.println("\">");
  client.println("</td></tr><tr><td>");
  client.println("Default Gateway:</td><td>");
  client.print("<input type=\"text\" id=\"defaultgw\" name=\"defaultgw\" minlength=\"7\" maxlength=\"15\" size=\"15\" pattern=\"^((\\d{1,2}|1\\d\\d|2[0-4]\\d|25[0-5])\\.){3}(\\d{1,2}|1\\d\\d|2[0-4]\\d|25[0-5])$\" value=\"");
  client.print(defaultgw);
  client.println("\"/>");
  client.println("</td></tr><tr><td>");
  client.println("DNS Server:</td><td>");
  client.print("<input type=\"text\" id=\"dns\" name=\"dns\" minlength=\"7\" maxlength=\"15\" size=\"15\" pattern=\"^((\\d{1,2}|1\\d\\d|2[0-4]\\d|25[0-5])\\.){3}(\\d{1,2}|1\\d\\d|2[0-4]\\d|25[0-5])$\" value=\"");
  client.print(dns);
  client.println("\"/>");

  client.println("</td></tr><tr><td>");
  client.println("mDNS Name:</td><td>");
  client.print("<input type=\"text\" id=\"mdns\" name=\"mdns\" minlength=\"3\" maxlength=\"20\" size=\"22\" pattern=\"[a-zA-Z]{1}[a-zA-Z0-9]{1,19}\" title=\"Enter the address to find the module in a browser. To open the website, type the extered name in the Browser, followed by '.local'\" value=\"");
  client.print(mDNS_name);
  client.println("\"/>.local");

  client.println("</td></tr></table>");

  client.println("<br><hr>");

  client.println("<h2>Relay:</h2>");
  client.println("<table><tr><td>");
  client.println("Relay on Power on:</td><td>");
  client.println("<select id=\"powerOn\" name=\"powerOn\" size=\"1\">");
  client.print("<option ");
  if (powerOn == "1") {
    client.print(" selected=\"selected\"");
  }
  client.print(" value=\"1\">ON</option>");
  client.print("<option ");
  if (powerOn != "1") {
    client.print(" selected=\"selected\"");
  }
  client.println(" value=\"2\">OFF</option>");
  client.println("</select>");
  client.println("</td></tr></table><br><hr>");

  client.println("<br><input type=\"submit\" value=\"Save\"/>");
  client.println("&nbsp;");
  client.println("<input type=\"button\" value=\"Cancel\" onClick=\"location.href='/'\">");
  client.println("</form>");


  client.println("<script>");
  client.println("function changeIPType() {");
  client.println("  if(document.getElementById(\"ipType\").value == 2) {");
  client.println("    document.getElementById(\"ip\").disabled = true");
  client.println("    document.getElementById(\"ip\").value = ''");
  client.println("    document.getElementById(\"subnet\").disabled = true");
  client.println("    document.getElementById(\"subnet\").value = ''");
  client.println("    document.getElementById(\"defaultgw\").disabled = true");
  client.println("    document.getElementById(\"defaultgw\").value = ''");
  client.println("    document.getElementById(\"dns\").disabled = true");
  client.println("    document.getElementById(\"dns\").value = ''");
  client.println("   }");
  client.println("  else {");
  client.println("    document.getElementById(\"ip\").disabled = false");
  client.println("    document.getElementById(\"subnet\").disabled = false");
  client.println("    document.getElementById(\"defaultgw\").disabled = false");
  client.println("    document.getElementById(\"dns\").disabled = false");
  client.println("  }");
  client.println("}");
  client.println("changeIPType();");
  client.println("</script>");
  client.println("</BODY></HTML>");
  client.println();
  client.flush();
}

void sendStyle(WiFiClient client) {
  client.println("<style>");
  client.println("input:invalid {color: red;}");
  client.println("body ");
  client.println("{");
  client.println("  background: #004270;");
  client.println("  background: -webkit-linear-gradient(to right, #aaddff, #00aaaa);");
  client.println("  background: linear-gradient(to right, #aaddff, #00aaaa);");
  client.println("  font-family: Helvetica Neue,Helvetica,Arial,sans-serif;");
  client.println("}");
  client.println("input, select");
  client.println("{");
  client.println("  border: none;");
  client.println("  color: black;");
  client.println("  text-align: left;");
  client.println("  text-decoration: none;");
  client.println("  display: inline-block;");
  client.println("  font-size: 16px;");
  client.println("  margin: 4px 2px;");
  client.println("}");
  client.println("input[type=text],input[type=password],select");
  client.println("{");
  client.println("  background-color: white;");
  client.println("  border-radius: 2px;");
  client.println("  padding: 2px 6px;");
  client.println("  border: 1px solid #004270;");
  client.println("}");
  client.println("input[type=text]:disabled");
  client.println("{");
  client.println("  background-color: lightgrey;");
  client.println("  border-radius: 2px;");
  client.println("  padding: 2px 6px;");
  client.println("  border: 1px solid #004270;");
  client.println("}");
  client.println("input[type=submit],input[type=button]");
  client.println("{");
  client.println("  background-color: white;");
  client.println("  border-radius: 4px;");
  client.println("  padding: 16px 32px;");
  client.println("  text-align: center;");
  client.println("  -webkit-transition-duration: 0.4s;");
  client.println("  transition-duration: 0.4s;");
  client.println("  cursor: pointer;");
  client.println("  border: 2px solid #000000;");
  client.println("}");
  client.println("input[type=submit]:hover, input[type=button]:hover");
  client.println("{");
  client.println("  background-color: rgb(130, 209, 235);");
  client.println("  color: white;");
  client.println("}");
  client.println("input[type=submit]:disabled");
  client.println("{");
  client.println("  background-color: #00aaaa;");
  client.println("  color: white;");
  client.println("}");
  client.println("</style>");
}

// void inputloop() {
//     // Nur alle 100 ms lesen
//     if (millis() - lastReadTime >= readInterval) {
//         lastReadTime = millis();
//         inputValue = digitalRead(INPUT_PORT);

//         if (inputValue == HIGH) {
//             // Türglocke gedrückt
//             Serial.println("Doorbell pressed ...");
//             doorbellPressed = true;
//             pressStartTime = millis();  // Zeitpunkt speichern
//             // Hier kannst du die LED einschalten, wenn gewünscht
//             digitalWrite(LED_BUILTIN, LOW); // LED einschalten
//             ledOn = true;                // LED-Status setzen
//         }
//     }

//     // Überprüfen, ob die Taste losgelassen wurde
//     if (inputValue == LOW) {
//         // Prüfen, ob die Druckzeit abgelaufen ist
//         if (millis() - pressStartTime >= buttonPressDelay) {
//             // Hier kannst du den Status zurücksetzen
//             doorbellPressed = false;
//             Serial.println("Doorbell released ...");
//         }
//     }

//     // Überprüfen, ob die LED für 30 Sekunden leuchten soll
//     if (ledOn && (millis() - pressStartTime >= ledDuration)) {
//         digitalWrite(LED_BUILTIN, HIGH); // LED ausschalten
//         ledOn = false;               // LED-Status zurücksetzen
//     }

// }


// void inputloop() {
//     if (millis() - lastReadTime >= readInterval) {
//         lastReadTime = millis();
//         int inputValue = digitalRead(INPUT_PORT);

//         // Überprüfen, ob der Eingang HIGH ist
//         if (inputValue == HIGH) {
//             lastInputTime = millis(); // Zeit der letzten Eingabe aktualisieren
//             if (!doorbellPressed) {
//                 Serial.println("Doorbell pressed ...");
//                 doorbellPressed = true;
//                 pressStartTime = millis();
//                 digitalWrite(LED_BUILTIN, LOW); // LED einschalten
//                 ledOn = true;                // LED-Status setzen
//             }
//         }

//         if (doorbellPressed) {
//             unsigned long pressDuration = millis() - pressStartTime; // Dauer des Drucks

//             // Wenn die Taste losgelassen wird
//             if (inputValue == LOW) {
//                 doorbellPressed = false;
//                 Serial.println("Doorbell released ...");

//                 // Erkenne den Drucktyp
//                 int length = strlen(morseCode);
//                 if (pressDuration < 300) {
//                     if (length < sizeof(morseCode) - 1) {
//                         morseCode[length] = '.'; // Kurzer Druck
//                         morseCode[length + 1] = '\0'; // Nullterminierung
//                     }
//                 } else {
//                     if (length < sizeof(morseCode) - 1) {
//                         morseCode[length] = '-'; // Langer Druck
//                         morseCode[length + 1] = '\0'; // Nullterminierung
//                     }
//                 }
//                 Serial.print("Morse Code: ");
//                 Serial.println(morseCode);
                
//                 // Überprüfen, ob der Morsecode "...-..." (Code) ist
//                 if (strcmp(morseCode, "...-...") == 0) {
//                     Serial.println("Code detected! Activating GPIO 0...");
//                     digitalWrite(RELAY_PORT, LOW); // Relay aktivieren
//                     delay(5000); // Aktivierung für 500 ms (optional)
//                     digitalWrite(RELAY_PORT, HIGH); // Relay deaktivieren
//                     memset(morseCode, 0, sizeof(morseCode)); // Morsecode zurücksetzen
//                 }
//             }
//         }

//         // Timeout: Wenn eine Sekunde lang keine Eingabe erfolgt, Morsecode zurücksetzen
//         if (millis() - lastInputTime >= timeoutDuration) {
//             memset(morseCode, 0, sizeof(morseCode)); // Morsecode zurücksetzen
//             Serial.println("Timeout: Morse code reset.");
//         }

//         // Überprüfen, ob die LED für 30 Sekunden leuchten soll
//         if (ledOn && (millis() - pressStartTime >= ledDuration)) {
//             digitalWrite(LED_BUILTIN, HIGH); // LED ausschalten
//             ledOn = false;                 // LED-Status zurücksetzen
//         }
//     }
// }

void inputloop() {
    if (millis() - lastReadTime >= readInterval) {
        lastReadTime = millis();
        int inputValue = digitalRead(INPUT_PORT);

        // Überprüfen, ob der Eingang HIGH ist
        if (inputValue == HIGH) {
            lastInputTime = millis(); // Zeit der letzten Eingabe aktualisieren
            if (!doorbellPressed) {
                Serial.println("Doorbell pressed ...");
                doorbellPressed = true;
                pressStartTime = millis();
                digitalWrite(LED_BUILTIN, HIGH); // LED einschalten
                ledOn = true; // LED-Status setzen
            }
        }

        if (doorbellPressed) {
            // Wenn die Taste losgelassen wird
            if (inputValue == LOW) {
                doorbellPressed = false;
                Serial.println("Doorbell released ...");

                // Dauer des Drucks hier berechnen
                unsigned long pressDuration = millis() - pressStartTime; // Dauer des Drucks

                // Erkenne den Drucktyp
                int length = strlen(morseCode);
                if (pressDuration < 300) {
                    if (length < sizeof(morseCode) - 1) {
                        morseCode[length] = '.'; // Kurzer Druck
                        morseCode[length + 1] = '\0'; // Nullterminierung
                    }
                } else {
                    if (length < sizeof(morseCode) - 1) {
                        morseCode[length] = '-'; // Langer Druck
                        morseCode[length + 1] = '\0'; // Nullterminierung
                    }
                }
                Serial.print("Morse Code: ");
                Serial.println(morseCode);

                // Überprüfen, ob der Morsecode "...-..." (Code) ist
                if (strcmp(morseCode, "..-..-") == 0) {
                    Serial.println("Code detected! Activating Relay...");
                    digitalWrite(RELAY_PORT, LOW); // Relay aktivieren
                    relayActivationTime = millis(); // Zeit der Aktivierung speichern
                    relayActive = true; // Relay-Status setzen
                    memset(morseCode, 0, sizeof(morseCode)); // Morsecode zurücksetzen
                }
            }
        }

        // Überprüfen, ob das Relay aktiv ist und nach 7 Sekunden deaktivieren
        if (relayActive && (millis() - relayActivationTime >= 7000)) {
            digitalWrite(RELAY_PORT, HIGH); // Relay deaktivieren
            relayActive = false; // Relay-Status zurücksetzen
        }

        // Timeout: Wenn 5 Sekunde lang keine Eingabe erfolgt, Morsecode zurücksetzen
        if (millis() - lastInputTime >= timeoutDuration) {
            memset(morseCode, 0, sizeof(morseCode)); // Morsecode zurücksetzen
            Serial.println("Timeout: Morse code reset.");
        }

        // Überprüfen, ob die LED für 30 Sekunden leuchten soll
        if (ledOn && inputValue == LOW && (millis() - pressStartTime >= ledDuration)) {
            digitalWrite(LED_BUILTIN, LOW); // LED ausschalten
            ledOn = false; // LED-Status zurücksetzen
        }
    }
}
