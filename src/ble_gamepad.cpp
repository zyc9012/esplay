
#include <NimBLEDevice.h>
#include "gamepad.h"

extern "C"
{
    void set_external_gamepad_state(input_gamepad_state state);
}

void scanEndedCB(NimBLEScanResults results);

static NimBLEAdvertisedDevice *advDevice;

static bool doConnect = false;
static uint32_t scanTime = 0; /** 0 = scan forever */

typedef struct
{
    uint8_t axis1_x; // default 0x80, from 0x00 to 0xff
    uint8_t axis1_y; // default 0x80, from 0x00 to 0xff
    uint8_t axis2_x; // default 0x80, from 0x00 to 0xff
    uint8_t axis2_y; // default 0x80, from 0x00 to 0xff
    uint8_t dpad;    // default 0x88. up: 0x00, upright: 0x11, right: 0x22, rightdown: 0x33, down: 0x44, ...
    uint8_t button1; // bits: RB LB 0 Y X 0 B A (xbox)
    uint8_t button2; // start: 0x04, menu: 0x08
    uint8_t res1;
    uint8_t res2;
    uint8_t res3;
} GamepadInput;

class AdvertisedDeviceCallbacks : public NimBLEAdvertisedDeviceCallbacks
{

    void onResult(NimBLEAdvertisedDevice *advertisedDevice)
    {
        Serial.print("Advertised Device found: ");
        Serial.println(advertisedDevice->toString().c_str());
        if (advertisedDevice->isAdvertisingService(NimBLEUUID((uint16_t)0x1812)))
        {
            Serial.println("Found Gamepad device");
            NimBLEDevice::getScan()->stop();
            /** Save the device reference in a global for the client to use*/
            advDevice = advertisedDevice;
            /** Ready to connect now */
            doConnect = true;
        }
    };
};

void notifyCB(NimBLERemoteCharacteristic *pRemoteCharacteristic, uint8_t *pData, size_t length, bool isNotify)
{
    if (length == 10)
    {
        GamepadInput *input = (GamepadInput *)pData;
        input_gamepad_state state = {
            0,
        };
        if (input->axis1_x < 0x60)
        {
            state.values[GAMEPAD_INPUT_LEFT] |= 1;
        }
        else if (input->axis1_x > 0xA0)
        {
            state.values[GAMEPAD_INPUT_RIGHT] |= 1;
        }

        if (input->axis1_y < 0x60)
        {
            state.values[GAMEPAD_INPUT_UP] |= 1;
        }
        else if (input->axis1_y > 0xA0)
        {
            state.values[GAMEPAD_INPUT_DOWN] |= 1;
        }

        if (input->dpad == 0x00)
        {
            state.values[GAMEPAD_INPUT_UP] |= 1;
        }
        else if (input->dpad == 0x11)
        {
            state.values[GAMEPAD_INPUT_UP] |= 1;
            state.values[GAMEPAD_INPUT_RIGHT] |= 1;
        }
        else if (input->dpad == 0x22)
        {
            state.values[GAMEPAD_INPUT_RIGHT] |= 1;
        }
        else if (input->dpad == 0x33)
        {
            state.values[GAMEPAD_INPUT_RIGHT] |= 1;
            state.values[GAMEPAD_INPUT_DOWN] |= 1;
        }
        else if (input->dpad == 0x44)
        {
            state.values[GAMEPAD_INPUT_DOWN] |= 1;
        }
        else if (input->dpad == 0x55)
        {
            state.values[GAMEPAD_INPUT_DOWN] |= 1;
            state.values[GAMEPAD_INPUT_LEFT] |= 1;
        }
        else if (input->dpad == 0x66)
        {
            state.values[GAMEPAD_INPUT_LEFT] |= 1;
        }
        else if (input->dpad == 0x77)
        {
            state.values[GAMEPAD_INPUT_LEFT] |= 1;
            state.values[GAMEPAD_INPUT_UP] |= 1;
        }

        if (input->button1 & 0x1)
        {
            state.values[GAMEPAD_INPUT_A] |= 1;
        }
        if (input->button1 & 0x2)
        {
            state.values[GAMEPAD_INPUT_B] |= 1;
        }
        if (input->button2 == 0x04)
        {
            state.values[GAMEPAD_INPUT_START] |= 1;
        }
        if (input->button2 == 0x08)
        {
            state.values[GAMEPAD_INPUT_MENU] |= 1;
        }
        set_external_gamepad_state(state);
    }
}

void scanEndedCB(NimBLEScanResults results)
{
    Serial.println("Scan Ended");
}

bool connectToServer()
{
    NimBLEClient *pClient = nullptr;

    /** Check if we have a client we should reuse first **/
    if (NimBLEDevice::getClientListSize())
    {
        /** Special case when we already know this device, we send false as the
         *  second argument in connect() to prevent refreshing the service database.
         *  This saves considerable time and power.
         */
        pClient = NimBLEDevice::getClientByPeerAddress(advDevice->getAddress());
        if (pClient)
        {
            if (!pClient->connect(advDevice, false))
            {
                Serial.println("Reconnect failed");
                return false;
            }
            Serial.println("Reconnected client");
        }
        /** We don't already have a client that knows this device,
         *  we will check for a client that is disconnected that we can use.
         */
        else
        {
            pClient = NimBLEDevice::getDisconnectedClient();
        }
    }

    /** No client to reuse? Create a new one. */
    if (!pClient)
    {
        if (NimBLEDevice::getClientListSize() >= NIMBLE_MAX_CONNECTIONS)
        {
            Serial.println("Max clients reached - no more connections available");
            return false;
        }

        pClient = NimBLEDevice::createClient();

        pClient->setConnectionParams(6, 6, 0, 51);
        pClient->setConnectTimeout(5);

        if (!pClient->connect(advDevice))
        {
            NimBLEDevice::deleteClient(pClient);
            Serial.println("Failed to connect, deleted client");
            return false;
        }
    }

    if (!pClient->isConnected())
    {
        if (!pClient->connect(advDevice))
        {
            Serial.println("Failed to connect");
            return false;
        }
    }

    Serial.print("Connected to: ");
    Serial.println(pClient->getPeerAddress().toString().c_str());
    Serial.print("RSSI: ");
    Serial.println(pClient->getRssi());

    NimBLERemoteService *pSvc = nullptr;
    NimBLERemoteCharacteristic *pChr = nullptr;
    NimBLERemoteDescriptor *pDsc = nullptr;

    pClient->getServices(true);
    pSvc = pClient->getService((uint16_t)0x1812);
    if (pSvc)
    {
        std::vector<NimBLERemoteCharacteristic *> *pChars = pSvc->getCharacteristics(true);
        for (NimBLERemoteCharacteristic *c : *pChars)
        {
            Serial.println(c->toString().c_str());
            if (c->getUUID().equals(NimBLEUUID((uint16_t)0x2A4D)))
            {
                if (c->canNotify())
                {
                    if (!c->subscribe(true, notifyCB))
                    {
                        Serial.printf("Failed to subscribe to: %s\n", c->getUUID().toString().c_str());
                    }
                }
            }
        }
    }
    else
    {
        Serial.println("Failed to find HID service");
    }

    return true;
}

void ble_gamepad_setup()
{
    Serial.begin(115200);
    Serial.println("Starting NimBLE Client");
    NimBLEDevice::init("");
    NimBLEDevice::setSecurityAuth(BLE_SM_PAIR_AUTHREQ_SC);

#ifdef ESP_PLATFORM
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); /** +9db */
#else
    NimBLEDevice::setPower(9); /** +9db */
#endif

    NimBLEScan *pScan = NimBLEDevice::getScan();

    pScan->setAdvertisedDeviceCallbacks(new AdvertisedDeviceCallbacks());
    pScan->setInterval(45);
    pScan->setWindow(15);
    pScan->setActiveScan(true);
    pScan->start(scanTime, scanEndedCB);
}

void ble_gamepad_loop(void *param)
{
    while (true)
    {
        while (!doConnect)
        {
            delay(1);
        }

        doConnect = false;

        if (!connectToServer())
        {
            Serial.println("Failed to connect, starting scan");
            NimBLEDevice::getScan()->start(scanTime, scanEndedCB);
        }
    }
}