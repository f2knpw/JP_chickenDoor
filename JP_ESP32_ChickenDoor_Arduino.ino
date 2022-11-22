
#include <WiFi.h>
//#include <HTTPClient.h>
#include <WiFiClient.h>

#include <TimeLib.h>
#include <SolarCalculator.h> //https://github.com/jpb10/SolarCalculator

//BLE
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

//Json
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson

//UDP
#include <WiFiUdp.h>

// motor driver
#include <ESP32MX1508.h> //https://github.com/ElectroMagus/ESP32MX1508
#define IN1_PIN  15
#define IN2_PIN  13
#define CH1 0                   // 16 Channels (0-15) are availible
#define CH2 1                   // Make sure each pin is a different channel and not in use by other PWM devices (servos, LED's, etc)

// Optional Parameters
#define RES 8                   // Resolution in bits:  8 (0-255),  12 (0-4095), or 16 (0-65535)     
#define FREQ  5000              // PWM Frequency in Hz    

MX1508 motorA(IN1_PIN, IN2_PIN, CH1, CH2);                      // Default-  8 bit resoluion at 2500 Hz
//MX1508 motorA(IN1_PIN,IN2_PIN, CH1, CH2, RES);                // Specify resolution
//MX1508 motorA(IN1_PIN,IN2_PIN, CH1, CH2, RES, FREQ);          // Specify resolution and frequency

//RTC DS1302
#include <Ds1302.h>

#define ENA_PIN 18
#define CLK_PIN 17
#define DAT_PIN 5
// DS1302 RTC instance
Ds1302 rtc(ENA_PIN, CLK_PIN, DAT_PIN);


const static char* WeekDays[] =
{
  "Monday",
  "Tuesday",
  "Wednesday",
  "Thursday",
  "Friday",
  "Saturday",
  "Sunday"
};



#define SW_H_PIN 23   //switch High 
#define SW_L_PIN 19   //switch Low
#define VCC_PIN 36    //ADC pin for solar panel voltage measurement



enum {sleeping, opening, closing, initializing};        // 0 : sleeping, 1 : opening, 2 : closing, 3 : initializing
int chickenStatus = sleeping;                           //sleeping is normal mode when door is opened or closed

int repeatInterval = 20;  //wake up interval when not sleeping
float margin = 10. ;      //margin (in minutes) after sunset and sunrise to close/open the door
float hourDec;            //current hour with decimal min and sec

//SUN
//PUT YOUR LATITUDE, LONGITUDE, AND TIME ZONE HERE
double latitude = 43.6;        //Toulouse
double longitude = 1.433333;

double transit, sunrise, sunset;  //transit is time at "noon" -not used-
int timeZone = 0;   //set to UTC
const int dst = 0;


String ssid = "";
String password = "";
boolean hasWifiCredentials = false;
boolean hasNtpTime = false;                 //UTC time not acquired from NTP

//these variable remain in RTC memory even in deep sleep or after software reset (https://github.com/espressif/esp-idf/issues/7718)(https://www.esp32.com/viewtopic.php?t=4931)
RTC_NOINIT_ATTR boolean hasRtcTime = false;   //UTC time not acquired from smartphone
//RTC_DATA_ATTR boolean hasRtcTime = false;   //will only survice to deepsleep reset... not software reset
RTC_NOINIT_ATTR int hours;
RTC_NOINIT_ATTR int seconds;
RTC_NOINIT_ATTR int tvsec;
RTC_NOINIT_ATTR int minutes;
RTC_NOINIT_ATTR int days;
RTC_NOINIT_ATTR int months;
RTC_NOINIT_ATTR int years;


//If you live in the northern hemisphere, it would probably be easier
//for you if you make north as the direction where the azimuth equals
//0 degrees. To do so, switch the 0 below with 180.
float northOrSouth = 180;

float pi = 3.14159265;

String device = "ChickenDoor";
String theMAC = "";
long LastBLEnotification;
long BLEconnectionTimeout = 0;

//time
#include <TimeLib.h>

//Preferences
#include <Preferences.h>
Preferences preferences;


#define PIN_LED 22

unsigned long timeout = 0;

boolean touchWake = false;
boolean resetWake = false;
touch_pad_t touchPin;
int threshold = 40;

#define uS_TO_S_FACTOR 1000000ULL  /* Conversion factor for micro seconds to seconds */
int timeToSleep = 20  ;            /* Time ESP32 will go to sleep (in seconds) */

String message = "";

//UDP --------------
unsigned int localPort = 5000;      // local port to listen on
char packetBuffer[64]; //buffer to hold incoming packet
char AndroidConnected = 0;
WiFiUDP Udp;
//end UDP-----------

//watchdog
//#include <esp_task_wdt.h> //for watchdog
//#define WDT_TIMEOUT 3   //3 seconds WDT

#include "rom/rtc.h"
void print_reset_reason(int reason) //Print last reset reason of ESP32
{
  switch ( reason)
  {
    case 1 :                                                    //Vbat power on reset
      Serial.println ("POWERON_RESET");
      resetWake = true;
      hasRtcTime = false;                                       //this is the only reset case where RTC memory persistant variables are wiped
      chickenStatus = initializing;
      break;
    case 3 : Serial.println ("SW_RESET"); break;                //Software reset digital core
    case 4 : Serial.println ("OWDT_RESET"); break;              //Legacy watch dog reset digital core
    case 5 :                                                    //Deep Sleep reset digital core
      Serial.println ("DEEPSLEEP_RESET");
      print_wakeup_reason();
      break;
    case 6 : Serial.println ("SDIO_RESET"); break;              //Reset by SLC module, reset digital core
    case 7 : Serial.println ("TG0WDT_SYS_RESET"); break;        //Timer Group0 Watch dog reset digital core
    case 8 : Serial.println ("TG1WDT_SYS_RESET"); break;        //Timer Group1 Watch dog reset digital core
    case 9 : Serial.println ("RTCWDT_SYS_RESET"); break;        //RTC Watch dog Reset digital core
    case 10 : Serial.println ("INTRUSION_RESET"); break;        //Instrusion tested to reset CPU
    case 11 : Serial.println ("TGWDT_CPU_RESET"); break;        //Time Group reset CPU
    case 12 : Serial.println ("SW_CPU_RESET"); break;           //Software reset CPU
    case 13 : Serial.println ("RTCWDT_CPU_RESET"); break;       //RTC Watch dog Reset CPU
    case 14 : Serial.println ("EXT_CPU_RESET"); break;          //for APP CPU, reseted by PRO CPU
    case 15 : Serial.println ("RTCWDT_BROWN_OUT_RESET"); break; //Reset when the vdd voltage is not stable
    case 16 : Serial.println ("RTCWDT_RTC_RESET"); break;       //RTC Watch dog reset digital core and rtc module
    default : Serial.println ("NO_MEAN");
  }
}

void print_wakeup_reason()  //deepSleep wake up reason
{
  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause();
  switch (wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD :
      Serial.println("Wakeup caused by touchpad");
      touchWake = true;
      print_wakeup_touchpad();
      break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    default : Serial.printf("Wakeup was not caused by deep sleep: %d\n", wakeup_reason); break;
  }
}

void print_wakeup_touchpad() {
  touchPin = esp_sleep_get_touchpad_wakeup_status();

  switch (touchPin)
  {
    case 0  : Serial.println("Touch detected on GPIO 4"); break;
    case 1  : Serial.println("Touch detected on GPIO 0"); break;
    case 2  : Serial.println("Touch detected on GPIO 2"); break;
    case 3  : Serial.println("Touch detected on GPIO 15"); break;
    case 4  : Serial.println("Touch detected on GPIO 13"); break;
    case 5  : Serial.println("Touch detected on GPIO 12"); break;
    case 6  : Serial.println("Touch detected on GPIO 14"); break;
    case 7  : Serial.println("Touch detected on GPIO 27"); break;
    case 8  : Serial.println("T9 detected --> opening"); chickenStatus = opening; break;
    case 9  : Serial.println("T8 detected --> closing"); chickenStatus = closing; break;
    default : Serial.println("Wakeup not by touchpad"); break;
  }
}


/* ===CODE_STARTS_HERE========================================== */


#define DEBUG_WIFI   //debug Wifi 
#define DEBUG_SLEEP
#define DEBUG_UDP    //broadcast info over UDP
#define DEBUG_PREFS  //debug preferences
#define DEBUG_VCC
//#define DEBUG


//asyncUDP
#if defined DEBUG_UDP
#include <AsyncUDP.h>
AsyncUDP broadcastUDP;
#endif

//BLE declarations
BLECharacteristic *pCharacteristic;
bool deviceConnected = false;
// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914f"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26b2"

void BLEnotify(String theString )
{
  if (deviceConnected == true)
  {
    char message[21];
    String small = "";          //BLE notification MTU is limited to 20 bytes
    while (theString.length() > 0)
    {
      small = theString.substring(0, 19); //cut into 20 chars slices
      theString = theString.substring(19);
      small.toCharArray(message, 20);
      pCharacteristic->setValue(message);
      pCharacteristic->notify();
      delay(3);             // bluetooth stack will go into congestion, if too many packets are sent
      LastBLEnotification = millis(); //will prevent to send new notification before this one is not totally sent
    }
  }
}

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
      Serial.println("client connected");
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
      Serial.println("client disconnected");
    }
};

class MyCallbacks: public BLECharacteristicCallbacks
{
    void onWrite(BLECharacteristic *pCharacteristic)
    {
      std::string rxValue = pCharacteristic->getValue();
      String test = "";
      if (rxValue.length() > 0)
      {
        Serial.print("Received : ");
        for (int i = 0; i < rxValue.length(); i++)
        {
          Serial.print(rxValue[i]);
          test = test + rxValue[i];
        }
        Serial.println();
      }
      String Res;
      int i;
      StaticJsonDocument<200> doc;
      DeserializationError error = deserializeJson(doc, test);
      // Test if parsing succeeds.
      if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.c_str());
        Serial.println("deserializeJson() failed");                             //answer with error : {"answer" : "error","detail":"decoding failed"}
      }
      else
      {
        // Fetch values --> {"Cmd":"Wifi"}
        String Cmd = doc["cmd"];
        if (Cmd == "Wifi")
        {
          const char* cpassword =  doc["Password"] ;
          const char* cssid = doc["SSID"];
          String strpassword(cpassword);
          String strssid(cssid);
          preferences.putString("password", strpassword);
          preferences.putString("ssid", strssid);
          BLEnotify("{\"status\" : \"Wifi set\"}");
#ifdef DEBUG
          Serial.println("set wifi");
#endif
          delay(1000);
          ESP.restart();
          delay(1000);
        }
        else if (Cmd == "Beat")
        {
          timeout = millis();
          BLEconnectionTimeout = millis(); //heartbeat for the bluetooth connection
        }

        else if (Cmd == "Open")           //we received position of panel
        {
          BLEnotify("{\"status\" : \"opening door\"}");
          Serial.println("opening from smartphone");
          chickenStatus = opening;
        }
        else if (Cmd == "Close")           //we received position of panel
        {
          BLEnotify("{\"status\" : \"closing door\"}");
          Serial.println("closing from smartphone");
          chickenStatus = closing;
        }
        else if (Cmd == "Margin")           //we received position of panel
        {
          margin =  doc["Value"] ;
          preferences.putFloat("margin", margin);
          BLEnotify("{\"status\" : \"margin updated\"}");
          Serial.print("margin updated ");
          Serial.println(margin);
        }

        else if (Cmd == "Time")
        {
          hours =  doc["HH"] ;
          minutes = doc["MM"];
          seconds = doc["SS"];
          days =  doc["DD"] ;
          months = doc["mm"];
          years = doc["YY"];
          timeZone = doc["TZ"];
          preferences.putInt("timeZone", timeZone);
          latitude = doc["LA"];
          longitude = doc["LO"];
          preferences.putDouble("latitude", latitude);
          preferences.putDouble("longitude", longitude);
          //setTime(now.hour(), now.minute(), now.second(), now.day(), now.month(),  now.year());
          hasRtcTime = true;
          hours = hours - timeZone;
          setTime(hours, minutes, seconds, days, months, years);
          struct timeval current_time;
          gettimeofday(&current_time, NULL);
          tvsec  = current_time.tv_sec ;  //seconds since reboot (stored into RTC memory)
          BLEnotify("{\"status\" :\"time and location set\"}");
          Serial.println("set time from smartphone:");
          display_time();
        }
      }
    }
};

void display_time(void)
{
  Serial.print(year());
  Serial.print("-");
  Serial.print(month());
  Serial.print("-");
  Serial.print(day());
  Serial.print(" at ");
  Serial.print(hour());
  Serial.print(":");
  Serial.print(minute());
  Serial.print(":");
  Serial.println(second());
}


void touchCallback() {
  //placeholder callback function
}

void setup()
{
  Serial.begin(115200);

  Serial.println(" ");
  Serial.println("*****************************************");
  Serial.print("CPU0 reset reason: ");
  print_reset_reason(rtc_get_reset_reason(0));
  Serial.println("*****************************************");

  //Preferences
  preferences.begin("eChickenDoor", false);
  //preferences.clear();              // Remove all preferences under the opened namespace
  //preferences.remove("counter");   // remove the counter key only
  preferences.putInt("timeToSleep", 20);                 // reset time to sleep to 10 minutes (usefull after night)
  timeToSleep = preferences.getInt("timeToSleep", 20);
  ssid = preferences.getString("ssid", "");         // Get the ssid  value, if the key does not exist, return a default value of ""
  password = preferences.getString("password", "");
  timeZone = preferences.getInt("timeZone", 1);
  latitude = preferences.getDouble ("latitude", 43.6);        //Toulouse
  longitude = preferences.getDouble("longitude", 1.433333);
  margin = preferences.getFloat("margin", 10);
  //preferences.end();  // Close the Preferences

#if defined DEBUG_PREFS
  Serial.println("_______prefs after boot_______");
  Serial.print("sun margin : ");
  Serial.println(margin);
  Serial.print("latitude : ");
  Serial.println(latitude);
  Serial.print("longitude : ");
  Serial.println(longitude);
  Serial.print("timeToSleep : ");
  Serial.println(timeToSleep);
  Serial.print("chickenStatus : ");
  Serial.println(chickenStatus);
  Serial.println("______________________________");
#endif

  //enable deepsleep for ESP32
  //  esp_sleep_enable_ext1_wakeup(PIR_PIN_BITMASK, ESP_EXT1_WAKEUP_ANY_HIGH); //this will be the code to enter deep sleep and wakeup with pin GPIO2 high
  esp_sleep_enable_timer_wakeup(timeToSleep * uS_TO_S_FACTOR);                 //allow timer deepsleep
  esp_sleep_enable_touchpad_wakeup();                                           //allow to wake up with touchpads

  touchAttachInterrupt(T9, touchCallback, threshold);                           //T9 open
  touchAttachInterrupt(T8, touchCallback, threshold);                           //T8 close

  pinMode(PIN_LED, OUTPUT);   // initialize digital pin 22 as an output.(LED and power on for sensors via P mosfet)
  digitalWrite(PIN_LED, LOW);

  pinMode(SW_H_PIN, INPUT_PULLUP);
  pinMode(SW_L_PIN, INPUT_PULLUP);


  //VCC voltage sensor
  //analogSetClockDiv(255);
  //analogReadResolution(12);           // Sets the sample bits and read resolution, default is 12-bit (0 - 4095), range is 9 - 12 bits
  analogSetWidth(12);                   // Sets the sample bits and read resolution, default is 12-bit (0 - 4095), range is 9 - 12 bits
  analogSetAttenuation(ADC_11db);        // Sets the input attenuation for ALL ADC inputs, default is ADC_11db, range is ADC_0db, ADC_2_5db, ADC_6db, ADC_11db
  float VCC = 0.;
  for (int i = 0; i < 100; i++)
  {
    VCC += analogRead(VCC_PIN);
  }
  VCC = VCC * 13.15 / 2505. / 100;


#ifdef DEBUG_VCC
  Serial.print("VCC : ");
  Serial.println(VCC);
#endif
  if ((VCC < 11) && (resetWake == false))
  {
    Serial.print("VCC : ");
    Serial.print(VCC);
    Serial.println("V --> XXX too low, go to sleep without motion");
    //    delay(200);
    //    GotoSleep();
  }


  //connect to WiFi
  WiFi.begin(ssid.c_str(), password.c_str());
  long start = millis();
  hasWifiCredentials = false;
  hasNtpTime = false;
  while ((WiFi.status() != WL_CONNECTED) && (millis() - start < 10000))
  {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) hasWifiCredentials = true;

  //if you get here you may be connected to the WiFi
  Serial.print("connected to Wifi: ");
  Serial.println(hasWifiCredentials);
  theMAC = WiFi.macAddress();
  theMAC.replace(":", "");

  // init interfaces
  SetIF();

  if (hasWifiCredentials)
  {
    //init and get the time
    Serial.println("trying to get time 1");
    configTime(timeZone * 3600, dst * 0, "pool.ntp.org");
    printLocalTime();

    //init and get the time
    Serial.println("trying to get time 2");   //call it twice to have a well synchronized time on soft reset... Why ? bex=caus eit works...
    delay(2000);
    configTime(timeZone * 3600, dst * 0, "pool.ntp.org");
    printLocalTime();

    //disconnect WiFi as it's no longer needed
    //  WiFi.disconnect(true);
    //  WiFi.mode(WIFI_OFF);


    // initialize the RTC
    rtc.init();

    // test if clock is halted and set a date-time (see example 2) to start it
    if (rtc.isHalted())
    {
      Serial.println("RTC is halted...");
      hasRtcTime = false;
    }
    else
    {
      hasRtcTime = true;
      // get the current time
      Ds1302::DateTime now;
      rtc.getDateTime(&now);
      years = now.year + 2000;
      months = now.month;
      days = now.day;
      hours = now.hour;
      minutes = now.minute ;
      seconds = now.second;
      setTime(hours, minutes, seconds, days, months, years); //set ESP32 time manually
      Serial.print("time after DS1302 RTC : ");
      display_time();
      struct timeval current_time;       //get ESP32 RTC time and save it
      gettimeofday(&current_time, NULL);
      tvsec  = current_time.tv_sec ;      //seconds since reboot (now stored into RTC RAM
      hasRtcTime = true;                  //now ESP32 RTC time is also initialized
    }



    if (hasNtpTime)   //set the time with NTP info
    {
      time_t now;
      struct tm * timeinfo;
      time(&now);
      timeinfo = localtime(&now);

      years = timeinfo->tm_year + 1900;   //https://mikaelpatel.github.io/Arduino-RTC/d8/d5a/structtm.html
      months = timeinfo->tm_mon + 1;
      days = timeinfo->tm_mday;
      hours = timeinfo->tm_hour - timeZone;
      minutes = timeinfo->tm_min;
      seconds = timeinfo->tm_sec;

      //set ESP32 time manually (hr, min, sec, day, mo, yr)
      setTime(hours, minutes, seconds, days, months, years);
      Serial.print("time after ntp: ");
      display_time();

      struct timeval current_time;        //get ESP32 RTC time and save it
      gettimeofday(&current_time, NULL);
      tvsec  = current_time.tv_sec ;      //seconds since reboot
      hasRtcTime = true;                  //now ESP32 RTC time is also initialized

      //set DS1302 RTC time
      Ds1302::DateTime dt = {
        .year = year() % 2000,
        .month = month(),
        .day = day(),
        .hour = hour(),
        .minute = minute(),
        .second = second(),
        .dow = weekday() - 1 // Day of the week (1-7), Sunday is day 1
      };
      rtc.setDateTime(&dt);
    }
  }


  //BLE
  // Create the BLE Device
  BLEDevice::init("JP ChickenDoor");

  // Create the BLE Server
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Create a BLE Characteristic
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ   |
                      BLECharacteristic::PROPERTY_WRITE  |
                      BLECharacteristic::PROPERTY_NOTIFY |
                      BLECharacteristic::PROPERTY_INDICATE
                    );

  // https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.client_characteristic_configuration.xml
  pCharacteristic->addDescriptor(new BLE2902());      // Create a BLE Descriptor
  pCharacteristic->setCallbacks(new MyCallbacks());
  pService->start();                                  // Start the BLE service
  pServer->getAdvertising()->start();                 // Start advertising
  Serial.println("Waiting a BLE client connection to notify...");
  delay(100);
  //vTaskDelay(10);
  //end BLE

  if ((!hasNtpTime) && (!hasRtcTime))
  {
    Serial.println("no time acquired to compute sun position...");
  }
  else  //ready to compute sun angles
  {
    if (!hasNtpTime)
    {
      Serial.println("use time from RTC :");

      struct timeval current_time;
      gettimeofday(&current_time, NULL);
      // Serial.printf("seconds : %ld\nmicro seconds : %ld", current_time.tv_sec, current_time.tv_usec);
      Serial.printf("seconds stored : %ld\nnow seconds : %ld\n", tvsec, current_time.tv_sec);

      int sec  = seconds - tvsec + current_time.tv_sec ;
      sec = hours * 3600 + minutes * 60 + sec;
      int ss = sec % 60;
      sec = sec / 60;
      int mm = sec % 60;
      sec = sec / 60;
      int hh = sec % 24;
      int dd = days + sec / 24;
      //set time manually (hr, min, sec, day, mo, yr)
      setTime(hh, mm, ss, dd, months, years);
      display_time();
    }

    // Calculate the sunset and sunrise
    calcSunriseSunset(year(), month(), day(), latitude, longitude, transit, sunrise, sunset);
    char str[6];
    Serial.print("Sunrise : ");
    Serial.println(sunrise);
    Serial.print("Sunset : ");
    Serial.println(sunset);
    sunrise += margin / 60;
    sunset += margin / 60;
    Serial.print("Sunrise + margin : ");
    Serial.println(sunrise);
    Serial.print("Sunset + margin : ");
    Serial.println(sunset);
    //Serial.println(hoursToString(sunset , str));

    //prepare chicken door motion

    hourDec = hour() + float(minute()) / 60. + float(second()) / 3600.;
    Serial.print("current hour : ");
    Serial.println(hourDec);
    switch (chickenStatus)
    {
      case initializing:
      case sleeping:
        if ((hourDec < sunrise) || (hourDec > sunset))
        {
          if (digitalRead(SW_L_PIN) == 1) {
            chickenStatus = closing;
            Serial.println ("closing door ");
          }
        }
        else
        {
          if (digitalRead(SW_H_PIN) == 1) {
            chickenStatus = opening;
            Serial.println ("opening door ");
          }
        }
        break;
      default:
        // statements
        break;
    }
  }
  timeout = millis();     //arm software watchdog
  BLEconnectionTimeout = millis();                                //will allow smartphone to connect over bluetooth





  //watchdog
  // Serial.println("Configuring WDT...");
  //  esp_task_wdt_init(WDT_TIMEOUT, true); //enable panic so ESP32 restarts
  //  esp_task_wdt_add(NULL); //add current thread to WDT watch
  //
  //  Serial.println("Resetting WDT...");
  //      esp_task_wdt_reset();             //call it periodically into the main loop

}

void printDigits(int digits)
{
  if (digits < 10)
    Serial.print('0');
  Serial.print(digits);
}

time_t toUtc(time_t local)
{
  return local - timeZone * 3600;
}

time_t toLocal(time_t utc)
{
  return utc + timeZone * 3600;
}

// Rounded HH:mm format
char * hoursToString(double h, char *str)
{
  int m = int(round(h * 60));
  int hr = (m / 60) % 24;
  int mn = m % 60;

  str[0] = (hr / 10) % 10 + '0';
  str[1] = (hr % 10) + '0';
  str[2] = ':';
  str[3] = (mn / 10) % 10 + '0';
  str[4] = (mn % 10) + '0';
  str[5] = '\0';
  return str;
}

void SetIF(void)
{
  //Start UDP
  //Udp.begin(localPort);
}

void printLocalTime() //check if ntp time is acquired nd print it
{
  struct tm timeinfo;
  hasNtpTime = true;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    hasNtpTime = false;
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S"); //https://www.ibm.com/docs/en/workload-automation/9.5.0?topic=troubleshooting-date-time-format-reference-strftime
}



void loop()
{
  switch (chickenStatus)
  {
    case opening:
      if (digitalRead(SW_H_PIN) == 1)
      {
        motorA.motorGo(255);            // Pass the speed to the motor: 0-255 for 8 bit resolution;
        timeout = millis();
      }
      else
      {
        motorA.motorStop();             // Soft Stop    -no argument
        chickenStatus = sleeping;
      }
      break;
    case closing:
      if (digitalRead(SW_L_PIN) == 1)
      {
        motorA.motorRev(255);            // Pass the speed to the motor: 0-255 for 8 bit resolution;
        timeout = millis();
      }
      else
      {
        motorA.motorStop();             // Soft Stop    -no argument
        chickenStatus = sleeping;
      }
      break;
    case sleeping:
      hourDec = hour() + float(minute()) / 60. + float(second()) / 3600.;
      float timeToSleepDec;
      if (digitalRead(SW_L_PIN) == 0) //door closed
      {
        if (hourDec > sunrise) timeToSleepDec = 24 + sunrise - hourDec;
        else timeToSleepDec = sunrise - hourDec;
      }
      else                            //door opened
      {
        if (hourDec > sunset) timeToSleepDec = 24 + sunset - hourDec;
        else timeToSleepDec = sunset - hourDec;
      }
      Serial.print( "will sleep (hour) : ");
      Serial.println( timeToSleepDec);
      timeToSleep = 3600 * timeToSleepDec;
      Serial.print( "will sleep (seconds) : ");
      Serial.println( timeToSleep);
      esp_sleep_enable_timer_wakeup(timeToSleep * uS_TO_S_FACTOR);                 //allow timer deepsleep
      writePrefs();
      GotoSleep();
      break;
    default:
      // statements
      break;
  }

  if ((millis() - timeout) > 10000)  //log every 5s
  {
    timeout = millis();
    chickenStatus = sleeping;
    writePrefs();
    Serial.print("timeout : ");
    GotoSleep();
  }


#if defined DEBUG_UDP
  //fbroadcastUDP("sun El " + String(sunElevation) + ", Current El " + String(currentEl)  + ", panel El " + String(90 - sunElevation) + ", Az " + String(sunAzimuth)  + ", Current Az " + String(currentAz));
#endif
}



void writePrefs(void)
{
  preferences.putInt("chickenStatus", chickenStatus);
}

void GotoSleep()
{
#if defined DEBUG_SLEEP
  Serial.println("Entering DeepSleep");
  delay(100);
#endif
  esp_deep_sleep_start();       //enter deep sleep mode
  delay(1000);
  abort();
}


#if defined DEBUG_UDP
void fbroadcastUDP(String Res)
{
  // Send UDP Broadcast to 255.255.255.255 (default broadcast addr), Port 5000
  broadcastUDP.broadcastTo(Res.c_str(), 5000);
}
#endif
