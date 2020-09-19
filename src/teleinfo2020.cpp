#ifdef DEBUG
#include <FS.h>                   //this needs to be first, or it all crashes and burns...
#include <ESP8266WebServer.h>     // Local WebServer 
#endif


#include <Arduino.h>
#include <ESP8266WiFi.h>          // ESP8266 Core WiFi Library 
#include <PubSubClient.h>

//#include <DNSServer.h>            // Local DNS Server used for redirecting all requests to the configuration portal
//#include <WiFiManager.h>          // https://github.com/tzapu/WiFiManager WiFi Configuration Magic
//#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
//#include <SoftwareSerial.h>

#include "teleinfo2020.h"
#include "secret.h"                // Store all private info (SECRET_*)

// WIFI connection
const char*     ssid            = SECRET_WIFI_SSID;            // The SSID (name) of the Wi-Fi network you want to connect to
const char*     password        = SECRET_WIFI_PASSWORD;       // The password of the Wi-Fi network
char            localIp[20];

// MQTT server connection
const char*     mqttServer      = SECRET_MQTT_SERVERNAME;
const int       mqttPort        = 1883;
const char*     mqttUser        = SECRET_MQTT_USER;
const char*     mqttPassword    = SECRET_MQTT_PASSWORD;
const char*     deviceName      = "node132";

// Built in led status
bool            led             = false;

// Buffer de reception
char            mqttBuffer[TMSG_SIZE_BUFFER];
// Liste des label a mettre en cache
const char*     cacheLabel  = TMSG_CACHE_LABEL;


WiFiClient      wifiCnx;
PubSubClient    mqttCnx(wifiCnx);
#ifdef DEBUG
ESP8266WebServer server (80);
File            fileLog;
#endif

TMsg            message;
TMsg            msgCache[TMSG_CACHE_SIZE];

// loop
int             iLoop;
int             errorCount      = 0;
int             bufferOverflow  = 0;
// Cache Hit ratio
int             cacheRatio      = 0;
int             cacheHit        = 0;
int             cacheMiss       = 0;
char            sItoaBuffer[25];

// Message control
int             messageStarted;
int             messageComplete;
int             messageFailed;
char            buffer[TMSG_SIZE_BUFFER + 1];
int             bufferIdx;
int             part[4];         // Les 4 partie du message (label, timestamp, value, checksum)
int             partIdx;
char            cChecksum;//Checksum calculé
char            rChecksum;//Checksum recu

// Puissance
int             _PCOUP      = 0;
int             _SINSTS     = 0;
int             puissance   = 99;

// Affichage
int             digitIndex  = 0;



/////////////////////////////////////////////////////////////////////////////////////////////////
//------------------------  M Q T T   C O N N E C T I O N  --------------------------------------
void mqttCallback(char* topic, byte* payload, unsigned int length) {

    unsigned int    i;

    debugln("Message recu =>  topic: " + String(topic));
    debug(" | longueur: " + String(length,DEC));

    for(i=0; i<length; i++) {                                                   // create character buffer with ending null terminator (string)
        mqttBuffer[i] = payload[i];
    }
    mqttBuffer[i] = '\0';

    String msgString = String(mqttBuffer);
    debugln("Payload: " + msgString);
}

void mqttConnect() {

    while (!mqttCnx.connected()) {
        if (mqttCnx.connect(deviceName, mqttUser, mqttPassword)) {
            debugln("MQTT connexion OK");
        } else {
            debug("MQTT Connexion failed with state ");
            debug(mqttCnx.state());
            delay(1000);
        }
    }

}

void mqttSend(const char* category, char* label, char* value) {

    mqttConnect();
    String topic = String(deviceName) + "/sensor/" + String(category) + "/" + String(label);
    mqttCnx.publish(topic.c_str(), String(value).c_str(), false); 

    debugln("MQTT " + String(topic) + ": " + String(value));
}


//////////////////////////////////////////////////////////////////////////////////////////////////
//------------------------ C A C H E    M A N A G E M E N T --------------------------------------
void initCacheLabel() {

    char    pLabel;
    int     iCacheLabel     = 0;
    int     iLabel          = 0;
    int     iMsgCache       = 0;

    /*  Copier les etiquettes de TMSG_CACHE_LABEL dans chacun
        des objets qui vont servir de cache pour ces tags la.
    */
    for(iMsgCache=0; iMsgCache< TMSG_CACHE_SIZE; iMsgCache++) {
        pLabel = cacheLabel[iCacheLabel++];
        iLabel  = 0;
        while (pLabel != ',') {
            msgCache[iMsgCache].label[iLabel++] = pLabel; 
            pLabel = cacheLabel[iCacheLabel++];
        }
        msgCache[iMsgCache].label[iLabel] = 0; 
        msgCache[iMsgCache].value[0] = 0; 
        msgCache[iMsgCache].cacheHit = (iMsgCache % 10); 

    }
}

int checkCacheLabel() {

    int     iMsgCache = 0;

    /*  Parcours de TMSG_CACHE_LABEL pour trouver un label identique
        au message recu, si c'est le cas comparer et/ou stocker la valeur
    */
    for(iMsgCache=0; iMsgCache< TMSG_CACHE_SIZE; iMsgCache++) {
        if (strcmp(message.label, msgCache[iMsgCache].label) == 0) {
            if (strcmp(message.value, msgCache[iMsgCache].value) == 0) {
                if (msgCache[iMsgCache].cacheHit++ >= 1000) {
                    // Une fois sur +/- 990-1000 on renvoi le message afin de confirmer la valeur
                    // On initialisa avec un decalage de iMsgCache modulo 10 pour eviter 
                    // que tous les renvois soient synchronisé.
                    msgCache[iMsgCache].cacheHit = (iMsgCache % 10);
                    return 1; //FOUND but SEND ANYWAY
                };
                return 0; //FOUND
            } else {
                strcpy(msgCache[iMsgCache].value, message.value);
                msgCache[iMsgCache].cacheHit = 0;
                return 1; //FOUND BUT DIFFERENT
            }
        }
    }
    return 1; //NOT FOUND
}


////////////////////////////////////////////////////////////////////////////////////////////////
//------------------------ 7 D I G I T S   C O N T R O L  --------------------------------------
void affiche(int value, int thisIndex) {

    char    segment[10];
    char    digit[2];
    int     thisSeg;

    segment[0] = B0111111;
    segment[1] = B0000110;
    segment[2] = B1011011;
    segment[3] = B1001111;
    segment[4] = B1100110;
    segment[5] = B1101101;
    segment[6] = B1111101;
    segment[7] = B0000111;
    segment[8] = B1111111;
    segment[9] = B1101111;

    if (value > 99) {value = 99;}

    digit[0] = value /10;
    digit[1] = value %10;

    thisSeg = segment[digit[thisIndex]];
    digitalWrite(PIN_DRIVE2,LOW);
    digitalWrite(PIN_DRIVE1,LOW);
    digitalWrite(PIN_LATCH,LOW);
    shiftOut(PIN_DATA, PIN_CLOCK, MSBFIRST, thisSeg); 
    digitalWrite(PIN_LATCH,HIGH);

    if (thisIndex == 0) {
        digitalWrite(PIN_DRIVE1,HIGH);
    } else {
        digitalWrite(PIN_DRIVE2,HIGH);
    }
}


/////////////////////////////////////////////////////////////////////////////////////////////////////
//------------------------ M E S S A G E   P R O C E S S I N G --------------------------------------
void messageInit() {
    messageStarted  = 0;    // La reception a démarré
    messageComplete = 0;    // La reception est terminée
    messageFailed   = 0;    // Une inconsistance est détecté
    buffer[0]       = 0;    // Buffer de reception - Vidage
    bufferIdx       = 0;    // Index du buffer
    part[0]         = 0;    // Init des partie sur l'entete du buffer
    part[1]         = 0; 
    part[2]         = 0;
    part[3]         = 0;
    partIdx         = 0;    // Index des partie utilisé
    cChecksum       = 0;    // Checksum calculé
    rChecksum       = 0;    // Checksum recu
}


int processMessage(char c) {

    c &= 0x7F;

    if (messageFailed == 0 && bufferIdx >= TMSG_SIZE_BUFFER - 1) {
        messageFailed = 1;
        return 2;
    }

    if ( (messageFailed == 1 || messageStarted == 0) && c != TINFO_SGR) {
        return 0;
    };

    switch (c)  {
        case    TINFO_STX:         // start of transmission
            debugln("\nTINFO_STX:");
            messageInit();
            break;
        case    TINFO_ETX:          // End of transmission
            debugln("\nTINFO_ETX:");
            messageInit();
            break;
        case    TINFO_SGR:            // Start of group (\n)
            debugln("\nTINFO_SGR:");
            messageInit();
            messageStarted  = 1;
            break;
        case    TINFO_EGR:            // End of group (\r)
            debug("\nTINFO_EGR");
            // réorganisation des partie du message
            switch (partIdx) {
                case 2:                         // Message sans horodatage
                    messageComplete = 1;
                    part[3] = part[2];
                    part[2] = part[1];
                    part[1] = 0;
                    break;
                case 3:                         // Message avec Horodatage
                    messageComplete = 1;
                    break;
                default:                        // Message incomplet
                    messageFailed = 1;
                    break;
            }    
            // test du checksum
            if ( messageComplete == 1) {
                rChecksum = buffer[part[3]];
                // Le checksum recu a été ajouté au checksum calculé. On le retire
                cChecksum = cChecksum - rChecksum;
                cChecksum = (cChecksum & 0x3F) + 0x20;
                if ( cChecksum != rChecksum) {
                    messageFailed = 1;
                    debug(" => FAILED "); 
                } else {
                    debug(" => OK "); 
                }
            }
            break;
        case TINFO_SEP:
            // Line = label + SEP + [timestamp + SEP] + value + SEP + checksum
            partIdx++;
            debug("\nTINFO_SEP:" + String(partIdx));     //Séparateur
            buffer[bufferIdx] = 0;
            buffer[bufferIdx + 1] = 0;
            bufferIdx++;
            switch (partIdx) {
                case 1:
                case 2:
                case 3:
                    part[partIdx] = bufferIdx;
                    break;
                default:
                    messageFailed = 1;
                    break;
            }    
            cChecksum += c;
            break;
            
        default:
            debug(c);     //valeur recue
            buffer[bufferIdx] = c;
            buffer[bufferIdx + 1] = 0;
            bufferIdx++;
            cChecksum += c;
            break;
    }

    if (messageStarted == 1 && messageComplete == 1 && messageFailed == 0) {
        if (    strlen(&buffer[part[0]]) > TMSG_SIZE_LABEL
            ||  strlen(&buffer[part[1]]) > TMSG_SIZE_TIMESTAMP
            ||  strlen(&buffer[part[2]]) > TMSG_SIZE_VALUE
        ) {
            messageInit();
            return 2;
        }                                                               // FAILED (size error)
        strcpy(message.label, &buffer[part[0]]);
        if (part[1]) {
            //strcpy(message.timestamp, &buffer[part[1]]);
            strcpy(message.value, &buffer[part[1]]);
            strcat(message.value, " ");
            strcat(message.value, &buffer[part[2]]);
        } else {
            strcpy(message.value, &buffer[part[2]]);
        }
        debugln(String(message.label) + '=' + String(message.value));
        messageInit();
        return 1;                                                       // SUCCESS
    }
    if ( messageFailed == 1) {return 2;}                                // FAILED (messageFailed)
    return 0;                                                           // CONTINUE (still reading..)
}


////////////////////////////////////////////////////////////////////////////////////////////////
//--------------------------------------  S E T U P --------------------------------------------
void setup() {

    int     i = 0;

    //PIN
    pinMode(LED_BUILTIN, OUTPUT);
        // 74HC595 Pins
    pinMode(PIN_LATCH, OUTPUT);
    pinMode(PIN_CLOCK, OUTPUT);
    pinMode(PIN_DATA, OUTPUT);
        // 7segments common anode pins
    pinMode(PIN_DRIVE1, OUTPUT);
    pinMode(PIN_DRIVE2, OUTPUT);

    digitalWrite(PIN_CLOCK,LOW);
    digitalWrite(PIN_LATCH,LOW);

    digitalWrite(LED_BUILTIN, false);                         

    // SERIAL
    Serial.begin(9600);
    delay(200);
    Serial.swap();
    delay(100);

    // FILE
#ifdef DEBUG
        SPIFFS.begin();
        SPIFFS.format();
        fileLog = SPIFFS.open("/log.txt", "w");    
        server.begin();
        server.serveStatic("/", SPIFFS, "/log.txt");
#endif

    // WIFI Connection
    WiFi.begin(ssid, password);             // Connect to the network
    debug(VERSION);
    debug(" Connecting to ");
    debug(ssid); 
    debugln(" ...");

    while (WiFi.status() != WL_CONNECTED) { // Wait (4min max) for the Wi-Fi to connect
        delay(500);
        debug(++i); 
        debug(' ');
        if (i > 500) {                      // Reboot if no wifi connection 
            ESP.reset();
        }
    }

    // Local IP Copy
    String sLocalIp = String() + WiFi.localIP()[0] + "." + WiFi.localIP()[1] + "." + WiFi.localIP()[2] + "." + WiFi.localIP()[3];
    strcpy(localIp,sLocalIp.c_str());

    debugln('\n');
    debugln("Connection established!");  
    debug("IP address:\t");
    debugln(localIp);         // Send the IP address of the ESP8266 to the computer

    //MQTT
    debugln("setting mqtt server to " + String(mqttServer));   
    mqttCnx.setServer(mqttServer, 1883);                                      //Configuration de la connexion au serveur MQTT
    mqttCnx.setCallback(mqttCallback);                                        //La fonction de callback qui est executée à chaque réception de message  
    mqttCnx.disconnect();
    mqttConnect();
    mqttSend("linky", "started", localIp);

    initCacheLabel();
    messageInit();

}

/////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////          L O O P           //////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////

void loop()
{
    int     isMsg   = 0;
    char    c;

    if (Serial.available() > 0) {
        if (Serial.available() > 250) {
            bufferOverflow++;
        }

        while (Serial.available() > 0 && isMsg == 0) {
            c = Serial.read();
            isMsg = processMessage(c);
            if (isMsg == 1) {
                debugln(">>> message complete");
                if (checkCacheLabel() == 1) {
                    cacheMiss++;
                    mqttSend("linky", message.label, message.value);
                    if (strcmp(message.label, "PCOUP") == 0) {
                        _PCOUP = atoi(message.value) * 1000;
                    }
                    if (strcmp(message.label, "SINSTS") == 0 && _PCOUP > 0) {
                        _SINSTS = atoi(message.value);
                        puissance = (_SINSTS * 100) / _PCOUP;
                        debugln("Puissance:" + String(puissance) +"%");
                    }
                } else {
                    cacheHit++;
                }
            }
            if (isMsg == 2) {
                errorCount++;
                debugln(">>> message FAILED");
            }
        }
        led = !led;                                               //toggle state
    } else {
        iLoop++;
        if (iLoop == 1000) {
            itoa(errorCount, sItoaBuffer, 10);
            mqttSend("linky", "RxError", sItoaBuffer);
        } 
        if (iLoop == 4000) {
            itoa(bufferOverflow, sItoaBuffer, 10);
            mqttSend("linky", "bufferOverflow", sItoaBuffer);
        } 
        if (iLoop == 7000) {
            if (cacheMiss >= 1000000) {
                cacheHit = cacheHit / 100;
                cacheMiss = cacheMiss / 100;
            }
            if (cacheMiss > 0) {
                cacheRatio = ((cacheHit * 100) / (cacheHit + cacheMiss));
            }
            itoa(cacheRatio, sItoaBuffer, 10);
            mqttSend("linky", "cacheHit", sItoaBuffer);
        } 
        if ((iLoop % 100) == 0) {
            led = !led;  
            mqttCnx.loop();
        } 
        if (iLoop >= 10000) {
            iLoop = 0;
        } 
        delay(10);
    }

    digitIndex = !digitIndex;
    affiche(puissance, digitIndex);

#ifdef DEBUG
        server.handleClient();
#endif

	//Wifi lost = reset
	if (WiFi.status() != WL_CONNECTED) {
		delay(5000);
		ESP.reset();
	}
    //delay(10);
    //yield();
    digitalWrite(LED_BUILTIN, led);                         // set pin to the opposite state
}
