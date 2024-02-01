/*
  Pool circuit in Leadwood 399
  Stefan Sunaert - 10 Jan 2024

  - In HA -> settings - people -> users
    make a user/pass for Mosquitto broker 
    used=arduino and passwd=youknowitsillystar // off course these are dummy users/passwd not mine 8-)
 
 */

// Load the neccessary libraries 
#include "ArduinoGraphics.h" // library 1 for the led matrix
#include "Arduino_LED_Matrix.h" // library 2 for the led matrix
#include <WiFiS3.h> // library for wifi on the R4-wifi
#include <ArduinoHA.h> // libary for Home Assistant
#include <OneWire.h> // library 1 for the DS18B20 temperature sensor
#include <DallasTemperature.h> // library 2 for the DS18B20 temperature sensor


// Load all the secrets
#include "arduino_secrets.h"
char ssid[] = SECRET_WIFI_SSID;        // your network SSID (name)
char wifi_pass[] = SECRET_WIFI_PASS;    // your network password (use for WPA, or use as key for WEP)
// Now for the Mosquito Broker 
#define BROKER_ADDR         IPAddress(192,168,0,20)
#define BROKER_USERNAME     SECRET_BROKER_USER
#define BROKER_PASSWORD     SECRET_BROKER_PASS

// define variables
const long INTERVAL_WIFI = 240000;  // interval at which to poll the Wifi (milliseconds)
const long INTERVAL_LEVEL = 4000;  // interval at which to poll the level of the pool (milliseconds)
const long INTERVAL_TEMP = 2000;  // interval at which to poll the temperature of the pool (milliseconds)
const int PIN_SENSOR_LEVEL = 13; // Arduino pin connected to pool level sensor
const int PIN_SENSOR_TEMP_POOL = 7; // Arduino pin connected to DS18B20 sensor's DQ pin
const int PIN_SENSOR_TEMP_AMBIENT = 8; // Arduino pin connected to DS18B20 sensor's DQ pin
#define RELAY_1  12  // the Arduino pin, which connects to the relay 1
int status = WL_IDLE_STATUS;     // the WiFi radio's status
bool poolLevel = true;  // level of pool
float poolTempCelsius;    // temperature of the pool
float ambientTempCelsius;    // temperature of the pool
String matrixText = "";
String poolLevelText = "OK";

// Generally, you should use "unsigned long" for variables that hold time
// The value will quickly become too large for an int to store
unsigned long previousMillisWifi = 0;  // will store last time Temp was updated
unsigned long previousMillisTemp = 0;  // will store last time Temp was updated
unsigned long previousMillisLevel = 0;  // will store last time Level was updated

// Objects
// Setting up the wifi client
WiFiClient client;

// Setting up the led matrix
ArduinoLEDMatrix matrix;

// Setting up DS18B20 temp sensors
OneWire oneWirePool(PIN_SENSOR_TEMP_POOL);         // setup a oneWire instance
DallasTemperature tempPoolSensor(&oneWirePool); // pass oneWire to DallasTemperature library
OneWire oneWireAmbient(PIN_SENSOR_TEMP_AMBIENT);         // setup a oneWire instance
DallasTemperature tempAmbientSensor(&oneWireAmbient); // pass oneWire to DallasTemperature library

// Setting up my arduino device for HA
HADevice device("treanusArduino");
HAMqtt mqtt(client, device);

// Setting up sensors
// "myInput" is unique ID of the sensor. You should define you own ID.
HABinarySensor sensor("myLDW399PoolLevel");
HASensorNumber analogSensorPool("myAnalogInputPool", HASensorNumber::PrecisionP1);
HASensorNumber analogSensorAmbient("myAnalogInputAmbient", HASensorNumber::PrecisionP1);
HASensorNumber analogSensorWifiSignal("myAnalogInputWifiSignal", HASensorNumber::PrecisionP0);

void setup() {
  //Initialize serial and wait for port to open:
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  // Initialise the LED Matrix
  matrix.begin();
  matrix.beginDraw();
  matrix.stroke(0xFFFFFFFF);
  const char text[] = "399";   // add some static text - will only show "UNO" (not enough space on the display)
  matrix.textFont(Font_4x6);
  matrix.beginText(0, 1, 0xFFFFFF);
  matrix.println(text);
  matrix.endText();
  matrix.endDraw();
  delay(2000);

  // setup the sensors and outputs
  pinMode(PIN_SENSOR_LEVEL, INPUT_PULLUP); // set arduino pin to input pull-up mode
  pinMode(RELAY_1, OUTPUT); // initialize the pin for the relay

  // temperature
  tempPoolSensor.begin();    // initialize the sensor to measure pool water temperature
  tempAmbientSensor.begin();    // initialize the sensor to measure ambient temperature

  // check for the WiFi module:
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    // don't continue
    while (true);
  }

  String fv = WiFi.firmwareVersion();
  if (fv < WIFI_FIRMWARE_LATEST_VERSION) {
    Serial.println("Please upgrade the firmware");
  }

  // attempt to connect to WiFi network:
  while (status != WL_CONNECTED) {
    Serial.print("Attempting to connect to WPA SSID: ");
    Serial.println(ssid);
    // Connect to WPA/WPA2 network:
    status = WiFi.begin(ssid, wifi_pass);

    // wait 10 seconds for connection:
    delay(10000);
  }

  // you're connected now, so print out the data:
  Serial.print("You're connected to the network");
  printWifiData();

  // optional device's details
  device.enableSharedAvailability();
  device.enableLastWill();
  device.setName("Arduino");
  device.setSoftwareVersion("1.0.3");

  // optional properties
  sensor.setCurrentState(poolLevel);
  sensor.setName("Pool Level");
  sensor.setDeviceClass("problem");
  sensor.setIcon("mdi:pool");

  // configure sensor (optional)
  analogSensorPool.setIcon("mdi:pool-thermometer");
  analogSensorPool.setName("Pool temperature");
  analogSensorPool.setDeviceClass("temperature");
  analogSensorPool.setUnitOfMeasurement("°C");

  analogSensorAmbient.setIcon("mdi:thermometer");
  analogSensorAmbient.setName("Ambient temperature");
  analogSensorAmbient.setDeviceClass("temperature");
  analogSensorAmbient.setUnitOfMeasurement("°C");

  analogSensorWifiSignal.setIcon("mdi:wifi");
  analogSensorWifiSignal.setName("Pool Wifi Signal");
  analogSensorWifiSignal.setDeviceClass("signal_strength");
  analogSensorWifiSignal.setUnitOfMeasurement("dB");


  mqtt.begin(BROKER_ADDR,BROKER_USERNAME,BROKER_PASSWORD);
  //mqtt.begin(BROKER_ADDR);
}

void loop() {
  // check the network connection once every 10 seconds:
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillisLevel >= INTERVAL_LEVEL) {
    previousMillisLevel = currentMillis;  
    
    poolLevel = digitalRead(PIN_SENSOR_LEVEL); // read state of the pool level sensor

    if (poolLevel == HIGH) {
      Serial.println("The pool level is sufficient");
      digitalWrite(RELAY_1, HIGH); // turn on relay
      poolLevelText = "OK";
    } else {
      Serial.println("The pool level is too low");
      digitalWrite(RELAY_1, LOW);  // turn off relay
      poolLevelText = "LOW";
    }
    
    sensor.setState(!poolLevel); // set the state of HA sensor
  }

  if (currentMillis - previousMillisTemp >= INTERVAL_TEMP) {
    previousMillisTemp = currentMillis;

    tempPoolSensor.requestTemperatures();             // send the command to get temperatures
    poolTempCelsius = tempPoolSensor.getTempCByIndex(0);  // read temperature in Celsius
    Serial.print("Temperature of the pool: ");
    Serial.print(poolTempCelsius);    // print the temperature in Celsius
    Serial.println("°C");
    analogSensorPool.setValue(poolTempCelsius);

    tempAmbientSensor.requestTemperatures();             // send the command to get temperatures
    ambientTempCelsius = tempAmbientSensor.getTempCByIndex(0);  // read temperature in Celsius
    Serial.print("Temperature of the ambient air: ");
    Serial.print(ambientTempCelsius);    // print the temperature in Celsius
    Serial.println("°C");
    analogSensorAmbient.setValue(ambientTempCelsius);
    Serial.print("Wifi signal: ");
    Serial.println(WiFi.RSSI());
  
    printWifiData();
    analogSensorWifiSignal.setValue(WiFi.RSSI());

  } 

  mqtt.loop();

  // Show info on the led matrix
  matrix.beginDraw();
  matrix.stroke(0xFFFFFFFF);
  matrix.textScrollSpeed(50);
  // add the text
  String matrixText = "P: " + String(poolTempCelsius) + "   A: " + String(ambientTempCelsius) + "   L:" + poolLevelText + "  ";
  matrix.textFont(Font_5x7);
  matrix.beginText(0, 1, 0xFFFFFF);
  matrix.println(matrixText);
  matrix.endText(SCROLL_LEFT);
  matrix.endDraw();

}

void printWifiData() {
  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());
  
  // print your board's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.println(rssi);

}
