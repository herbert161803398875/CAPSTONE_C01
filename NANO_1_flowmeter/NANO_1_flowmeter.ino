#include <Wire.h>
#include <EEPROM.h>
#include "GravityTDS.h"
#include <OneWire.h>
#include <DallasTemperature.h>

// Pin definitions for each sensor
#define TdsSensorPin A1    // TDS sensor on A1
#define PhSensorPin A2     // pH sensor on A2
#define TurbidityPin A3    // Turbidity sensor on A3
#define FlowSensorPin 2    // Flow sensor on D2

// Pin for DS18B20 temperature sensor
#define ONE_WIRE_BUS 5     // DS18B20 on D5

// Constants for the pH meter
#define Offset 0.10
#define ArrayLength 40
int pHArray[ArrayLength];
int pHArrayIndex = 0;

// Gravity TDS object
GravityTDS gravityTds;

// Variables for sensor readings
float temperature = 31; // Default temperature (will be updated by DS18B20)
float tdsValue = 0;
float pHValue = 0;
double turbidityValue = 0;
double voltage = 0;
float flowRate = 0.0;   // Flow rate in L/min
float totalVolume = 0.0; // Total volume in liters

volatile int flow_frequency;  // Measures flow sensor pulses
unsigned long currentTime;
unsigned long cloopTime;
float volume_per_pulse = 0.004;  // Example value, adjust based on your sensor

bool esp32Connected = false;

// Setup for DS18B20
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// Function to calculate average pH
double averageArray(int* arr, int number) {
  long amount = 0;
  for (int i = 0; i < number; i++) {
    amount += arr[i];
  }
  return (double)amount / number;
}

// Interrupt function to count pulses from flow sensor
void flow() {
  flow_frequency++;
}

void setup() {
  Serial.begin(115200);

  // Initialize TDS sensor
  gravityTds.setPin(TdsSensorPin);
  gravityTds.setAref(5.0);      // Reference voltage for ADC
  gravityTds.setAdcRange(1024);  // 10-bit ADC range
  gravityTds.begin();

  // Initialize DS18B20 sensor
  sensors.begin();

  // Initialize I2C communication
  Wire.begin(9); // Arduino Nano I2C address is 9
  Wire.onRequest(requestEvent);  // Function called when data is requested by master

  // Initialize flow sensor
  pinMode(FlowSensorPin, INPUT);
  digitalWrite(FlowSensorPin, HIGH); // Optional Internal Pull-Up
  attachInterrupt(digitalPinToInterrupt(FlowSensorPin), flow, RISING);  // Attach interrupt for flow sensor

  // Initialize Serial output
  Serial.println("Arduino Nano is ready to send sensor data via I2C.");
  
  currentTime = millis();
  cloopTime = currentTime;
}

void loop() {
  currentTime = millis();

  // Read pH sensor
  static unsigned long samplingTime = millis();
  if (millis() - samplingTime > 20) {
    pHArray[pHArrayIndex++] = analogRead(PhSensorPin);
    if (pHArrayIndex == ArrayLength) pHArrayIndex = 0;
    voltage = averageArray(pHArray, ArrayLength) * 5.0 / 1024;
    pHValue = 3.5 * voltage + Offset;
    samplingTime = millis();
  }

  // Read TDS sensor
  gravityTds.setTemperature(temperature);  // Set temperature for compensation
  gravityTds.update();                     // Update TDS value
  tdsValue = gravityTds.getTdsValue();      // Get TDS value

  // Read turbidity sensor
  int turbidityADC = analogRead(TurbidityPin);
  double turbVoltage = turbidityADC * (5.0 / 1024.0);
  if (turbVoltage >= 3.02) {
    turbidityValue = 489.5815 * turbVoltage * turbVoltage * turbVoltage - 4882.6517 * turbVoltage * turbVoltage + 16196.162* turbVoltage - 17863.967;
  } else if (turbVoltage >= 2.7) {
    turbidityValue = -1120.4 * turbVoltage * turbVoltage + 5742.3 * turbVoltage - 4352.9;
  } else {
    turbidityValue = 3000; // Default high value if below 2.5V
  }

  // Read DS18B20 temperature sensor
  sensors.requestTemperatures();
  temperature = sensors.getTempCByIndex(0);  // Get temperature in Celsius

  // Every second, calculate flow rate in L/min and update total volume
  if (currentTime >= (cloopTime + 1000)) {
    cloopTime = currentTime; // Update loop time

    if (flow_frequency != 0) {
      // Calculate flow rate in liters per minute (L/min)
      flowRate = (flow_frequency * volume_per_pulse * 60);
      
      // Update total volume in liters
      totalVolume += flowRate / 60;

      flow_frequency = 0; // Reset pulse counter after calculation
    } else {
      flowRate = 0;  // No flow detected
    }

    // Print flow rate and total volume to serial monitor
    Serial.print("Flow Rate: ");
    Serial.print(flowRate);
    Serial.println(" L/min");
    Serial.print("Total Volume: ");
    Serial.print(totalVolume);
    Serial.println(" L");
  }

  // Print sensor data to serial monitor
  Serial.println("Sensor Readings:");
  Serial.print("pH: ");
  Serial.println(pHValue, 2);
  Serial.print("TDS: ");
  Serial.print(tdsValue, 0);
  Serial.println(" ppm");
  Serial.print("Turbidity: ");
  Serial.print(turbidityValue, 2);
  Serial.println(" NTU");
  Serial.print("Temperature (DS18B20): ");
  Serial.print(temperature);
  Serial.println(" °C");

  // Check for connection to ESP32 and notify
  if (esp32Connected) {
    Serial.println("Connected to ESP32.");
  } else {
    Serial.println("Waiting for ESP32 connection...");
  }

  delay(1000);  // Adjust delay if needed
}

// Function to send sensor data as bytes when requested by ESP32
void requestEvent() {
  // Create a buffer to hold the sensor data
  union {
    float value;
    byte bytes[4];
  } pHData, tdsData, turbidityData, volumeData, tempData;

  // Assign sensor values to union variables
  pHData.value = pHValue;
  tdsData.value = tdsValue;
  turbidityData.value = turbidityValue;
  volumeData.value = totalVolume;  // Send total volume as part of data
  tempData.value = temperature;    // Send temperature from DS18B20 as part of data

  // Send pH value as 4 bytes
  Wire.write(pHData.bytes, 4);
  // Send TDS value as 4 bytes
  Wire.write(tdsData.bytes, 4);
  // Send Turbidity value as 4 bytes
  Wire.write(turbidityData.bytes, 4);
  // Send total volume as 4 bytes
  Wire.write(volumeData.bytes, 4);
  // Send temperature as 4 bytes
  Wire.write(tempData.bytes, 4);

  // Notify that ESP32 is connected and data is sent
  esp32Connected = true;
}
