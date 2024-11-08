#include <LoRa.h>
#include <WiFiMulti.h>
#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <HTTPClient.h>
#include <UrlEncode.h>

#define LORA_SS 16
#define LORA_RST 17
#define LORA_DIO0 4
#define TFT_CS 27

#define WIFI_SSID "Purba Family 2"
#define WIFI_PASSWORD "fgd45RR##"
#define INFLUXDB_URL "https://us-east-1-1.aws.cloud2.influxdata.com"
#define INFLUXDB_TOKEN "2jCdvXDUaZ7TD5pQkNtB-NLGlQDy6ZjozjOnEAdArXxOvl1zsgS-cHKbm6YrzAJJLbgbs8aSvr0zCCT2LTswiA=="
#define INFLUXDB_ORG "c53165878691937e"
#define INFLUXDB_BUCKET "water-monitoring"

float batas_volume = 9999;
int pin14Status = 0;
int pin12Status = 0;
int statusPump = 1;
bool isReceiver = true;

// untuk pembacaan serial menggunakan interrupt
volatile bool newSerialData = false;
String serialBuffer = "";

WiFiMulti wifiMulti;
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);
Point sensor("pemantauan_air");

String phoneNumber = "+6281326479934";
String apiKey = "3479585";

TFT_eSPI tft = TFT_eSPI();
String receivedData = "";
bool dataAvailable = false;

float pH = 0.0, tds = 0.0, turbidity = 0.0, kualitas_air = 0.0, volume_air = 0.0, suhu = 0.0;

// Callback untuk pembacaan serial
void IRAM_ATTR serialEvent() {
  while (Serial1.available()) {
    char inChar = (char)Serial1.read();
    serialBuffer += inChar;
    if (inChar == '\n') {
      newSerialData = true;  // Set flag ketika data lengkap diterima
    }
  }
}

void sendMessage(String message) {
  String url = "https://api.callmebot.com/whatsapp.php?phone=" + phoneNumber + "&apikey=" + apiKey + "&text=" + customUrlEncode(message); 
  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  
  int httpResponseCode = http.POST(url);
  if (httpResponseCode == 200) {
    Serial.println("Message sent successfully");
  } else {
    Serial.print("Error sending the message, HTTP response code: ");
    Serial.println(httpResponseCode);
  }
  http.end();
}

void setup() {
  Serial.begin(115200);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  pinMode(TFT_CS, OUTPUT);
  pinMode(14, INPUT_PULLDOWN);
  pinMode(12, INPUT_PULLDOWN);
  pinMode(2, OUTPUT);
  digitalWrite(2, HIGH);

  digitalWrite(TFT_CS, HIGH);
  if (!LoRa.begin(433E6)) {
    Serial.println("Starting LoRa failed!");
    while (1);
  }
  LoRa.onReceive(onReceive);
  LoRa.receive();

  WiFi.mode(WIFI_STA);
  wifiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (wifiMulti.run() != WL_CONNECTED) {
    Serial.print(".");
    delay(100);
  }
  Serial.println();

  if (client.validateConnection()) {
    Serial.print("Connected to InfluxDB: ");
    Serial.println(client.getServerUrl());
  } else {
    Serial.print("InfluxDB connection failed: ");
    Serial.println(client.getLastErrorMessage());
  }

  tft.init();
  tft.setRotation(1);

  // Start UART for communication and attach interrupt
  Serial1.begin(9600, SERIAL_8N1, 26, 25);
  Serial1.onReceive(serialEvent);  // Attach callback for serial data
}

void loop() {
  // Proses data baru dari Serial jika tersedia
  if (newSerialData) {
    Serial.print("Data received: ");
    Serial.println(serialBuffer);

    float tempVolume = serialBuffer.toFloat();
    if (!isnan(tempVolume)) {
      batas_volume = tempVolume;
      Serial.print("Updated Volume Limit: ");
      Serial.println(batas_volume);
    } else {
      Serial.println("Invalid data received, ignoring...");
    }

    // Reset flag dan buffer
    newSerialData = false;
    serialBuffer = "";
  }

  if (dataAvailable) {
    parseData();
    transmitStatus();
    displayData();
    uploadToInfluxDB();
    sendWhatsAppNotification();

    dataAvailable = false;  
    isReceiver = true;
    LoRa.receive();
  }
}

void onReceive(int packetSize) {
  if (packetSize == 0) return;

  digitalWrite(TFT_CS, HIGH);
  digitalWrite(LORA_SS, LOW);
  receivedData = "";
  for (int i = 0; i < packetSize; i++) {
    receivedData += (char)LoRa.read();
  }
  dataAvailable = true;
  digitalWrite(LORA_SS, HIGH);
  digitalWrite(TFT_CS, LOW);
}

void parseData() {
  int commaIndex1 = receivedData.indexOf(',');
  int commaIndex2 = receivedData.indexOf(',', commaIndex1 + 1);
  int commaIndex3 = receivedData.indexOf(',', commaIndex2 + 1);
  int commaIndex4 = receivedData.indexOf(',', commaIndex3 + 1);
  int commaIndex5 = receivedData.indexOf(',', commaIndex4 + 1);

  if (commaIndex1 != -1 && commaIndex2 != -1 && commaIndex3 != -1 && commaIndex4 != -1 && commaIndex5 != -1) {
    pH = receivedData.substring(0, commaIndex1).toFloat();
    tds = receivedData.substring(commaIndex1 + 1, commaIndex2).toFloat();
    turbidity = receivedData.substring(commaIndex2 + 1, commaIndex3).toFloat();
    kualitas_air = receivedData.substring(commaIndex3 + 1, commaIndex4).toFloat();
    volume_air = receivedData.substring(commaIndex4 + 1, commaIndex5).toFloat();
    suhu = receivedData.substring(commaIndex5 + 1).toFloat();
  } else {
    Serial.println("Invalid data format received");
  }
}

void displayData() {
  digitalWrite(LORA_SS, HIGH);
  digitalWrite(TFT_CS, LOW);
  
  tft.fillScreen(TFT_BLACK);
  drawInfoCard("pH", String(pH).c_str(), 20, 20, TFT_CYAN);
  drawInfoCard("TDS", String(tds).c_str(), 20, 90, TFT_CYAN);
  drawInfoCard("Turbidity", String(turbidity).c_str(), 20, 160, TFT_CYAN);
  drawInfoCard("Quality Index", String(kualitas_air).c_str(), 20, 230, TFT_CYAN);
  drawInfoCard("Water Volume", String(volume_air).c_str(), 240, 20, TFT_CYAN);
  drawInfoCard("Temperature", String(suhu).c_str(), 240, 90, TFT_CYAN);
  
  digitalWrite(TFT_CS, HIGH);
}

void uploadToInfluxDB() {
  sensor.clearFields();
  sensor.addField("pH", pH);
  sensor.addField("kekeruhan", turbidity);
  sensor.addField("tds", tds);
  sensor.addField("suhu", suhu);
  sensor.addField("kualitas_air", kualitas_air);
  sensor.addField("volume_air", volume_air);

  if (wifiMulti.run() == WL_CONNECTED) {
    if (!client.writePoint(sensor)) {
      Serial.print("InfluxDB write failed: ");
      Serial.println(client.getLastErrorMessage());
    } else {
      Serial.println("Data successfully written to InfluxDB");
    }
  } else {
    Serial.println("WiFi connection lost");
  }
}

void sendWhatsAppNotification() {
  if (kualitas_air <= -2.0) {  
    String message = "Peringatan: Kualitas Air Tidak Layak Pakai!\n";
    message += "Indeks Kualitas Air: " + String(kualitas_air) + "\n";
    message += "Data Pemantauan Air Lainnya:\n";
    message += "pH: " + String(pH) + "\n";
    message += "TDS: " + String(tds) + "\n";
    message += "Turbidity: " + String(turbidity) + "\n";
    message += "Water Volume: " + String(volume_air) + "\n";
    message += "Temperature: " + String(suhu);
    
    sendMessage(message);
  }
}

void transmitStatus() {
  if (isReceiver) {
    isReceiver = false;
    LoRa.idle();

    pin14Status = digitalRead(14);
    pin12Status = digitalRead(12);
    String statusMessage = String(pin14Status) + "," + String(pin12Status) + "," + String(batas_volume);
    
    digitalWrite(TFT_CS, HIGH);
    digitalWrite(LORA_SS, LOW);
    
    LoRa.beginPacket();
    LoRa.print(statusMessage);
    LoRa.endPacket();
    
    digitalWrite(LORA_SS, HIGH);
    digitalWrite(TFT_CS, LOW);
  }
  
  if (pin12Status == HIGH) {
    Serial.println("OVERRIDE ON : PUMP OFF");
    statusPump = 0; // LOW jika pin12Status HIGH
  } else if (pin12Status == LOW) {
    Serial.println("OVERRIDE OFF");
    if (pin14Status == HIGH) {
      Serial.println("NON-AUTO, PUMP ON");
      statusPump = 1; // HIGH jika pin14Status HIGH
    } else if (pin14Status == LOW) {
      if (volume_air >= batas_volume) {
        Serial.println("Batas volume telah terpenuhi");
        statusPump = 0; // LOW
      } else if (kualitas_air <= -2.0) {
        Serial.println("Indeks Air terlalu buruk");
        statusPump = 0; // LOW
      } else {
        Serial.println("Batas belum terpenuhi");
        statusPump = 1; // HIGH
      }
    }
  }
}

void drawInfoCard(const char* label, const char* value, int x, int y, uint16_t color) {
  tft.fillRoundRect(x, y, 200, 50, 10, TFT_DARKGREY);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
  tft.setTextSize(2);
  tft.setCursor(x + 10, y + 10);
  tft.print(label);
  tft.setCursor(x + 10, y + 30);
  tft.setTextColor(color);
  tft.print(value);
}
