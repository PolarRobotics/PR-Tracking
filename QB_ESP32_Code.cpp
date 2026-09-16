/* QB_ESP32_Code - Alexander Casada , Nathaniel Johnson - 8 March 2026

This program is for the quarterback's second turret ESP32. This second turret ESP32
is solely for the Polar Robotics Pass Completion capstone project. This ESP32 will
get the quarterback's location through a wired UART connection to the DWM1001 tag on
the quarterback. The quarterback also communicates with each of the receivers on the
field using ESP-NOW, which utilizes Wi-Fi to send each of the receivers ID and location
to the quarterback. The secondary quarterback ESP32 finally formats the quarterback and
each receivers ID, as well as the location and send this to the main quarterback turret
ESP32, using a wired UART connection.

References:
- DWM1001 FIRWARE API GUIDE: https://forum.qorvo.com/uploads/default/original/1X/dcac22d1c0feaf8238f68d11515ad55dbef1b963.pdf
- GPT-5 was used for understanding and troubleshooting the code.
*/

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>

// ====================== RECEIVER MAC ADDRESSES ======================
uint8_t receiver1Mac[6] = {0x48, 0xE7, 0x29, 0x9F, 0xE6, 0x8C};
uint8_t receiver2Mac[6] = {0x20, 0xE7, 0xC8, 0x7E, 0x12, 0xB0};

// UART to DWM1001 using RX = 16 and TX = 17 on the esp32
HardwareSerial DWM(2);
String lineBuffer = "";

// Data Structures
typedef struct __attribute__((packed)) {
  uint64_t id;
  float x;
  float y;
  float z;
  float q;
} PositionData;

typedef struct __attribute__((packed)) {
  uint64_t ackFrom;
  uint64_t ackTo;
} AckData;

PositionData qbPos;
PositionData rxPos1;   // Receiver pi
PositionData rxPos2;   // Receiver 32.2
AckData ackData;

/**
OnDataRecv - Routes location data based on senders MAC address
*/
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) 
{
  if (len == sizeof(PositionData)) 
  {
    PositionData temp;
    memcpy(&temp, incomingData, sizeof(PositionData));

    if (memcmp(mac, receiver1Mac, 6) == 0) 
    {
      rxPos1 = temp;
      ackData.ackTo = rxPos1.id;
    } 
    else if (memcmp(mac, receiver2Mac, 6) == 0) 
    {
      rxPos2 = temp;
      ackData.ackTo = rxPos2.id;
    }

    ackData.ackFrom = qbPos.id;
    esp_now_send(mac, (uint8_t*)&ackData, sizeof(ackData));
  }
}

void parseLine(String line) 
{
  int posIndex = line.indexOf("POS,");

  if (posIndex >= 0) 
  {
    int first = line.indexOf(',', posIndex);
    int second = line.indexOf(',', first + 1);
    int third  = line.indexOf(',', second + 1);
    int fourth = line.indexOf(',', third + 1);

    if (first > 0 && second > first && third > second && fourth > third) 
    {
      qbPos.id = 0xDECAB0AA80F0005B;
      qbPos.x = line.substring(first + 1, second).toFloat();
      qbPos.y = line.substring(second + 1, third).toFloat();
      qbPos.z = line.substring(third + 1, fourth).toFloat();
      qbPos.q = line.substring(fourth + 1).toFloat();
    }
  }
}

void setup() 
{
  Serial.begin(115200);
  DWM.begin(115200, SERIAL_8N1, 16, 17);

  // ESP-NOW WIFI SETUP
  WiFi.mode(WIFI_STA);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) 
  {
    Serial.println("ESP-NOW init has failed!");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);

  esp_now_peer_info_t peerInfo = {};
  peerInfo.channel = 1;
  peerInfo.encrypt = false;

  memcpy(peerInfo.peer_addr, receiver1Mac, 6);
  esp_now_add_peer(&peerInfo);

  memcpy(peerInfo.peer_addr, receiver2Mac, 6);
  esp_now_add_peer(&peerInfo);

  // Start DWM1001 continuous output
  delay(1000);
  DWM.print("\r\r");
  delay(5000);
  DWM.print("\r\r");
  DWM.print("lec\r\n");

  Serial.println("QB Secondary ESP32 ready - listening for Receiver 1 + Receiver 2");
}

unsigned long lastPrint = 0;

void loop() 
{
  while (DWM.available()) 
  {
    char c = DWM.read();
    if (c == '\n') 
    {
      parseLine(lineBuffer);
      lineBuffer = "";
    } else if (c != '\r') 
    {
      lineBuffer += c;
    }
  }

  if (millis() - lastPrint > 100) 
  {
    lastPrint = millis();

    // Prints out in the format that the main QB esp-32 can process
    Serial.printf(
      "<QB, %.2f, %.2f, %.2f, %.2f> "
      "<RCV, %.2f, %.2f, %.2f, %.2f> "
      "<RCV, %.2f, %.2f, %.2f, %.2f>\n",
      qbPos.x, qbPos.y, qbPos.z, qbPos.q,
      rxPos1.x, rxPos1.y, rxPos1.z, rxPos1.q,
      rxPos2.x, rxPos2.y, rxPos2.z, rxPos2.q
    );
  }
}