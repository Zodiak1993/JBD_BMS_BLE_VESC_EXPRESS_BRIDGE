/* JBD_BMS_BLE_VESC_EXPRESS_BRIDGE:
 *
 *  An ESP32 firmware and companion LispBM script
 *  to connect multiple JBD BLE BMSs to VESC-EXPRESS via ESPNOW
 *
 *  Created: September 4 2024
 *  Author: A-damW, https://github.com/A-damW
 *
*/

#include <NimBLEDevice.h>
#include <esp_now.h>
#include <WiFi.h>
#include "mydatatypes.h"

// ----------------- User Config -----------------

// BMS MAC addresses filter (leave empty {} to connect to any BMS)
std::string bmsBLEMacAddressesFilter[] = {"a5:c2:37:1a:f5:f2"};

// VESC Express MAC address for ESP-NOW
uint8_t expressAddress[] = {0x28, 0x37, 0x2F, 0x74, 0x5E, 0xB5};

// ----------------- Declarations -----------------

static NimBLEAdvertisedDevice* advDevice;
static bool doConnect = false;
static uint32_t scanTime = 0; // 0 = scan forever

#define TRACE // Platzhalter für Debug-Trace
#define commSerial Serial

packBasicInfoStruct packBasicInfo;
packEepromStruct packEeprom;
packCellInfoStruct packCellInfo;

const byte cBasicInfo3 = 3;
const byte cCellInfo4  = 4;

// BLE Service / Characteristic UUIDs
static BLEUUID serviceUUID("0000ff00-0000-1000-8000-00805f9b34fb");
static BLEUUID charUUID_rx("0000ff01-0000-1000-8000-00805f9b34fb");
static BLEUUID charUUID_tx("0000ff02-0000-1000-8000-00805f9b34fb");

bool newPacketReceived = false;

esp_now_peer_info_t peerInfo;

// ----------------- Callbacks -----------------

void OnDataSent(const uint8_t* mac_addr, esp_now_send_status_t status) {
    // Optional: Status der ESP-NOW Sendung ausgeben
}

class ClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* pClient) {
        commSerial.println("Connected");
    }

    void onDisconnect(NimBLEClient* pClient) {
        commSerial.print(pClient->getPeerAddress().toString().c_str());
        commSerial.println(" Disconnected - Starting scan");
        NimBLEDevice::getScan()->start(scanTime, scanEndedCB);
    }

    bool onConnParamsUpdateRequest(NimBLEClient* pClient, const ble_gap_upd_params* params) {
        if (params->itvl_min < 24 || params->itvl_max > 40 || params->latency > 2 || params->supervision_timeout > 100) {
            return false;
        }
        return true;
    }
};

class AdvertisedDeviceCallbacks : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) {
        commSerial.print("Advertised Device found: ");
        commSerial.println(advertisedDevice->toString().c_str());
        if (advertisedDevice->isAdvertisingService(serviceUUID)) {
            commSerial.println("Found Our Service");
            NimBLEDevice::getScan()->stop();
            advDevice = advertisedDevice;
            doConnect = true;
            NimBLEDevice::whiteListAdd(advertisedDevice->getAddress());
        }
    }
};

void notifyCB(NimBLERemoteCharacteristic* pRemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
    bleCollectPacket((char*)pData, length);
}

void scanEndedCB(NimBLEScanResults results) {
    commSerial.println("Scan Ended");
}

static ClientCallbacks clientCB;

// ----------------- Helper Functions -----------------

int find(std::string key) {
    int index = -1;
    for (uint8_t i = 0; i < sizeof(bmsBLEMacAddressesFilter)/sizeof(std::string); i++) {
        if (bmsBLEMacAddressesFilter[i].compare(key) == 0) {
            index = i;
            break;
        }
    }
    return index;
}

// ----------------- BLE Connection -----------------

bool connectToServer() {
    NimBLEClient* pClient = nullptr;

    if (NimBLEDevice::getClientListSize()) {
        pClient = NimBLEDevice::getClientByPeerAddress(advDevice->getAddress());
        if (pClient) {
            if (!pClient->connect(advDevice, false)) {
                commSerial.println("Reconnect failed");
                return false;
            }
            commSerial.println("Reconnected client");
        } else {
            pClient = NimBLEDevice::getDisconnectedClient();
        }
    }

    if (!pClient) {
        if (NimBLEDevice::getClientListSize() >= NIMBLE_MAX_CONNECTIONS) {
            commSerial.println("Max clients reached - no more connections available");
            return false;
        }

        pClient = NimBLEDevice::createClient();
        commSerial.println("New client created");
        pClient->setClientCallbacks(&clientCB, false);
        pClient->setConnectionParams(20, 40, 0, 100);
        pClient->setConnectTimeout(5);

        if (!pClient->connect(advDevice)) {
            NimBLEDevice::deleteClient(pClient);
            commSerial.println("Failed to connect, deleted client");
            return false;
        }
    }

    if (!pClient->isConnected()) {
        if (!pClient->connect(advDevice)) {
            commSerial.println("Failed to connect");
            return false;
        }
    }

    commSerial.print("Connected to: ");
    commSerial.println(pClient->getPeerAddress().toString().c_str());
    commSerial.print("RSSI: ");
    commSerial.println(pClient->getRssi());

    NimBLERemoteService* pSvc = pClient->getService(serviceUUID);
    if (pSvc) {
        NimBLERemoteCharacteristic* pChr = pSvc->getCharacteristic(charUUID_rx);
        if (pChr) {
            if (pChr->canWrite()) {
                uint8_t data[7] = {0xdd, 0xa5, 0x3, 0x0, 0xff, 0xfd, 0x77};
                if (!pChr->writeValue(data, sizeof(data), true)) {
                    pClient->disconnect();
                    return false;
                }
            }
            if (pChr->canNotify()) {
                if (!pChr->subscribe(true, notifyCB)) {
                    pClient->disconnect();
                    return false;
                }
            }
        }
    } else {
        commSerial.println("JBD BMS not found.");
    }

    commSerial.println("Done with this device!");
    return true;
}

// ----------------- Setup / Loop -----------------

void setup() {
    commSerial.begin(9600);
    commSerial.println("Starting NimBLE Client");

    // Absolut minimale Anpassung: GPIO0 einmal HIGH setzen
    delay (1000);
    pinMode(1, OUTPUT);
    digitalWrite(1, HIGH);

    WiFi.mode(WIFI_STA);

    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }
    esp_now_register_send_cb(OnDataSent);

    memcpy(peerInfo.peer_addr, expressAddress, 6);
    peerInfo.channel = 1;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("Failed to add peer");
        return;
    }

    NimBLEDevice::init("");

    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(new AdvertisedDeviceCallbacks());
    pScan->setInterval(45);
    pScan->setWindow(15);

    for (byte i = 0; i < sizeof(bmsBLEMacAddressesFilter)/sizeof(std::string); ++i) {
        NimBLEDevice::whiteListAdd(bmsBLEMacAddressesFilter[i]);
    }

    if (sizeof(bmsBLEMacAddressesFilter)/sizeof(std::string) == 0) {
        pScan->setFilterPolicy(BLE_HCI_SCAN_FILT_NO_WL);
    } else {
        pScan->setFilterPolicy(BLE_HCI_SCAN_FILT_USE_WL);
    }
    pScan->setActiveScan(true);
    pScan->start(scanTime, scanEndedCB);
}

void loop() {
    while (!doConnect) {
        NimBLEClient* pClient = nullptr;
        NimBLERemoteService* pSvc = nullptr;
        NimBLERemoteCharacteristic* pChr = nullptr;

        if (NimBLEDevice::getClientListSize()) {
            for (auto i = 0; i < NimBLEDevice::getWhiteListCount(); ++i) {
                pClient = NimBLEDevice::getClientByPeerAddress(NimBLEDevice::getWhiteListAddress(i));
                if (pClient && pClient->isConnected()) {
                    pSvc = pClient->getService(serviceUUID);
                    if (pSvc) {
                        pChr = pSvc->getCharacteristic(charUUID_tx);
                        if (pChr && pChr->canWriteNoResponse()) {
                            uint8_t data[7] = {0xdd, 0xa5, 0x3, 0x0, 0xff, 0xfd, 0x77};
                            pChr->writeValue(data, sizeof(data), true);
                        }
                    }

                    delay(150);

                    if (newPacketReceived) {
                        String socStr = String(packBasicInfo.CapacityRemainPercent);
                        commSerial.println("📤 Sending SoC string: " + socStr);
                        esp_err_t result = esp_now_send(expressAddress, (uint8_t*)socStr.c_str(), socStr.length());
                        if (result == ESP_OK) {
                            commSerial.println("✅ SoC sent successfully");
                        } else {
                            commSerial.printf("❌ ESP-NOW send error: %d\n", result);
                        }
                        newPacketReceived = false;
                    }
                }
            }
        }

        delay(2000);
    }

    doConnect = false;

    if (connectToServer()) {
        commSerial.println("Success! Connected and ready for notifications!");
    } else {
        commSerial.println("Failed to connect, restarting scan");
    }

    NimBLEDevice::getScan()->start(scanTime, scanEndedCB);
}
