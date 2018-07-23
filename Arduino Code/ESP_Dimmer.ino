#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <WebSocketsServer.h>
#include <Hash.h>
#include <String.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
// called this way, it uses the default address 0x40
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver();

// WiFi Configuration (hard-coded at the moment)
static const char ssid[] = "__Your-WiFi-SSID__";
static const char password[] = "__Your-WiFi-Password-Here__";

// mDNS hostname for this device
// You should be able to call http://dimmer.iot
static const char dns_hostname[] = "dimmer";

// Channel Configuration (pins)
#define CH1_PIN         0
#define CH2_PIN         1
#define CH3_PIN         2
#define EN_PIN          4

#define DEBUG_BUILD 0

// Global variables
volatile uint32_t milis_update = 0;
volatile uint32_t debug_update = 0;
volatile uint16_t ch1_target   = 0;  // Channel target value (not used yet)
volatile uint16_t ch2_target   = 0;  // Channel target value (not used yet)
volatile uint16_t ch3_target   = 0;  // Channel target value (not used yet)
volatile uint16_t ch1_current  = 0;  // Channel current value
volatile uint16_t ch2_current  = 0;  // Channel current value
volatile uint16_t ch3_current  = 0;  // Channel current value


// GPIO#0 is for Adafruit ESP8266 HUZZAH board. Your board LED might be on 13.
const int LEDPIN = 0;

// Static function definitions
static void writeLED(uint8_t channel, uint16_t value);

// Initialize classes
MDNSResponder mdns;
ESP8266WiFiMulti WiFiMulti;
ESP8266WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

// -- BEGIN Index.html
static const char PROGMEM INDEX_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name = "viewport" content = "width = device-width, initial-scale = 1.0, maximum-scale = 1.0, user-scalable=0">
<title>IoT Dimmer - Sasa Karanovic</title>
<style>
body { background: #262830 url('http://sasakaranovic.com/iotdimmer/bg.png') repeat; font-family: Arial, Helvetica, Sans-Serif; Color: #fffff9; }
#container { width: 80%; max-width: 450px; margin: auto; }
.bulb { display: block; clear: none; width: 32px; height: 32px; padding-bottom: 0.5em; background-attachment: fixed; background-position: center; background-repeat: no-repeat; }
.bulbOn { background: transparent url('http://sasakaranovic.com/iotdimmer/bulbON.png') top left no-repeat; float: right; }
.bulbOff{ background: transparent url('http://sasakaranovic.com/iotdimmer/bulbOFF.png') top left no-repeat; float: left; }
h1 {  display: block; font-size: 2em; margin-top: 0.67em; margin-bottom: 0.67em; margin-left: 0; margin-right: 0; font-weight: bold; text-align: center; }
.slidecontainer {width: 100%; }
.slider {width: 100%; margin: 0 0 3em 0; }
  .buttonOff { background-color: #f44336; }
  .buttonOn{ background-color: #4CAF50; }
  a { 
  background-color: #212121;
    border: none;
    color: white;
    padding: 15px 32px;
  margin-right: 1em;
    text-align: center;
    text-decoration: none;
    display: inline-block;
    font-size: 16px;
  border-radius: 4px;
  }
</style>
<script>
var websock;
function start() {
websock = new WebSocket('ws://' + window.location.hostname + ':81/');
websock.onopen = function(evt) { console.log('websock open'); };
websock.onclose = function(evt) { console.log('websock close'); };
websock.onerror = function(evt) { console.log(evt); };
websock.onmessage = function(evt) {
console.log(evt);
var ch1 = document.getElementById('ch1');
var ch2 = document.getElementById('ch2');
var ch3 = document.getElementById('ch3');
var ch_values = JSON.parse(evt.data);
ch1.value = ch_values.ch1;
ch2.value = ch_values.ch2;
ch3.value = ch_values.ch3;
};
}
function setAll(val) {
var dat = 'ch0:'+ val
websock.send(dat);
}
function updateSlider(e) {
var dat = e.id +':'+ e.value
websock.send(dat);
}
</script>
</head>
<body onload="javascript:start();">
<div id="container">
<h1>Grooovy Baby</h1>
<div class="slidecontainer">
<div class="bulb bulbOn"></div>
<div class="bulb bulbOff"></div>
<input id="ch1" type="range" min="1" max="4095" value="0" class="slider" onchange="updateSlider(this)">
</div>
<div class="slidecontainer">
<div class="bulb bulbOn"></div>
<div class="bulb bulbOff"></div>
<input id="ch2" type="range" min="1" max="4095" value="0" class="slider" onchange="updateSlider(this)">
</div>
<div class="slidecontainer" style="display:none; visibility: hidden">
<div class="bulb bulbOn"></div>
<div class="bulb bulbOff"></div>
<input id="ch3" type="range" min="1" max="4095" value="0" class="slider" onchange="updateSlider(this)">
</div>
<div class="slidecontainer" style="text-align: center">
<a href="#" class="buttonOff" onClick="setAll(1)">Turn Off</a> <a href="#" onClick="setAll(2049)">Set to 50%</a> <a href="#" class="buttonOn"  onClick="setAll(4096)">Turn On</a>
</div>
</div>
</body>
</html>
)rawliteral";
// -- END Index.html

// WebSocket Event Handler
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length)
{
  char buffer[40] = {0};
  String content((char*)payload);
  #if DEBUG_BUILD
  Serial.printf("webSocketEvent(%d, %d, ...)\r\n", num, type);
  #endif
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected!\r\n", num);
      break;
    // --When new connection is established
    case WStype_CONNECTED:
      {
        IPAddress ip = webSocket.remoteIP(num);
        sprintf(buffer, "{\"ch1\":%d,\"ch2\":%d,\"ch3\":%d}", ch1_target,  ch2_target,  ch3_target);
        #if DEBUG_BUILD
        Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\r\n", num, ip[0], ip[1], ip[2], ip[3], payload);
        #endif
        // Send the current LED status
        webSocket.sendTXT(num, buffer, strlen(buffer));
      }
      break;
    // -- This is what we get from WebSocket
    case WStype_TEXT:
      #if DEBUG_BUILD
      Serial.printf("[%u] get Text: %s\r\n", num, payload);
      #endif

      // Check if data is for Channel1
      if(content.indexOf("ch1:") >= 0)
      {
        content.remove(0,4);  // Remove first 3 characters
        #if DEBUG_BUILD
        Serial.print("Channel 1 value: ");
        Serial.println(content);
        #endif
        writeLED(1, content.toInt());
      }
      // Check if data is for Channel2
      else if(content.indexOf("ch2:") >= 0)
      {
        content.remove(0,4);  // Remove first 3 characters
        #if DEBUG_BUILD
        Serial.print("Channel 2 value: ");
        Serial.println(content);
        #endif
        writeLED(2, content.toInt());
      }
      // Check if data is for Channel3
      else if(content.indexOf("ch3:") >= 0)
      {
        content.remove(0,4);  // Remove first 3 characters
        #if DEBUG_BUILD
        Serial.print("Channel 3 value: ");
        Serial.println(content);
        #endif
        writeLED(3, content.toInt());
      }
      else if(content.indexOf("ch0:") >= 0)
      {
        content.remove(0,4);  // Remove first 3 characters
        #if DEBUG_BUILD
        Serial.print("Channel 3 value: ");
        Serial.println(content);
        #endif
        writeLED(0, content.toInt());
      }
      else
      {
        Serial.println("Unknown command?!");
      }

      // send data to all connected clients
      sprintf(buffer, "{\"ch1\":%d,\"ch2\":%d,\"ch3\":%d}", ch1_target,  ch2_target,  ch3_target);
      webSocket.broadcastTXT(buffer, strlen(buffer));
      break;
    // -- Binary?
    case WStype_BIN:
      #if DEBUG_BUILD
      Serial.printf("[%u] get binary length: %u\r\n", num, length);
      #endif
      hexdump(payload, length);

      // echo data back to browser
      webSocket.sendBIN(num, payload, length);
      break;
    default:
      Serial.printf("Invalid WStype [%d]\r\n", type);
      break;
  }
}

// Function handler when http root is requested
void handleRoot()
{
  server.send_P(200, "text/html", INDEX_HTML);
}

// Function handler for invalid (404) page
void handleNotFound()
{
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

// Update Channel Value
static void writeLED(uint8_t channel, uint16_t value)
{
  #if DEBUG_BUILD
  Serial.printf("Channel%d=%d\n", channel, value);
  #endif

  if(value > 4095)
  {
    #if DEBUG_BUILD
    Serial.printf("Invalid value passed for channel %d=%d\n!", channel, value);
    #endif
    value = 4095;
  }

  //Determine which channel needs to be updated
  switch(channel)
  {
    case 0:
      ch1_target = value;
      pwm.setPWM(CH1_PIN, value, 0);

      ch2_target = value;
      pwm.setPWM(CH2_PIN, value, 0);

      ch3_target = value;
      pwm.setPWM(CH3_PIN, value, 0);

    break;

    case 1:
    if(ch1_target != val)
    {
      ch1_target = value;
      pwm.setPWM(CH1_PIN, value, 0);
    }
    break;

    case 2:
    if(ch2_target != value)
    {
      ch2_target = value;
      pwm.setPWM(CH2_PIN, value, 0);
    }
    break;

    case 3:
    if(ch3_target != value)
    {
      ch3_target = value;
      pwm.setPWM(CH3_PIN, value, 0);
    }
    break;

    default:
    Serial.printf("Invalid channel selected (%d)! [Valid channels are 1-3]\r\n", channel);
    break;
  }
}

// SetUp ESP8266
void setup()
{
  pinMode(EN_PIN, OUTPUT);
  digitalWrite(EN_PIN, LOW);
  
  Serial.begin(115200);

  //Serial.setDebugOutput(true);

  Serial.println();
  Serial.println();
  Serial.println();

  for(uint8_t t = 4; t > 0; t--) {
    Serial.printf("[SETUP] BOOT WAIT %d...\r\n", t);
    Serial.flush();
    delay(1000);
  }

  WiFiMulti.addAP(ssid, password);

  while(WiFiMulti.run() != WL_CONNECTED) {
    Serial.print(".");
    delay(100);
  }

  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Setup mDNS to allow accessing this unit through http://DNS_NAME.local
  if (mdns.begin(dns_hostname, WiFi.localIP())) {
    Serial.println("MDNS responder started");
    mdns.addService("http", "tcp", 80);
    mdns.addService("ws", "tcp", 81);
  }
  else {
    Serial.println("MDNS.begin failed");
  }
  Serial.printf("Connect to http://%s.local or http://", dns_hostname);
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.onNotFound(handleNotFound);

  server.begin();

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  Serial.println("Setting up I2C on D3 and D4");
  Wire.begin(D3, D4); /* join i2c bus with SDA=D1 and SCL=D2 of NodeMCU */
  
  Serial.println("Turning on PWM");
  pwm.begin();
  pwm.setPWMFreq(1600);  // This is the maximum PWM frequency

  Serial.println("Setting CH0-2 to OFF.");
  pwm.setPWM(CH1_PIN, 0, 4096);
  pwm.setPWM(CH2_PIN, 0, 4096);
  pwm.setPWM(CH3_PIN, 0, 4096);

}

// Main()
void loop()
{
  webSocket.loop();
  server.handleClient();
}


