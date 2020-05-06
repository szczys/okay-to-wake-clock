/*
    Okay to wake clock
    Copyright 2020, Mike Szczys

    Uses OTA update

    Features:

    * Turns on red LEDs at 7pm
    * Changes to yellow LEDs at 5:30am
    * Changes to green LEDs at 6:30am
    * Turns of LEDs at 7:30am
    * Time is set via NTP at power-up
    * OTA updates are available for 10 minutes after powerup at which point WiFi is shut off for power savings
*/

/* User timer settings: { hours, minutes } */
int SLEEP_TIME[2] = { 19, 00 };
int DOZE_TIME[2] = { 5, 30 };
int WAKE_TIME[2] = { 6, 30 };
int DAY_TIME[2] = { 7, 30 };

//How many minutes to leave WiFi on after power-up for purposes of over-the-air-updates
int MINUTES_BEFORE_WIFI_SHUTOFF = 10;


/*
 * Library Dependencies:
 * Timezone by Jack Christensen
 * Time (install as a dependency of Timezone)
 * Adafruit NeoPixel
 */
 
/* Includes */
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <Timezone.h>
#include <Adafruit_NeoPixel.h>
#include <ArduinoOTA.h>
#include "credentials.h"

#define LED_PIN    12
#define LED_COUNT 3

/* State machine definitions */
#define DAY   0
#define SLEEP 1
#define DOZE  2
#define WAKE  3

const uint32_t state_colors[4] = {
  ((uint32_t)0x00 << 16) | ((uint32_t)0x00 <<  8) | 0x00,
  ((uint32_t)0xFF << 16) | ((uint32_t)0x00 <<  8) | 0x00,
  ((uint32_t)0xFF << 16) | ((uint32_t)0xFF <<  8) | 0x00,
  ((uint32_t)0x00 << 16) | ((uint32_t)0xFF <<  8) | 0x00
};

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

/* Set these WiFi details in the credentials.h file so they don't get included in the git repo */
const char* ssid     = STASSID;
const char* password = STAPSK;

const char* host = "https://hackaday.com";
const uint16_t port = 80;

//UDP stuff for NTP internet time lookup
//https://www.geekstips.com/arduino-time-sync-ntp-server-esp8266-udp/
unsigned int localPort = 2390;      // local port to listen for UDP packets
IPAddress timeServerIP; // time.nist.gov NTP server address
const char* ntpServerName = "2.north-america.pool.ntp.org";
const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets
// A UDP instance to let us send and receive packets over UDP
WiFiUDP udp;

//Timezone stuff for central time
//https://github.com/JChristensen/Timezone/blob/master/examples/Clock/Clock.ino
// US Eastern Time Zone (New York, Detroit)
TimeChangeRule myDST = {"CDT", Second, Sun, Mar, 2, -300};    // Daylight time = UTC - 5 hours
TimeChangeRule mySTD = {"DST", First, Sun, Nov, 2, -360};     // Standard time = UTC - 6 hours
Timezone myTZ(myDST, mySTD);
TimeChangeRule *tcr;        // pointer to the time change rule, use to get TZ abbrev

void setup() {
  Serial.begin(115200);
  
  strip.begin();           // INITIALIZE NeoPixel strip object (REQUIRED)
  for (uint8_t i=0; i<LED_COUNT; i++) strip.setPixelColor(i,0);
  strip.show();            // Turn OFF all pixels ASAP
  strip.setBrightness(50); // Set BRIGHTNESS to about 1/5 (max = 255)

  // We start by connecting to a WiFi network

  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  /* Explicitly set the ESP8266 to be a WiFi-client, otherwise, it by default,
     would try to act as both a client and an access-point and could cause
     network-issues with your other WiFi-devices on your WiFi-network. */
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  Serial.println("Starting UDP");
  udp.begin(localPort);
  Serial.print("Local port: ");
  Serial.println(udp.localPort());

  setTime(myTZ.toUTC(compileTime()));

  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
}

void loop() {
  uint8_t state = DAY;
  change_lights(state);

  unsigned long myUTC;
  while (1) {
    myUTC = getUTC();
    if (myUTC == 0) {
      delay(2000);
      Serial.println("Retrying...");
    }
    else break;
  }
  setTime(myUTC);

  char wifi_state = 1;
  
  while(1) {
    if (wifi_state) {
      ArduinoOTA.handle();
      if (millis() > MINUTES_BEFORE_WIFI_SHUTOFF*60*1000) {
        //Shutoff WiFi after X minutes. Leaves a window for OTA update after power cycling
        WiFi.forceSleepBegin();
        wifi_state = 0;
      }
    }

    time_t utc = now();
    time_t local = myTZ.toLocal(utc, &tcr);
    Serial.println();
    printDateTime(utc, "UTC");
    printDateTime(local, tcr -> abbrev);

    int rightNow[] = { hour(local), minute(local) };
    int rn = big_time(rightNow);
    Serial.print("rightNow = ");
    Serial.println(rn);
    

    //Day = <Sleep and >Day
    //FIXME: This assumes SLEEP will start at night and not in the early morning (eg: 00:12 for 12:12am would trip this up)
    if ((rn >= big_time(DAY_TIME)) && (rn < big_time(SLEEP_TIME))) {
      if (state != DAY) {
        state = DAY;
        change_lights(state);
      }
    }
    //Doze = <Wake and >=Doze
    else if ((rn >= big_time(DOZE_TIME)) && (rn < big_time(WAKE_TIME))) {
      if (state != DOZE) {
        state = DOZE;
        change_lights(state);
      }
    }
    
    //Wake = <Day and >=Wake
    else if ((rn >= big_time(WAKE_TIME)) && (rn < big_time(DAY_TIME))) {
      if (state != WAKE) {
        state = WAKE;
        change_lights(state);
      }
    }
    //Sleep = <Doze and >=Sleep
    else if ((rn >= big_time(SLEEP_TIME)) || (rn < big_time(DOZE_TIME))) {
      if (state != SLEEP) {
        state = SLEEP;
        change_lights(state);
      }
    }

    Serial.print("state = ");
    Serial.println(state);
    // wait ten seconds before asking for the time again
    delay(10000);
  }
}

int big_time(int hoursmins[2]) {
  return (hoursmins[0]*60) + hoursmins[1];
}

void change_lights(char state) {
  for (uint8_t i=0; i<LED_COUNT; i++) strip.setPixelColor(i,state_colors[state]);
  strip.show();
}

unsigned long getUTC(void) {
  //get a random server from the pool
  WiFi.hostByName(ntpServerName, timeServerIP); 

  sendNTPpacket(timeServerIP); // send an NTP packet to a time server
  // wait to see if a reply is available
  delay(1000);
  
  int cb = udp.parsePacket();
  if (!cb) {
    Serial.println("no packet yet");
    return 0;
  }
  else {
    Serial.print("packet received, length=");
    Serial.println(cb);
    // We've received a packet, read the data from it
    udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

    //the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, esxtract the two words:

    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;
    Serial.print("Seconds since Jan 1 1900 = " );
    Serial.println(secsSince1900);

    // now convert NTP time into everyday time:
    Serial.print("Unix time = ");
    // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
    const unsigned long seventyYears = 2208988800UL;
    // subtract seventy years:
    unsigned long epoch = secsSince1900 - seventyYears;
    // print Unix time:
    Serial.println(epoch);


    // print the hour, minute and second:
    Serial.print("The UTC time is ");       // UTC is the time at Greenwich Meridian (GMT)
    Serial.print((epoch  % 86400L) / 3600); // print the hour (86400 equals secs per day)
    Serial.print(':');
    if ( ((epoch % 3600) / 60) < 10 ) {
      // In the first 10 minutes of each hour, we'll want a leading '0'
      Serial.print('0');
    }
    Serial.print((epoch  % 3600) / 60); // print the minute (3600 equals secs per minute)
    Serial.print(':');
    if ( (epoch % 60) < 10 ) {
      // In the first 10 seconds of each minute, we'll want a leading '0'
      Serial.print('0');
    }
    Serial.println(epoch % 60); // print the second
    return epoch;
  }
}

// send an NTP request to the time server at the given address
unsigned long sendNTPpacket(IPAddress& address)
{
  Serial.println("sending NTP packet...");
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, 123); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
}


// Function to return the compile date and time as a time_t value
time_t compileTime()
{
    const time_t FUDGE(10);     // fudge factor to allow for compile time (seconds, YMMV)
    const char *compDate = __DATE__, *compTime = __TIME__, *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char chMon[4], *m;
    tmElements_t tm;

    strncpy(chMon, compDate, 3);
    chMon[3] = '\0';
    m = strstr(months, chMon);
    tm.Month = ((m - months) / 3 + 1);

    tm.Day = atoi(compDate + 4);
    tm.Year = atoi(compDate + 7) - 1970;
    tm.Hour = atoi(compTime);
    tm.Minute = atoi(compTime + 3);
    tm.Second = atoi(compTime + 6);
    time_t t = makeTime(tm);
    return t + FUDGE;           // add fudge factor to allow for compile time
}

// format and print a time_t value, with a time zone appended.
void printDateTime(time_t t, const char *tz)
{
    char buf[32];
    char m[4];    // temporary storage for month string (DateStrings.cpp uses shared buffer)
    strcpy(m, monthShortStr(month(t)));
    sprintf(buf, "%.2d:%.2d:%.2d %s %.2d %s %d %s",
        hour(t), minute(t), second(t), dayShortStr(weekday(t)), day(t), m, year(t), tz);
    Serial.println(buf);
}
