#include <EEPROM.h>
#include <WiFiNINA.h>

#include <SoftwareSerial.h>
#include <stdio.h>
#include <cppQueue.h>

#include "secret-defines.h"
#include "rolling_code.h"
#include "cert.h"

#define uint unsigned int
#define ushort unsigned short

// Useful HC-05 AT doc: https://www.katranji.com/tocimages/files/402491-535604.pdf
// Another one: https://s3-sa-east-1.amazonaws.com/robocore-lojavirtual/709/HC-05_ATCommandSet.pdf
// Another one (the datasheet): https://components101.com/sites/default/files/component_datasheet/HC-05%20Datasheet.pdf
// This comment seemed like it would be SUPER helpful, and it DOES get them out of AT mode, but once I reconnect the enable pins and cycle power off and on again, they go right back to long blinking like they are in AT Command mode
    // VERY IMPORTANT DISCOVERY, having the enable pin on high voltage automatically puts the HC-05 into command mode!!
        // So for data mode, and regular operation, the enable pin should not be plugged in anywhere!

// I got the HC-05 chips working!
// Important notes: 
    // The slave (sender) must have it's BIND address set to all zeros (AT+BIND=0,0,0)
    // The master (receiver) must have it's BIND address set to the slave's address
    // The master and slave must have the same pin set
        // See pin with AT+PSWD? and assign with AT+PSWD=NEW_PIN
    // It ended up working with the master having its class set to 0, meaning it would only accept connections from the BIND address. 
    // It may also work with AT+CLASS=1, which should accept connections from ALL addresses, but I only got it working with AT+CLASS=0, which is more secure anyways
    // The master and slave must have the same baud rate (AT+UART=38400,0,0)
    // To enter command mode (where you can send AT commands), plug in the enable pin to 5V, and cycle the power on and off.
    // To go back to data mode, unplug the enable pin and cycle the power again.
    // I simply set all of the above to the proper values, and switched to data mode and they automatically paired!!
    // They are in command mode if they stay on for ~2 seconds, then off for ~2 seconds and repeat
    // They are waiting for connections if they are constantly flashing
    // They are paired if they cycle through 1 quick flash, then 2 seconds with nothing, and repeat
    // Also, do not add any SoftwareSerial.listen() calls for the HC-05s, they seem to just break things

// Whether we are on UIC WiFi or home WiFi
#define UIC_WIFI 1
    // Connecting to UIC WiFi is not working yet, though I am currently in a correspondence with the ACCC discussing options
SoftwareSerial BTSerial(10, 11); // RX, TX

// Twilio RAM Variables
String base64Auth;
String twilioPostURL;
String twilioSID;
String httpPostStr;

void(*resetFunc)() = 0; // Arduino reset function at address 0

uint ROM_LEN;

// Using a queue (character by character) as just reading the string and using indexOf did not work
cppQueue queue(sizeof(char), 50, FIFO, true);

void clearEEPROM(){
    for(int i = 0; i < ROM_LEN; i++){
        EEPROM.write(i, 0xFF); // https://docs.arduino.cc/learn/built-in-libraries/eeprom:
                               // "Locations that have never been written to have the value of 255"
    }
}

// returns 0 on an error or invalid string
byte getStrLenFromEEPROM(uint addr){
    if(addr > ROM_LEN){
        return 0;
    }

    byte len = EEPROM.read(addr);

    for(int i = 0; i < len; i++){
        if(EEPROM.read(addr + i) == 0xFF){
            return 0;
        }
    }
    
    return len;
}


// A variable arguments function to configure every one of the numPins pins to OUTPUT mode
// Uses stdio.h's varargs
void configurePinsOut(ushort numPins, ...){ 
    va_list argList;
    va_start(argList, numPins);

    // Loop down (decrementing) while numPins is not 0
    for(; numPins; numPins--){
        pinMode(va_arg(argList, unsigned int), OUTPUT);
    }
}

// Returns next free address in EEPROM
// String can be a max length of 255 characters, as I am storing the length of the string in the first (SINGLE) byte
// DO NOT pass anything larger than 255
// No error checking or validation is done, besides checking the length of the string against the max EEPROM address
uint writeCStrToEEPROM(uint addrOffset, char* strToWrite, byte len){
    if(addrOffset + (uint)len + 1 > ROM_LEN - 1)
    {
        Serial.println("ERROR: EEPROM is full\nwriteCStrToEEPROM did not write anything to EEPROM");
        return 0;
    }
    EEPROM.write(addrOffset, len);
    
    int i;
    for (i = 0; i < len; i++)
    {
        EEPROM.write(addrOffset + 1 + i, strToWrite[i]);
    }

    return addrOffset + i + 2;
}

void readCStrFromEEPROM(const uint &addrOffset, char* data){
    uint newStrLen = EEPROM.read(addrOffset);

    for (uint i = 0; i < newStrLen; i++)
    {
        data[i] = EEPROM.read(addrOffset + 1 + i);
    }
    data[newStrLen] = 0x0;
    return data;
}

IPAddress dns(8, 8, 8, 8);  // Google's DNS Server IP

RollingCodes rollingCodes;

void setupTwilioVars(){
    char base64AuthCStr[getStrLenFromEEPROM(authAddr) + 1];
    readCStrFromEEPROM(authAddr, base64AuthCStr);
    base64Auth = String(base64AuthCStr);

    char twilio_sid_cstr[getStrLenFromEEPROM(twilioSIDAddr) + 1];
    readCStrFromEEPROM(twilioSIDAddr, twilio_sid_cstr);
    twilioSID = String(twilio_sid_cstr);

    twilioPostURL = "https://api.twilio.com/2010-04-01/Accounts/" + twilioSID + "/Messages.json";
    httpPostStr = String("POST /2010-04-01/Accounts/" + twilioSID+ "/Messages.json HTTP/1.1");
}

void setup(){
    Serial.begin(9600);

    long seed = EEPROM.get<long>(rollingSeedAddr, seed);
    // rollingCodes.setSeed(seed);
    rollingCodes.setSeed(1234); // For testing

    BTSerial.begin(38400);

    ROM_LEN = EEPROM.length();

    uint ssidLen = UIC_WIFI ? getStrLenFromEEPROM(uicWifiSSID) : getStrLenFromEEPROM(ssidAddr);
    uint passLen = UIC_WIFI ? getStrLenFromEEPROM(uicWifiPassword) : getStrLenFromEEPROM(passAddr);
    uint usernameLen = getStrLenFromEEPROM(uicWifiUsername);
    char ssid[ssidLen + 1];
    char pass[passLen + 1];
    char username[usernameLen + 1];

    readCStrFromEEPROM(UIC_WIFI ? uicWifiSSID : ssidAddr, ssid);
    readCStrFromEEPROM(UIC_WIFI ? uicWifiPassword : passAddr, pass);
    readCStrFromEEPROM(uicWifiUsername, username);

    if(WiFi.status() != WL_CONNECTED){
        Serial.println("Connecting to WiFi...");
        if(UIC_WIFI) {
            // esp_wpa2_config_t config = WPA2_CONFIG_INIT_DEFAULT(); 
            Serial.println("Connecting to " + String(ssid) + " with username " + String(username));
            WiFi.beginEnterprise(ssid, username, pass, "Anonymous", cert);
        }
        else 
            WiFi.begin(ssid, pass);
        
        if(UIC_WIFI){
            Serial.println("SSID: " + String(ssid));
            Serial.println("WiFi status code: " + String(WiFi.status()));
        }
        delay(1000);
    }
    if(WiFi.status() != WL_CONNECTED && UIC_WIFI){
        Serial.println("It looks like you still can't connect to UIC WiFi.\n\tContinuing without WiFi access...");
    }

    if(!UIC_WIFI) WiFi.config(IPAddress(192, 168, 0, 182), dns, IPAddress(192, 168, 0, 1), IPAddress(255, 255, 255, 0));

    Serial.print("Connected to network " + String(WiFi.SSID()) + " with IP Address: ");
    Serial.println(WiFi.localIP());

    long rssi = WiFi.RSSI();
    Serial.print("signal strength (RSSI): ");
    Serial.print(rssi);
    Serial.println(" dBm");

    String fv = WiFi.firmwareVersion();
    Serial.println("WiFi Firmware Version: " + fv);

    Serial.println("Wifi Status: " + String(WiFi.status()));

    setupTwilioVars();

    Serial.println("Arduino Setup Complete.");
}

void initializeQueueForHTTPFind(){
    char toAdd = 'H';
    queue.push(&toAdd);

    toAdd = 'T';
    queue.push(&toAdd);

    toAdd = 'T';
    queue.push(&toAdd);

    toAdd = 'P';
    queue.push(&toAdd);

    toAdd = '/';
    queue.push(&toAdd);

    toAdd = '1';
    queue.push(&toAdd);

    toAdd = '.';
    queue.push(&toAdd);

    toAdd = '1';
    queue.push(&toAdd);

    toAdd = ' ';
    queue.push(&toAdd);
}

bool sendText(String phoneNumber, String msg){
    WiFiSSLClient client;

    String postData = "Body=" + msg + "&From=" + twilioNumber + "&To=" + phoneNumber;

    if(client.connect("api.twilio.com", 443)){
        client.println(httpPostStr);
        client.println("Host: api.twilio.com");
        client.print("Authorization: Basic ");
        client.println(base64Auth);
        client.println("Accept: */*");
        // client.println("Accept: text/plain");
        client.println("Content-Type: application/x-www-form-urlencoded");
        client.print("Content-Length: ");
        client.println(postData.length());
        client.println();
        client.print(postData);
    } else {
        Serial.println("ERROR: Could not connect to server");
    }

    bool foundHttpCode = false;
    int httpCode = -1;
    
    initializeQueueForHTTPFind();

    while(client.connected()){
        if(client.available()){
            // String entireContents = client.readString();
            // if(entireContents.indexOf("HTTP/1.1 201 Created") != -1){
            //     found201 = true;
            // }

            char c = client.read();
            char qc;

            queue.peek(&qc);
            if(c == qc){
                queue.drop();
                if(queue.isEmpty()){
                    foundHttpCode = true;

                    String builder = "";
                    for(int i = 0; i < 3; i++){
                        c = client.read();
                        builder += c;
                    }

                    httpCode = builder.toInt();
                    break;
                }
            } else {
                queue.flush();
                initializeQueueForHTTPFind();
            }

            // Serial.print(c);
        }
    }

    client.stop();

    if(foundHttpCode){
        if(httpCode == 201){
            Serial.println("Text sent successfully. HTTP Code: 201.");
            return true;
        } else {
            Serial.println("ERROR: Failed to send text message. HTTP Code: " + String(httpCode) + ".");
            return false;
        }
    } else {
        Serial.println("ERROR: Text not sent. No HTTP Code found.");
        return false;
    }
}

bool sendToastMessage(String code, int totalSeconds){
    static const char* toastTxtFmt = "Your toast is ready. It took %d minutes and %d seconds to toast your bread.\nCome get it!";

    int mins, seconds;
    // secondsToHMS(totalSeconds, mins, seconds);

    seconds = totalSeconds % 60;
    mins = ((totalSeconds)/60) % 60;
    
    char buf[120];
    sprintf(buf, toastTxtFmt, mins, seconds);

    sendText("+17736103537", String(buf));
}

void loop() {
    static bool firstRun = true;

    // BTSerial.listen();
    if(BTSerial.available() > 0){
        
        // char readChar = BTSerial.read();
        // Serial.print(readChar);


        String read = BTSerial.readString();
        signed int timeStartIndex = read.indexOf("FIN:");
        if(timeStartIndex == -1){
            Serial.println("Received improper string from BTSerial: " + read);
            return;
        }
        timeStartIndex += 4; // Advance to beginning of actual number

        signed int secondHalfCode = read.indexOf(";", (uint)timeStartIndex);
        if(secondHalfCode == -1){
            Serial.println("Received improper string from BTSerial: " + read);
            return;
        }
        secondHalfCode += 1; // Advance to beginning of second half past the ";"

        String code = read.substring(0, timeStartIndex - 4) + read.substring(secondHalfCode);

        int totalSeconds = read.substring(timeStartIndex, secondHalfCode - 1).toInt();
        if(totalSeconds == 0){
            Serial.println("Received improper string from BTSerial: " + read);
            return;
        }

        rollingCodes.resetSeed();

        Serial.println("Received code. For debugging purposes, the code is not being checked.\n\tAttempting to send text!");
        sendToastMessage(code, totalSeconds);

        /*
        if(rollingCodes.verifyNextCode(code)){
            Serial.println("Code verified. Sending toast message...");
        // Serial.println("Different hardware so different random code. Sending toast message anyways...");
        sendToastMessage(code, totalSeconds);
        } else {
            Serial.println("!!! BTSerial received correctly formatted string with a NON-VALID CODE! Message below:\n\t" + read);
            Serial.println("DEBUG: Parsed code as: " + code);
        }
        */

    }

    if(Serial.available() > 0){
        String input = Serial.readStringUntil('\n');

        input.trim(); // Uses isspace, which removes \r and \n: https://cplusplus.com/reference/cctype/isspace/

        if(input.equalsIgnoreCase("StartBTLoop")){
            Serial.println("Starting BT Loop. Type 'exit!' to quit");
            
            // Using for AT commands: https://www.teachmemicro.com/hc-05-bluetooth-command-list/
            while(!input.equals("exit!")){
                if(Serial.available() > 0){
                    input = Serial.readStringUntil('\n');
                    if(input.equals("exit!"))
                        break;
                    Serial.println("Attempting to print: " + input);
                    size_t written;
                    if( (written = BTSerial.print(input + "\r\n")) != input.length() + 2){
                        Serial.println("Write failed. Only wrote " + String(written) + " bytes");
                    } else Serial.println("Sent successfully");
                }

                // BTSerial.listen();
                if(BTSerial.available() > 0){
                    // Serial.println(BTSerial.readString());
                    Serial.print((char)BTSerial.read());
                }
            }

            Serial.println("Exiting BT Loop");
            return;
        }

        int spaceIndex = -1;
        String firstArg = "";
        String secondArg = "";
        String thirdArg = "";
        String fourthArg = "";

        if((spaceIndex = input.indexOf(' ')) != -1){
            firstArg = input.substring(0, spaceIndex);
            secondArg = input.substring(spaceIndex + 1);
    
            if((spaceIndex = input.indexOf(' ', spaceIndex + 1)) != -1){
                thirdArg = input.substring(spaceIndex + 1);

                if((spaceIndex = input.indexOf(' ', spaceIndex + 1)) != -1){
                    fourthArg = input.substring(spaceIndex + 1);
                }
            }
        }

        bool firstTwoArgsSet = firstArg != "" && secondArg != "";
        bool firstThreeArgsSet = firstTwoArgsSet && thirdArg != "";


        if(input.equalsIgnoreCase("reset")){
            Serial.println("Resetting...");
            delay(200);
            resetFunc();
        } else if(input.equalsIgnoreCase("clear")){
            clearEEPROM();
            Serial.println("EEPROM cleared");
        }
        else if(firstArg.equalsIgnoreCase("read")){
            uint addr;
            // if(  (firstTwoArgsSet && (addr = secondArg.toInt()) > 0) || 
            // (firstTwoArgsSet && thirdArg.equals("--force"))  ){
            if(  (firstTwoArgsSet && (addr = secondArg.toInt()) > 0) && addr < ROM_LEN){
                byte strLen = getStrLenFromEEPROM(addr);

                if(strLen != 0){
                    char str[strLen + 1];

                    readCStrFromEEPROM(addr, str);
                    Serial.println("String at address " + String(addr) + ": " + String(str));

                } else {
                    Serial.println("ERROR: String at address " + String(addr) + " is not a valid string");
                }
            } else {
                Serial.println("ERROR: Invalid address");
            }
        }
        
        else if(input.equalsIgnoreCase("help")){
            Serial.println("Available commands:");
            Serial.println("\treset - resets the Arduino");
            Serial.println("\tconnect - connects to WiFi");
            Serial.println("\tclear - clears the EEPROM");
            Serial.println("\tread <address> - reads the EEPROM");
            Serial.println("\nwritestr|write <addr:integer> <str: string without quotes> - writes str to the EEPROM at addr");
            Serial.println("\tlength - prints the total byte count available in the EEPROM");
            Serial.println("\thelp - prints this message");
        } else if(firstThreeArgsSet && (firstArg.equalsIgnoreCase("writestr") || firstArg.equalsIgnoreCase("write"))){
            uint addr = secondArg.toInt();
            if(addr != 0 || secondArg.equals("0")){
                const char* str = thirdArg.c_str();
                uint nextAvailable = writeCStrToEEPROM(addr, str, thirdArg.length());

                if(nextAvailable != 0){
                    Serial.println("Wrote string of length " + String(thirdArg.length()) + " to EEPROM at address " + String(addr));
                    Serial.println("WRITE THIS DOWN: Next available address is " + String(nextAvailable));
                } else {
                    Serial.println("Something went wrong.");
                }
            } else {
                Serial.println("ERROR: Invalid address");
            }
        } else if(input.equalsIgnoreCase("length")){
            Serial.println("Total EEPROM length: " + String(ROM_LEN));
        }
        // else if(firstTwoArgsSet && firstArg.equalsIgnoreCase("send")){
        //     Serial.println("Attempting to send SMS...");

        //     char base64AuthCStr[getStrLenFromEEPROM(authAddr) + 1];
        //     readCStrFromEEPROM(authAddr, base64AuthCStr);
        //     String base64AuthStr = String(base64AuthCStr);

        //     char twilio_sid_cstr[getStrLenFromEEPROM(twilioSID) + 1];
        //     readCStrFromEEPROM(twilioSID, twilio_sid_cstr);
        //     String twilio_sid = String(twilio_sid_cstr);

        //     String twilio_url = "https://api.twilio.com/2010-04-01/Accounts/" + twilio_sid + "/Messages.json";
        //     String postCStr = String("POST /2010-04-01/Accounts/" + twilio_sid + "/Messages.json HTTP/1.1").c_str();

        //     WiFiSSLClient client;

        //     String theRest = input.substring(input.indexOf(' ') + 1);
        //     String postData = "Body=" + theRest + "&From=" + twilio_number + "&To=" + my_number;

        //     if(client.connect("api.twilio.com", 443)){
        //         client.println(postCStr);
        //         client.println("Host: api.twilio.com");
        //         client.print("Authorization: Basic ");
        //         client.println(base64AuthCStr);
        //         // client.println("Accept: */*");
        //         client.println("Accept: text/plain");
        //         client.println("Content-Type: application/x-www-form-urlencoded");
        //         client.print("Content-Length: ");
        //         client.println(postData.length());
        //         client.println();
        //         client.print(postData);
        //     } else {
        //         Serial.println("ERROR: Could not connect to server");
        //     }

        //     /*
        //     // Read the response (including HTTP headers)
        //     while(client.connected()){
        //         if(client.available()){
        //             char c = client.read();
        //             Serial.print(c);
        //         }
        //     }
        //     */

        //     if (client.connected()){
        //         client.stop();
        //     }
        // } 
        else {
            Serial.println("Invalid command. Type 'help' for a list of commands");
        }   
    }
}