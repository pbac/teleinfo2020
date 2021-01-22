#include <Arduino.h>
#include <ESP8266WiFi.h>          // ESP8266 Core WiFi Library 
#include <PubSubClient.h>

//#include "main.h"
#include "secret.h"
#include "mqtt54.h"
#include "teleinfo2020.h"

#ifdef DEBUG
#include <FS.h>                   //this needs to be first, or it all crashes and burns...
#include <ESP8266WebServer.h>     // Local WebServer 
#endif

#define		DEVICE_TYPE			"node"
#define		DEVICE_ID			"132"
#define		VERSION				"2.1.5"

#define		BUILTIN_LED			16

#define		TINFO_STX 			0x02
#define		TINFO_ETX 			0x03 
#define		TINFO_SGR 			0x0A //'\n' // start of group  
#define		TINFO_EGR 			0x0D //'\r' // End of group    
#define		TINFO_SEP 			0x09          // Separator (tab)

#define		PIN_LATCH  			12
#define		PIN_CLOCK  			4
#define		PIN_DATA   			14
#define		PIN_DRIVE1 			5
#define		PIN_DRIVE2 			0

#define		TMSG_SIZE_BUFFER	256
#define		TMSG_SIZE_LABEL		10
#define		TMSG_SIZE_TIMESTAMP	14
#define		TMSG_SIZE_VALUE		150

#define		TMSG_CACHE_SIZE 	33
#define		TMSG_CACHE_LABEL 	"ADSC,EASD01,EASD02,EASD03,EASD04,EASF01,EASF02,EASF03,EASF04,EASF05,EASF06,EASF07,EASF08,EASF09,EASF10,IRMS1,LTARF,MSG1,NGTF,NJOURF,NTARF,PCOUP,PREF,PRM,RELAIS,STGE,URMS1,VTIC,CCASN,CCASN-1,SMAXSN,SMAXSN-1,UMOY1,"   

class TMsg
{
  public:
	char	label[TMSG_SIZE_LABEL + 1];
	char	value[TMSG_SIZE_TIMESTAMP + TMSG_SIZE_VALUE + 1];
	short	cacheHit;
};

//-------------------------------------------------------------------

TMsg			message;
TMsg			msgCache[TMSG_CACHE_SIZE];

WiFiClient		wifiCnx;
Mqtt54			mqttCnx(wifiCnx, SECRET_MQTT_SERVERNAME, SECRET_MQTT_PORT, SECRET_MQTT_USER, SECRET_MQTT_PASSWORD);
#ifdef DEBUG
ESP8266WebServer server (80);
File            fileLog;
#endif

//-------------------------------------------------------------------

const char*     cacheLabel  = TMSG_CACHE_LABEL;

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

//_______________________________________________________________________________________________________
//_______________________________________________________________________________________________________

void flashLed () {
	static bool	builtIn_led = 0;		// For flashing the ESP led

	builtIn_led = !builtIn_led;
	digitalWrite(BUILTIN_LED, builtIn_led);
}
void flashLed (bool	builtIn_led) {
	digitalWrite(BUILTIN_LED, builtIn_led);
}

//------------------------ 7 D I G I T S   C O N T R O L  --------------------------------------
void affiche(int value, int thisIndex) {
	char	segment[10];
	byte	digit[2];
	byte	thisSeg;

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

//___________________________________  W I F I   C O N N E C T I O N  ___________________________________
//_______________________________________________________________________________________________________

int wifiConnection(const char * Ssid, const char * Password, const char * Hostname) {
	int		cnxWait = 0;

	flashLed(1);
	affiche(2, 1);

	WiFi.mode(WIFI_STA);											// Set atation mode only (not AP)
	delay(150);
	//WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);			// Reset all address
	delay(150);
	WiFi.hostname(Hostname);										// Set the hostname (for dhcp server)
	delay(150);
	WiFi.begin(Ssid, Password);										// Connect to the network
	debug(String() + Hostname + " connecting to " + Ssid + "... "); 

	while (WiFi.status() != WL_CONNECTED) {						// Wait (4min max) for the Wi-Fi to connect
		affiche(3, 1);
		delay(250);
		affiche(cnxWait, 0);
		delay(250);
		debug(String(++cnxWait) + "."); 
		if (cnxWait > 500) {ESP.restart();}							// Reboot if no wifi connection 
	}
	flashLed(0);
	debugln();
	debugln(String() + "IP address  :\t" + WiFi.localIP()[0] + "." + WiFi.localIP()[1] + "." + WiFi.localIP()[2] + "." + WiFi.localIP()[3]);
	affiche(4, 1);
	return cnxWait;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
//------------------------ C A C H E    M A N A G E M E N T --------------------------------------
void initCacheLabel() {
	char    pLabel;
	int     iCacheLabel     = 0;
	int     iLabel          = 0;
	byte    iMsgCache       = 0;

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
	byte     iMsgCache = 0;

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


//_____________________________________________________________________________________//
//_____________________________          S E T U P         ____________________________//
void setup() {

	//PIN
	pinMode(BUILTIN_LED, OUTPUT);
		// 74HC595 Pins
	pinMode(PIN_LATCH, OUTPUT);
	pinMode(PIN_CLOCK, OUTPUT);
	pinMode(PIN_DATA, OUTPUT);
		// 7segments common anode pins
	pinMode(PIN_DRIVE1, OUTPUT);
	pinMode(PIN_DRIVE2, OUTPUT);

	digitalWrite(PIN_CLOCK,LOW);
	digitalWrite(PIN_LATCH,LOW);

	digitalWrite(BUILTIN_LED, false);                         


	// SERIAL
	Serial.begin(9600);
	affiche(0, 1);
	delay(500);
	Serial.swap();
	delay(200);

	// FILE
#ifdef DEBUG
		SPIFFS.begin();
		SPIFFS.format();
		fileLog = SPIFFS.open("/log.txt", "w");    
		server.begin();
		server.serveStatic("/", SPIFFS, "/log.txt");
#endif
	debugln("Starting " DEVICE_TYPE " " DEVICE_ID " v" VERSION);
	affiche(1, 1);

	wifiConnection(SECRET_WIFI_SSID, SECRET_WIFI_PASSWORD, DEVICE_TYPE DEVICE_ID);

	affiche(5, 1);
	mqttCnx.setDevice(DEVICE_TYPE, DEVICE_ID);
	mqttCnx.setTime(SECRET_NTP_SERVERNAME, SECRET_NTP_TIMEZONE);
	mqttCnx.start(WiFi.localIP(), WiFi.macAddress());
	mqttCnx.send("device", "version",  "node", VERSION);	

	affiche(6, 1);

	initCacheLabel();
	messageInit();
	affiche(7, 1);

}

//_____________________________________________________________________________________//
//_____________________________          L O O P           ____________________________//

void loop()
{
	static int		isMsg   = 0;
	static char		c;
	// loop
	static int		iLoop;
	static int		errorCount			= 0;
	static int		bufferOverflow		= 0;
	// Cache Hit ratio
	static int		cacheRatio			= 0;
	static int		cacheHit			= 0;
	static int		cacheMiss			= 0;
	static char		sItoaBuffer[25];

	isMsg   = 0;
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
					mqttCnx.send("data", "linky", message.label, message.value);
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
		flashLed();
	} else {
		iLoop++;
		if (iLoop == 1000) {
			itoa(errorCount, sItoaBuffer, 10);
			mqttCnx.send("control", "linky", "RxError", sItoaBuffer);
		} 
		if (iLoop == 4000) {
			itoa(bufferOverflow, sItoaBuffer, 10);
			mqttCnx.send("control", "linky", "bufferOverflow", sItoaBuffer);
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
			mqttCnx.send("control", "linky", "cacheHit", sItoaBuffer);
		} 
		if ((iLoop % 1000) == 0) {
			flashLed();
			mqttCnx.loop();
		} 
		if (iLoop >= 10000) {
			iLoop = 0;
		} 
		delay(5);
	}

	digitIndex = !digitIndex;
	affiche(puissance, digitIndex);

#ifdef DEBUG
		server.handleClient();
#endif

	if (WiFi.status() != WL_CONNECTED) {
		//Serial.println("Wifi connexion lost : Rebooting ESP");
		affiche(9, 1);
		delay(5000);
		ESP.restart();
	}
}
