/* WR_ESP32_Code - Alexander Casada , Nathaniel Johnson - 28 October 2025

This program is for each of the receivers and will use the second ESP32 on the receiver to get the receiver's location
through a wired UART connection to the DWM1001 tag on the receiver. The receiver will then send its location to the quarterback
using ESP-NOW, which utilizes Wi-Fi to send each of the receivers ID and location to the quarterback.

References:
- DWM1001 FIRWARE API GUIDE: https://forum.qorvo.com/uploads/default/original/1X/dcac22d1c0feaf8238f68d11515ad55dbef1b963.pdf
- GPT-5 was used for understanding and troubleshooting the code.
*/

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>

// UART to DWM1001
HardwareSerial DWM(2); // RX=16, TX=17
String lineBuffer = "";

// Data Structures for Location, Quality, and ID
typedef struct __attribute__((packed)) {
  uint64_t id;
  float x;
  float y;
  float z;
  float q;
} PositionData;

typedef struct __attribute__((packed)) {
  uint64_t ackFrom;   // QB ID
  uint64_t ackTo;     // Receiver ID
} AckData;

PositionData posData;
AckData ackData;

// Quarterback MAC address
uint8_t qbAddress[] = {0xA0, 0xB7, 0x65, 0x37, 0x06, 0xE4};

// Unique DWM1001 receiver ID
// pi: 0xDECA114583701E85ULL
// 32.2: 0xDECA4B5BCBB00FA3ULL
#define RECEIVER_ID 0xDECA4B5BCBB00FA3ULL

/**
OnDataSent - Triggered after ESP-NOW transmission completes.
@param mac_addr pointer to the MAC address of the receiver
@param status result of the send operation (success or fail)

Prints the result of the ESP-NOW transmission to the serial monitor.
*/
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("ESP-NOW send status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}

/**
OnDataRecv - Triggered when ESP-NOW data is received.
@param mac pointer to the MAC address of the sender
@param incomingData pointer to the received data buffer
@param len length of the received data

If the data matches AckData size, prints acknowledgment info from QB.
Otherwise, prints a warning about unexpected packet size.
*/
void OnDataRecv(const uint8_t * mac, const uint8_t *incomingData, int len) {
  if (len == sizeof(AckData)) {
    memcpy(&ackData, incomingData, sizeof(AckData));
    Serial.printf("ACK received from QB [0x%llX] for Receiver [0x%llX]\n", ackData.ackFrom, ackData.ackTo);
  } else {
    Serial.printf("Unexpected packet size %d bytes\n", len);
  }
}

/**
setup - Initializes UART, Wi-Fi, ESP-NOW, and connection to the QB.

Configures serial ports, sets Wi-Fi to station mode, initializes ESP-NOW,
registers callbacks, adds QB communication, and starts continuous location
output from the DWM1001 module.
*/
void setup() {
  Serial.begin(115200);
  DWM.begin(115200, SERIAL_8N1, 16, 17);

  Serial.println("Receiver ESP32: UART + ESP-NOW");

  WiFi.mode(WIFI_STA);
  Serial.print("Receiver MAC: ");
  Serial.println(WiFi.macAddress());

  // Force channel 1 to match QB
  esp_err_t chResult = esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  Serial.printf("esp_wifi_set_channel() returned: %d\n", chResult);

  esp_err_t initResult = esp_now_init();
  Serial.printf("esp_now_init() returned: %d\n", initResult);
  if (initResult != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    Serial.flush();
    return;
  }

  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, qbAddress, 6);
  peerInfo.channel = 1;   // match QB’s channel
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add QB esp");
    return;
  } else {
    Serial.println("QB esp added successfully");
  }

  delay(1000);

  // Start continuous location output from DWM1001
  DWM.print("\r\r");
  delay(5000);
  DWM.print("\r\r");
  DWM.print("lec\r\n");
}

/**
parseLine - Parses a line of UART data from the DWM1001 module.
@param line the input string containing position data

Extracts position values (x, y, z, q) from the line if it contains
a "POS," entry, assigns the receiver ID, and sends the data to the QB
using ESP-NOW.
*/
void parseLine(String line) {
  int posIndex = line.indexOf("POS,");
  if (posIndex >= 0) {
    int first = line.indexOf(',', posIndex);
    int second = line.indexOf(',', first + 1);
    int third  = line.indexOf(',', second + 1);
    int fourth = line.indexOf(',', third + 1);

    if (first > 0 && second > first && third > second && fourth > third) {
      posData.id = RECEIVER_ID;
      posData.x = line.substring(first + 1, second).toFloat();
      posData.y = line.substring(second + 1, third).toFloat();
      posData.z = line.substring(third + 1, fourth).toFloat();
      posData.q = line.substring(fourth + 1).toFloat();

      Serial.printf("Sending ID=0x%llX: X=%.2f, Y=%.2f, Z=%.2f, Q=%.2f\n",
                    posData.id, posData.x, posData.y, posData.z, posData.q);

      esp_err_t result = esp_now_send(qbAddress, (uint8_t *) &posData, sizeof(posData));
      Serial.printf("esp_now_send() result: %d\n", result);
    }
  }
}

/**
loop - Main program loop.

Continuously reads UART data from the DWM1001. When a full line is received,
it is parsed and sent to the quarterback.
*/
void loop() {
  while (DWM.available()) {
    char c = DWM.read();
    Serial.write(c);
    if (c == '\n') {
      parseLine(lineBuffer);
      lineBuffer = "";
    } else if (c != '\r') {
      lineBuffer += c;
    }
  }
}