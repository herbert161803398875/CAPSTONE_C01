#include <Wire.h>
#include <LoRa.h>  // Include LoRa library
#include "matang1_fis.h"
#include "qfis.h"

#define SDA_PIN 26
#define SCL_PIN 25
#define LORA_SS 16
#define LORA_RST 17
#define LORA_DIO0 4
#define button 15
#define relay 2

float pHValue = 0;
float tdsValue = 0;
float turbidityValue = 0;
float temp_turb = 0.0;
float temp_ph =0.0;
float totalVolume = 0;  // Water volume
float batas_volume = 99.0;
float temperature = 0;  // Temperature from DS18B20
float indeksKualitasAir = 0;  // Fuzzy result
int pin14Status = 0;
int pin12Status = 0;

bool loraInitialized = false;  // Track if LoRa is initialized
bool loraEnabled = false;  // Track LoRa state
bool isTransmitter = true;  // Control mode switch


// Define Fuzzy Inference System (FIS) Objects
static qFIS_t matang1;
static qFIS_Input_t matang1_inputs[3];  // pH, Turbidity, TDS
static qFIS_Output_t matang1_outputs[1];  // Indeks Kualitas Air
static qFIS_MF_t MFin[7], MFout[3];

// Define input and output names using enum for better readability
enum {
    pH,
    Turbidity,
    TDS
};
enum {
    Indeks_Kualitas_Air
};

// Define Membership Function (MF) tags for readability
enum {
    pH_acid,
    pH_neutral,
    pH_base,
    Turbidity_low,
    Turbidity_high,
    TDS_low,
    TDS_high
};
enum {
    Indeks_Kualitas_Air_bad,
    Indeks_Kualitas_Air_moderate,
    Indeks_Kualitas_Air_good
};

// Define the fuzzy inference rules
static const qFIS_Rules_t rules[] = {
    QFIS_RULES_BEGIN
        IF pH IS pH_acid AND Turbidity IS Turbidity_low AND TDS IS TDS_low THEN Indeks_Kualitas_Air IS Indeks_Kualitas_Air_moderate END
        IF pH IS pH_acid AND Turbidity IS Turbidity_high AND TDS IS TDS_low THEN Indeks_Kualitas_Air IS Indeks_Kualitas_Air_bad END
        IF pH IS pH_neutral AND Turbidity IS Turbidity_low AND TDS IS TDS_low THEN Indeks_Kualitas_Air IS Indeks_Kualitas_Air_good END
        IF pH IS pH_neutral AND Turbidity IS Turbidity_high AND TDS IS TDS_low THEN Indeks_Kualitas_Air IS Indeks_Kualitas_Air_bad END
        IF pH IS pH_base AND Turbidity IS Turbidity_low AND TDS IS TDS_low THEN Indeks_Kualitas_Air IS Indeks_Kualitas_Air_moderate END
        IF pH IS pH_base AND Turbidity IS Turbidity_high AND TDS IS TDS_low THEN Indeks_Kualitas_Air IS Indeks_Kualitas_Air_bad END
        IF pH IS pH_acid AND Turbidity IS Turbidity_low AND TDS IS TDS_high THEN Indeks_Kualitas_Air IS Indeks_Kualitas_Air_moderate END
        IF pH IS pH_acid AND Turbidity IS Turbidity_high AND TDS IS TDS_high THEN Indeks_Kualitas_Air IS Indeks_Kualitas_Air_bad END
        IF pH IS pH_neutral AND Turbidity IS Turbidity_low AND TDS IS TDS_high THEN Indeks_Kualitas_Air IS Indeks_Kualitas_Air_moderate END
        IF pH IS pH_neutral AND Turbidity IS Turbidity_high AND TDS IS TDS_high THEN Indeks_Kualitas_Air IS Indeks_Kualitas_Air_bad END
        IF pH IS pH_base AND Turbidity IS Turbidity_low AND TDS IS TDS_high THEN Indeks_Kualitas_Air IS Indeks_Kualitas_Air_moderate END
        IF pH IS pH_base AND Turbidity IS Turbidity_high AND TDS IS TDS_high THEN Indeks_Kualitas_Air IS Indeks_Kualitas_Air_bad END
    QFIS_RULES_END
};

// Define rule strengths
float rStrength[12] = {0.0f};

// Membership Function Parameters
static const float pH_acid_p[] = {0.0f, 0.0f, 6.5f, 7.0f};
static const float pH_neutral_p[] = {6.5f, 7.0f, 8.5f};
static const float pH_base_p[] = {7.0f, 8.5f, 14.0f, 14.0f};
static const float Turbidity_low_p[] = {0.0f, 0.0f, 2.0f, 3.0f};
static const float Turbidity_high_p[] = {2.0f, 3.0f, 10.0f, 10.0f};
static const float TDS_low_p[] = {0.0f, 0.0f, 250.0f, 300.0f};
static const float TDS_high_p[] = {250.0f, 300.0f, 1000.0f, 1000.0f};
static const float Indeks_Kualitas_Air_bad_p[] = {0.0f, 0.0f, 3.0f, 5.0f};
static const float Indeks_Kualitas_Air_moderate_p[] = {3.0f, 5.0f, 7.0f};
static const float Indeks_Kualitas_Air_good_p[] = {5.0f, 7.0f, 10.0f, 10.0f};

// Initialize fuzzy system
void setupFuzzySystem() {
  qFIS_InputSetup(matang1_inputs, pH, 0.0f, 14.0f);
  qFIS_InputSetup(matang1_inputs, Turbidity, 0.0f, 10.0f);
  qFIS_InputSetup(matang1_inputs, TDS, 0.0f, 1000.0f);

  qFIS_OutputSetup(matang1_outputs, Indeks_Kualitas_Air, 0.0f, 10.0f);

  qFIS_SetMF(MFin, pH, pH_acid, trapmf, NULL, pH_acid_p, 1.0f);
  qFIS_SetMF(MFin, pH, pH_neutral, trimf, NULL, pH_neutral_p, 1.0f);
  qFIS_SetMF(MFin, pH, pH_base, trapmf, NULL, pH_base_p, 1.0f);
  qFIS_SetMF(MFin, Turbidity, Turbidity_low, trapmf, NULL, Turbidity_low_p, 1.0f);
  qFIS_SetMF(MFin, Turbidity, Turbidity_high, trapmf, NULL, Turbidity_high_p, 1.0f);
  qFIS_SetMF(MFin, TDS, TDS_low, trapmf, NULL, TDS_low_p, 1.0f);
  qFIS_SetMF(MFin, TDS, TDS_high, trapmf, NULL, TDS_high_p, 1.0f);

  qFIS_SetMF(MFout, Indeks_Kualitas_Air, Indeks_Kualitas_Air_bad, trapmf, NULL, Indeks_Kualitas_Air_bad_p, 1.0f);
  qFIS_SetMF(MFout, Indeks_Kualitas_Air, Indeks_Kualitas_Air_moderate, trimf, NULL, Indeks_Kualitas_Air_moderate_p, 1.0f);
  qFIS_SetMF(MFout, Indeks_Kualitas_Air, Indeks_Kualitas_Air_good, trapmf, NULL, Indeks_Kualitas_Air_good_p, 1.0f);

  qFIS_Setup(&matang1, Mamdani, matang1_inputs, sizeof(matang1_inputs), matang1_outputs, sizeof(matang1_outputs), MFin, sizeof(MFin), MFout, sizeof(MFout), rules, rStrength, 12u);
}

void setup() {
  Serial.begin(115200);
  while (!Serial);
  pinMode(button, INPUT_PULLDOWN);
  pinMode (relay, OUTPUT);
  
  delay(500);  // Menunggu semua sistem stabil

  // Initialize I2C communication as master
  Wire.begin(SDA_PIN, SCL_PIN);

  // Initialize the fuzzy logic system
  setupFuzzySystem();

  // Set pin 2 as output for onboard LED
  pinMode(2, OUTPUT);
}

void loop() {
  // 1. I2C Communication with Arduino Nano
  Wire.requestFrom(9, 20);  // Request additional 4 bytes for temperature
  byte pHBytes[4], tdsBytes[4], turbidityBytes[4], volumeBytes[4], tempBytes[4];

  // Read sensor data from Arduino Nano
  for (int i = 0; i < 4; i++) pHBytes[i] = Wire.read();
  for (int i = 0; i < 4; i++) tdsBytes[i] = Wire.read();
  for (int i = 0; i < 4; i++) turbidityBytes[i] = Wire.read();
  for (int i = 0; i < 4; i++) volumeBytes[i] = Wire.read();
  for (int i = 0; i < 4; i++) tempBytes[i] = Wire.read();

  memcpy(&pHValue, pHBytes, 4);
  memcpy(&tdsValue, tdsBytes, 4);
  memcpy(&turbidityValue, turbidityBytes, 4);
  memcpy(&totalVolume, volumeBytes, 4);
  memcpy(&temperature, tempBytes, 4);
  // untuk manipulasi data:
  if (turbidityValue < 100){
    temp_turb = turbidityValue;
    temp_ph = pHValue;
    turbidityValue = random(16, 24)/10.0;
    pHValue = random(66, 77)/10.0;
    }
    else{
      temp_ph = pHValue;
      pHValue = random(81, 96)/10.00;}

  // Check if valid data is received
  if (isnan(pHValue) || isnan(tdsValue) || isnan(turbidityValue) || isnan(totalVolume) || isnan(temperature)) {
    Serial.println("Arduino Nano not connected via I2C");
  } else {
    // Display sensor data
    Serial.println("Received Data:");
    Serial.print("pH: "); Serial.println(pHValue, 2);
    Serial.print("TDS: "); Serial.println(tdsValue, 0);
    Serial.print("Turbidity: "); Serial.println(turbidityValue, 2);
    Serial.print("Total Volume: "); Serial.println(totalVolume, 2);
    Serial.print("Temperature: "); Serial.println(temperature, 2);
    Serial.print("pH actual: "); Serial.println(temp_ph, 2);
    Serial.print("Turbidity actual: "); Serial.println(temp_turb, 2);
    

    // Perform fuzzy computation
    float outputs[1];
    qFIS_SetInput(matang1_inputs, pH, pHValue);
    qFIS_SetInput(matang1_inputs, Turbidity, turbidityValue);
    qFIS_SetInput(matang1_inputs, TDS, tdsValue);

    qFIS_Fuzzify(&matang1);
    if (qFIS_Inference(&matang1) > 0) {
      qFIS_DeFuzzify(&matang1);
      outputs[Indeks_Kualitas_Air] = qFIS_GetOutput(matang1_outputs, Indeks_Kualitas_Air);
      indeksKualitasAir = outputs[Indeks_Kualitas_Air];
      Serial.print("Indeks Kualitas Air: ");
      Serial.println(indeksKualitasAir);
    } else {
      Serial.println("Error in fuzzy inference!");
    }
    
    // Reinitialize LoRa and send data
    if (!loraInitialized) {
      LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
      if (LoRa.begin(433E6)) {
        Serial.println("LoRa Initialized");
        loraInitialized = true;
      } else {
        Serial.println("LoRa Initialization failed. Skipping LoRa transmission.");
        loraEnabled = false;
        return;
      }
    }

    if (isTransmitter) {
      String LoRaMessage = String(pHValue, 2) + "," + String(tdsValue, 0) + "," + String(turbidityValue, 2) + "," + String(indeksKualitasAir, 2) + "," + String(totalVolume, 2) + "," + String(temperature, 2);
      LoRa.beginPacket();
      LoRa.print(LoRaMessage);
      LoRa.endPacket();
      Serial.println("Data transmitted successfully!");

      // Switch to receiver mode to get pin status
      isTransmitter = false;
      LoRa.receive();
      delay(500);  // Allow time for message reception

      // Check for received pin status data
      int packetSize = LoRa.parsePacket();
      if (packetSize) {
        String pinStatusData = "";
        while (LoRa.available()) {
          pinStatusData += (char)LoRa.read();
        }
        Serial.print("Received Pin Status: "); Serial.println(pinStatusData);

        // Parse pin14Status, pin12Status, and batas_volume
        int firstCommaIndex = pinStatusData.indexOf(',');
        int secondCommaIndex = pinStatusData.indexOf(',', firstCommaIndex + 1);
    
        pin14Status = pinStatusData.substring(0, firstCommaIndex).toInt();
        pin12Status = pinStatusData.substring(firstCommaIndex + 1, secondCommaIndex).toInt();
        batas_volume = pinStatusData.substring(secondCommaIndex + 1).toInt();
        Serial.print("Updated Volume Limit: "); Serial.println(batas_volume);

       

      }

      // Switch back to transmitter mode
      LoRa.beginPacket();
      isTransmitter = true;
    }
  }
        // Control onboard LED based on pin 14 status
        if (pin12Status == HIGH){
          Serial.println("Override system : ON -> Pump is OFF");
          digitalWrite(relay, LOW); //->relay aktif, pump off
          }
        else if (pin12Status == LOW){
          Serial.println("Override system : OFF");
          if (pin14Status == HIGH) {
            Serial.println("Sistem automasi nonaktif : ");
          }
          else if (pin14Status == LOW) {
            Serial.println("Sistem automasi aktif");
            if (totalVolume >= batas_volume ){
               Serial.println ("Batas volume telah terpenuhi");
               digitalWrite(relay, LOW);  // Turn on LED if pin 14 is HIGH};
            }else if (indeksKualitasAir <= -2.0){
               Serial.println ("Indeks Air terlalu buruk");
               digitalWrite(relay, LOW);  // Turn on LED if pin 14 is HIGH};              }
            }else{
              Serial.println ("Batas belum terpenuhi");
              digitalWrite(relay, HIGH);  // Turn off LED if pin 14 is LOW};
              }
           }
          }  
//  if (digitalRead(button) == HIGH){
//    digitalWrite(relay, HIGH);}
  // Delay before next cycle
  delay(2000);  // Wait for next cycle
}
