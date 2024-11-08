#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Keypad.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1

// Set up display to use I2CMaster (Bus 1) for OLED
TwoWire I2CMaster = TwoWire(1);  // Use I2C Bus 1 for OLED (master mode)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &I2CMaster, OLED_RESET);

const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {',', '0', '#', 'D'}
};

byte rowPins[ROWS] = {18, 5, 17, 16};
byte colPins[COLS] = {4, 0, 2, 15};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

String volume = "";
float batas_volume = 0.0;
String Strbatas_vol = "";

// Timer interval untuk mengatur pengiriman serial
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 500;  // Interval waktu dalam milidetik

void setup() {
  Serial.begin(115200);

  // Initialize I2C Bus 1 (Master) for OLED display
  I2CMaster.begin(27, 14);  // SDA on 27, SCL on 14 for OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED initialization failed");
    while (1); // Halt if OLED fails to initialize
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("Initializing...");
  display.display();
  delay(1000);  // Delay to check if initialization message appears

  updateDisplay();

  // Initialize Serial1 for communication with receiver ESP32
  Serial1.begin(9600, SERIAL_8N1, 26, 25);
}

void loop() {
  char key = keypad.getKey();

  if (key) {
    if (isDigit(key)) { 
      volume += key;  
      updateDisplay();
    } else if (key == ',') {
      if (volume.indexOf(',') == -1) {
        volume += '.';
        updateDisplay();
      }
    } else if (key == 'A') {
      if (!volume.isEmpty()) {
        batas_volume = volume.toFloat();
        
        if (isnan(batas_volume) || batas_volume == 0.0) {
          batas_volume = 0.0;
          Serial.println("Batas Volume ditetapkan: Tidak ada");
        } else {
          Serial.print("Batas Volume ditetapkan: ");
          Serial.println(batas_volume);
        }
      } else {
        batas_volume = 0.0;
        Serial.println("Batas Volume ditetapkan: Tidak ada");
      }
      showConfirmation();
      volume = "";
      updateDisplay();
    } else {
      displayWarning();
      delay(100);
      updateDisplay();
    }
  }

  // Mengirim data hanya jika interval waktu tercapai
  if (millis() - lastSendTime >= sendInterval) {
    lastSendTime = millis();  // Reset timer

    Strbatas_vol = String(batas_volume); // Konversi float ke string
    Serial1.println(Strbatas_vol);       // Kirim data ke Serial1
    Serial.print("Mengirim data: ");
    Serial.println(Strbatas_vol);        // Tampilkan di Serial Monitor
  }
}

void updateDisplay() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print("Batas Volume : ");
  display.print(volume);
  display.print(" Liter");
  display.display();
}

void displayWarning() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print("Karakter tidak dikenal");
  display.display();
}

void showConfirmation() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print("Input telah diterima");
  display.display();
  delay(500);

  display.clearDisplay();
  display.setCursor(0, 0);
  display.print("Batas Volume : ");
  display.print(batas_volume);
  display.print(" Liter");
  display.display();
}
