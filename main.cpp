/*
 * Copyright (c) 2020 Arm Limited
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mbed.h"
#include "rtos/ThisThread.h"
#include "NTPClient.h"

#include "certs.h"
#include "iothub.h"
#include "iothub_client_options.h"
#include "iothub_device_client.h"
#include "iothub_message.h"
#include "azure_c_shared_utility/shared_util_options.h"
#include "azure_c_shared_utility/threadapi.h"
#include "azure_c_shared_utility/tickcounter.h"
#include "azure_c_shared_utility/xlogging.h"
#include "GP2Y1010AU0F/GP2Y1010AU0F.cpp"
#include "iothubtransportmqtt.h"
#include <cctype>

#define BLINKING_RATE     100ms
#define TELEMETRY_RATE    2s

/**
 * This example sends and receives messages to and from Azure IoT Hub.
 * The API usages are based on Azure SDK's official iothub_convenience_sample.
 */

// Global symbol referenced by the Azure SDK's port for Mbed OS, via "extern"
NetworkInterface *_defaultSystemNetwork;

//Function declarations
bool initAzureMqtt(IOTHUB_DEVICE_CLIENT_HANDLE client_handle);
void cleanup(IOTHUB_DEVICE_CLIENT_HANDLE client_handle);
void sendDustTelemetry(IOTHUB_DEVICE_CLIENT_HANDLE client_handle);
void handleButtonRise();

static bool isPeriodicSend = true; //sending data in an interval of TELEMETRY_RATE
static bool buttonPressed = false;
static bool message_received = false;
GP2Y1010AU0F dust(LED1, PC_5, PD_14);

static void on_connection_status(IOTHUB_CLIENT_CONNECTION_STATUS result, IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason, void* user_context)
{
    if (result == IOTHUB_CLIENT_CONNECTION_AUTHENTICATED) {
        LogInfo("Connected to IoT Hub");
    } else {
        LogError("Connection failed, reason: %s", MU_ENUM_TO_STRING(IOTHUB_CLIENT_CONNECTION_STATUS_REASON, reason));
    }
}

//Handles recieved async messages
static IOTHUBMESSAGE_DISPOSITION_RESULT on_message_received(IOTHUB_MESSAGE_HANDLE message, void* user_context)
{
    LogInfo("Message received from IoT Hub");

    const unsigned char *data_ptr;
    size_t len;
    const char* data_ptr2;
    int blinkCount = 0;

    data_ptr2 = IoTHubMessage_GetProperty(message,"blink");
    DigitalOut led(LED1);
    blinkCount = atoi(data_ptr2);
    if(blinkCount > 0){
        LogInfo("Started to blink %d times", blinkCount);
        for(int i = 0; i < blinkCount; i++){
            led = !led;
            ThisThread::sleep_for(BLINKING_RATE);
        }
        LogInfo("Blinking Ended");
    }
    LogInfo("Message Property blink:%s ",data_ptr2);

    data_ptr2 = IoTHubMessage_GetProperty(message,"interval");
    LogInfo("Message Property interval:%s ",data_ptr2);
    if(strcmp(data_ptr2, "true") == 0){
        isPeriodicSend = true;
    }else if(strcmp(data_ptr2, "false") == 0){
        isPeriodicSend = false;
    }

    //Message Body
    if (IoTHubMessage_GetByteArray(message, &data_ptr, &len) != IOTHUB_MESSAGE_OK) {
        LogError("Failed to extract message data, please try again on IoT Hub");
        return IOTHUBMESSAGE_ABANDONED;
    }

    message_received = true;
    LogInfo("Message body: %.*s", len, data_ptr);
    return IOTHUBMESSAGE_ACCEPTED;
}

static void on_message_sent(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void* userContextCallback)
{
    if (result == IOTHUB_CLIENT_CONFIRMATION_OK) {
        LogInfo("Message sent successfully");
    } else {
        LogInfo("Failed to send message, error: %s",
            MU_ENUM_TO_STRING(IOTHUB_CLIENT_CONFIRMATION_RESULT, result));
    }
}




int main() {

    static const char connection_string[] = MBED_CONF_APP_IOTHUB_CONNECTION_STRING; //Connect to the IoT Hub
    bool trace_on = MBED_CONF_APP_IOTHUB_CLIENT_TRACE; //Trace from config
    tickcounter_ms_t interval = 100; //communication frequency
    IOTHUB_CLIENT_RESULT res; //response

    LogInfo("Connecting to the network");

    _defaultSystemNetwork = NetworkInterface::get_default_instance();
    if (_defaultSystemNetwork == nullptr) {
        LogError("No network interface found");
        return -1;
    }

    int ret = _defaultSystemNetwork->connect();
    if (ret != 0) {
        LogError("Connection error: %d", ret);
        return -1;
    }
    LogInfo("Connection success, MAC: %s", _defaultSystemNetwork->get_mac_address());

    LogInfo("Getting time from the NTP server");

    NTPClient ntp(_defaultSystemNetwork);
    ntp.set_server("time.google.com", 123);
    time_t timestamp = ntp.get_timestamp();
    if (timestamp < 0) {
        LogError("Failed to get the current time, error: %u", timestamp);
        return -1;
    }
    LogInfo("Time: %s", ctime(&timestamp));

    rtc_init();
    rtc_write(timestamp);
    time_t rtc_timestamp = rtc_read(); // verify it's been successfully updated
    LogInfo("RTC reports %s", ctime(&rtc_timestamp));  
    

    LogInfo("Starting the Demo");
    
    //MQTT Specific connections
    //Interrupt handler for button message sending, if interval is disabled
    InterruptIn btn1(BUTTON1);
    btn1.rise(handleButtonRise);

    //Create connection object
    IOTHUB_DEVICE_CLIENT_HANDLE client_handle = IoTHubDeviceClient_CreateFromConnectionString(
        connection_string,
        MQTT_Protocol
    );

    
    if(!initAzureMqtt(client_handle)){
        printf("MQTT init failed, please restart.");
        return -1;
    }


    // Send two message to the cloud (one per second)
    // or until we receive a message from the cloud
    



    while (true) {
         if (isPeriodicSend) {
            sendDustTelemetry(client_handle);
            ThisThread::sleep_for(TELEMETRY_RATE);
            dust.printAverageDensity();
         }else{
             if(buttonPressed){
                buttonPressed = false;
                sendDustTelemetry(client_handle);
                ThisThread::sleep_for(TELEMETRY_RATE);
                dust.printAverageDensity();
             }
         }
        
        
    }

    return 0;
}

bool initAzureMqtt(IOTHUB_DEVICE_CLIENT_HANDLE client_handle) {
    //static const char connection_string[] = MBED_CONF_APP_IOTHUB_CONNECTION_STRING;
    bool trace_on = MBED_CONF_APP_IOTHUB_CLIENT_TRACE;
    tickcounter_ms_t interval = 100;
    IOTHUB_CLIENT_RESULT res;

    LogInfo("Initializing IoT Hub client");
    IoTHub_Init();

    /*IOTHUB_DEVICE_CLIENT_HANDLE client_handle = IoTHubDeviceClient_CreateFromConnectionString(
        connection_string,
        MQTT_Protocol
    );*/
    if (client_handle == nullptr) {
        LogError("Failed to create IoT Hub client handle");
        cleanup(client_handle);
        return false;
    }

    // Enable SDK tracing
    res = IoTHubDeviceClient_SetOption(client_handle, OPTION_LOG_TRACE, &trace_on);
    if (res != IOTHUB_CLIENT_OK) {
        LogError("Failed to enable IoT Hub client tracing, error: %d", res);
        cleanup(client_handle);
        return false;
    }

    // Enable static CA Certificates defined in the SDK
    res = IoTHubDeviceClient_SetOption(client_handle, OPTION_TRUSTED_CERT, certificates);
    if (res != IOTHUB_CLIENT_OK) {
        LogError("Failed to set trusted certificates, error: %d", res);
        cleanup(client_handle);
        return false;
    }

    // Process communication every 100ms
    res = IoTHubDeviceClient_SetOption(client_handle, OPTION_DO_WORK_FREQUENCY_IN_MS, &interval);
    if (res != IOTHUB_CLIENT_OK) {
        LogError("Failed to set communication process frequency, error: %d", res);
        cleanup(client_handle);
        return false;
    }

    // set incoming message callback
    res = IoTHubDeviceClient_SetMessageCallback(client_handle, on_message_received, nullptr);
    if (res != IOTHUB_CLIENT_OK) {
        LogError("Failed to set message callback, error: %d", res);
        cleanup(client_handle);
        return false;
    }

    // Set connection/disconnection callback
    res = IoTHubDeviceClient_SetConnectionStatusCallback(client_handle, on_connection_status, nullptr);
    if (res != IOTHUB_CLIENT_OK) {
        LogError("Failed to set connection status callback, error: %d", res);
        cleanup(client_handle);
        return false;
    }
    return true;
}

void handleButtonRise() { buttonPressed = true; }

void cleanup(IOTHUB_DEVICE_CLIENT_HANDLE client_handle){
    IoTHubDeviceClient_Destroy(client_handle);
    IoTHub_Deinit();
    //LogInfo("End");
}

void sendDustTelemetry(IOTHUB_DEVICE_CLIENT_HANDLE client_handle){
    char message[80];
    IOTHUB_MESSAGE_HANDLE message_handle;
    IOTHUB_CLIENT_RESULT res;

    dust.measure();
    sprintf(message, "{\"messageId\":%d,\"dustSharp\":%f,\"dustCN\":%f}",
               dust.measureCount, dust.averageSharp, dust.averageCN);
    /*sprintf(message, "%d messages left to send, or until we receive a reply", 2);
        LogInfo("Sending: \"%s\"", message);*/

        message_handle = IoTHubMessage_CreateFromString(message);
        if (message_handle == nullptr) {
            LogError("Failed to create message");
            cleanup(client_handle);
        }

        res = IoTHubDeviceClient_SendEventAsync(client_handle, message_handle, on_message_sent, nullptr);
        IoTHubMessage_Destroy(message_handle); // message already copied into the SDK

        if (res != IOTHUB_CLIENT_OK) {
            LogError("Failed to send message event, error: %d", res);
            cleanup(client_handle);
        }
}