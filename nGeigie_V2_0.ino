/*
  nGeigie.ino

2015-04-05 V2.4.9  delay for switching off LEDs
2015-04-07 V2.6.0 merged code with 3G
2015-04-07 V2.6.1 beeper setup and code cleaning　(need jumper from D10 in arduino shield (is pin D27)to A3)
2015-04-08 V2.6.3 setup for measurung voltage on A13
2015-04-08 V2.6.4 made switch for sending to dev or API
2015-04-13 V2.6.5 added switch for switching from Ethernet and other options to switch in the coding without recompile.

contact rob@yr-design.biz
 */
 
 /**************************************************************************/
// Init
/**************************************************************************/
 
#include <SPI.h>         // needed for Arduino versions later than 0018
#include <Ethernet.h>
#include <EEPROM.h>
#include <limits.h>
#include <SoftwareSerial.h>
#include <Time.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdio.h>
#include <math.h>
#include <i2c_t3.h>
#include <LiquidCrystal_I2C.h>
#include "a3gim.h"
#include "nGeigie3GSetup.h"
#include "nGeigie3GDebug.h"

//setup LCD I2C
#define I2C_ADDR    0x27  // Define I2C Address where the PCF8574A is for LCD2004 form http://www.sainsmart.com
#define BACKLIGHT_PIN     3
#define En_pin  2
#define Rw_pin  1
#define Rs_pin  0
#define D4_pin  4
#define D5_pin  5
#define D6_pin  6
#define D7_pin  7

int n = 1;
LiquidCrystal_I2C	lcd(I2C_ADDR,En_pin,Rw_pin,Rs_pin,D4_pin,D5_pin,D6_pin,D7_pin);
int backlightPin = 2;

#define ENABLE_DEBUG 
#define LINE_SZ 128
// SENT_SZ is used for sending data for 3G
#define SENT_SZ 120
//OLINE_SZ is used for OpenLog buffers
#define OLINE_SZ 250
//GATEWAY_sz is array for gateways
#define GATEWAY_SZ 2

static char obuf[OLINE_SZ];
static char buf[LINE_SZ];
static char buf2[LINE_SZ];
static char lat_buf[16];
static char lon_buf[16];


// OpenLog Settings --------------------------------------------------------------
SoftwareSerial OpenLog =  SoftwareSerial(0, 1);
static const int resetOpenLog = 3;
#define OPENLOG_RETRY 500
bool openlog_ready = false;
char logfile_name[13];  // placeholder for filename
bool logfile_ready = false;

//static void setupOpenLog();
static bool loadConfig(char *fileName);
//static void createFile(char *fileName);

static ConfigType config;
nGeigieSetup ngeigieSetup(OpenLog, config, obuf, OLINE_SZ);

//static
static char VERSION[] = "V2.6.5";
static char path[LINE_SZ];
static char path2[LINE_SZ];
char datedisplay[8];
char coordinate[16];
static char json_buf[SENT_SZ];
static char json_buf2[SENT_SZ];
typedef struct
{
    unsigned char state;
    unsigned char conn_fail_cnt;
} devctrl_t;
static devctrl_t ctrl;

//const
const char *server = "107.161.164.163";
const int port = 80;
const int interruptMode = FALLING;
const int updateIntervalInMinutes = 1;

byte macAddress[] = { 0x90, 0xA2, 0xDA, 0x0E, 0xE0, 0x5C };
EthernetClient client;
IPAddress localIP (192, 168, 100, 40);	
IPAddress serverIP(107, 161, 164, 163 ); 
int resetPin = A1;   //
int ethernet_powerdonwPin = 7;


//int
int MAX_FAILED_CONNS = 3;
int len;
int len2;
int conn_fail_cnt;
int NORMAL = 0;
int RESET = 1;

//long
unsigned long elapsedTime(unsigned long startTime);

//char
char timestamp[19];
char lat[8];
char lon[9];
char lat_lon_nmea[25];
unsigned char state;
char res[a3gsMAX_RESULT_LENGTH+1];


//WDT setup init

#define RCM_SRS0_WAKEUP                     0x01
#define RCM_SRS0_LVD                        0x02
#define RCM_SRS0_LOC                        0x04
#define RCM_SRS0_LOL                        0x08
#define RCM_SRS0_WDOG                       0x20
#define RCM_SRS0_PIN                        0x40
#define RCM_SRS0_POR                        0x80
#define RCM_SRS1_LOCKUP                     0x02
#define RCM_SRS1_SW                         0x04
#define RCM_SRS1_MDM_AP                     0x08
#define RCM_SRS1_SACKERR                    0x20

//WDT timer
IntervalTimer wdTimer;

//reset marco
#define CPU_RESTART_ADDR (uint32_t *)0xE000ED0C
#define CPU_RESTART_VAL 0x5FA0004
#define CPU_RESTART (*CPU_RESTART_ADDR = CPU_RESTART_VAL);

void onReset()
{
 CPU_RESTART;
}


// generate checksums for log format
byte len1, chk;
byte len3, chk2;
char checksum(char *s, int N)
{
    int i = 0;
    char chk = s[0];

    for (i=1; i < N; i++)
        chk ^= s[i];

    return chk;
}

char checksum2(char *s, int N)
{
    int i = 0;
    char chk2 = s[0];

    for (i=1; i < N; i++)
        chk2 ^= s[i];

    return chk2;
}

// Sampling interval (e.g. 60,000ms = 1min)
unsigned long updateIntervalInMillis = 0;

// The next time to feed
unsigned long nextExecuteMillis = 0;

// Event flag signals when a geiger event has occurred
volatile unsigned char eventFlag = 0;       // FIXME: Can we get rid of eventFlag and use counts>0?
unsigned long int counts_per_sample;
unsigned long int counts_per_sample2;

// The last connection time to disconnect from the server
// after uploaded feeds
long lastConnectionTime = 0;

// The conversion coefficient from cpm to µSv/h
float conversionCoefficient = 0;
float conversionCoefficient2 = 0;



void onPulse()
{
    counts_per_sample++; 

}
void onPulse2()
{
    counts_per_sample2++;

}

/**************************************************************************/
// Setup
/**************************************************************************/

void setup() {  
  
     // Load EEPROM settings
         ngeigieSetup.initialize();
                
                
    //print last reset message and setup the patting of the dog
         delay(100);
         printResetType();
   //start WDT	
         wdTimer.begin(KickDog, 10000000); // patt the dog every 10sec  
    
   //beep 
       tone(28, 600, 200);
    
    //button reset make jumper on upper from A3 to D8 (27)
          pinMode(27, INPUT_PULLUP);
          attachInterrupt(27, onReset, interruptMode);
       
    // openlog setup 
          Serial.begin(9600);
          OpenLog.begin(9600);
  
    // set brightnes
          lcd.setBacklightPin(BACKLIGHT_PIN,POSITIVE);
          lcd.setBacklight(125);
          
    //set up the LCD's number of columns and rows: 
          lcd.begin(20, 4);
             
            
if (strcmp(config.intf, "EN") == 0) {
    // Print a message to the LCD.
	   lcd.clear();
	   lcd.print(F("nGeigie Ethernet"));
           Serial.print("Interface = Ethernet");
	   //delay(3000);
           lcd.setCursor(0, 1);
           lcd.print(VERSION);
      }
      
 if (strcmp(config.intf, "3G") == 0) {
    // Print a message to the LCD.
	   lcd.clear();
	   lcd.print(F("nGeigie 3G"));
           Serial.print("Interface = 3G");
	   //delay(3000);
           lcd.setCursor(0, 1);
           lcd.print(VERSION);
      }
  
  if (strcmp(config.intf, "BL") == 0) {
    // Print a message to the LCD.
	   lcd.clear();
	   lcd.print(F("nGeigie BLE"));
           Serial.print("Interface = BLE");
	   //delay(3000);
           lcd.setCursor(0, 1);
           lcd.print(VERSION);
      }
 
    //LED1(green) setup
      pinMode(31, OUTPUT);
      digitalWrite(31, HIGH);
      
   //LED2(red) setup
     pinMode(26, OUTPUT);
     digitalWrite(26, HIGH);
     
    // LED on delay
      delay (3000); 
       
    //LED off
      digitalWrite(26, LOW);
      digitalWrite(31, LOW); 
 
     
    //Openlog setup
        OpenLog.begin(9600);
        setupOpenLog();
          if (openlog_ready) {
              lcd.clear();
              lcd.setCursor(0, 0);
              lcd.print ("loading setup");
              Serial.println();
              Serial.println("loading setup");
              ngeigieSetup.loadFromFile("NGEIGIE.TXT");
              lcd.setCursor(0, 1);
              lcd.print ("network setup");
              Serial.println();
              Serial.println("loading Network setup");
              ngeigieSetup.loadFromFile("NETWORKS.TXT");
              lcd.setCursor(0, 2);
              lcd.print ("sensor setup");
              Serial.println();
              Serial.println("loading sensors setup");
              ngeigieSetup.loadFromFile("SENSORS.TXT");
              delay(3000);
          }
          if (!openlog_ready) {
              lcd.setCursor(0, 3);
              lcd.print("No SD card.. ");
              Serial.println();
              Serial.println("No SD card.. ");
              delay(3000);
          }
      

    //
    // SENSOR 1 setup
    if (config.sensor1_enabled) {
        //LND_712 conversionCoefficient = 0.0083;
        //LND 7317 conversionCoefficient = 0.0029;
        conversionCoefficient = 1/config.sensor1_cpm_factor; // 0.0029;
        //Pulse1 comes in at D4
        lcd.clear();
        lcd.setCursor(0, 0);
        pinMode(14, INPUT_PULLUP);
        attachInterrupt(14, onPulse, interruptMode);
        lcd.print("CMPF1=");
        lcd.print(config.sensor1_cpm_factor);
        Serial.print("CMPF1=");
        Serial.println(config.sensor1_cpm_factor); 
    }
    
    // SENSOR 2 setup
    
     if (config.sensor2_enabled) {
        // LND_712 conversionCoefficient = 0.0083;
        // LND 7317 conversionCoefficient = 0.0029;
        conversionCoefficient2 = 1/config.sensor2_cpm_factor; // 0.0029;
        //Pulse2 comes in at D15
        pinMode(15,INPUT_PULLUP);
        attachInterrupt(15, onPulse2, interruptMode);
        lcd.setCursor(0, 1);
        lcd.print("CMPF2=");
        lcd.print(config.sensor2_cpm_factor);
        Serial.print("CMPF2=");
        Serial.println(config.sensor2_cpm_factor);
        
    }
    
  //Check if Time is setup
    setSyncProvider(getTeensy3Time);
    if (timeStatus()!= timeSet) {
        Serial.println("Unable to sync with the RTC");
        lcd.setCursor(0, 3);
        lcd.print ("Please setup time");
        sprintf_P(logfile_name, PSTR("%04d1234.log"),config.user_id);
        if (openlog_ready) {
            lcd.clear();
            lcd.setCursor(0, 1);
  	    lcd.print("Log=");
            lcd.println(logfile_name);
  	    lcd.println("Local logging only");
        }
      } else {
          Serial.println("RTC has set the system time for GMT");
	       sprintf_P(logfile_name, PSTR("%04d%02d%02d.log"),config.user_id, month(), day());
  		if (openlog_ready) {
                    lcd.print("Log=");
      		    lcd.println(logfile_name);
          }
      }  
      
      //display time
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("Time (GMT):");
          printDigits(hour());
          lcd.print(":");
       	  printDigits(minute());
          lcd.setCursor(0, 1);
          lcd.print("Date:");
          lcd.print(month());
          lcd.print("-");
          lcd.print(day());
          
          Serial.print("Time (GMT):");
          printDigitsSerial(hour());
          Serial.print(F(":"));
       	  printDigitsSerial(minute());
          Serial.println("");
          Serial.print("Date:");
          Serial.print(month());
          Serial.print("-");
          Serial.println(day());
                

   
    //Gateways setup to be done
    //read for SDcard gateways 
    //store value in array
    
       //gatewaynumber=random(2);
       //Serial.print(gatewaynumber);
    
    //select randomly for total sserver
       delay(3000);
       lcd.clear();
       lcd.setCursor(0, 0);
       lcd.print("G1=");
       lcd.print(config.gw1);
       lcd.setCursor(0, 1);
       lcd.print("G2=");
       lcd.print(config.gw2);
       Serial.print("Gateway1=");
       Serial.println(config.gw1);
       Serial.print("Gateway2=");
       Serial.println(config.gw2);
       Serial.print("APIkey=");
       Serial.println(config.api_key);    

    //Display User IDs
        delay(3000);
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("ID:");
        lcd.print(config.user_id);
        lcd.print(" ID2:");
        lcd.print(config.user_id2);
        lcd.setCursor(0, 1);
        Serial.print("ID:");
        Serial.print(config.user_id);
        Serial.print(" ID2:");
        Serial.println(config.user_id2);
        
     //Display Lon/Lat
        lcd.print("LAT:");
        lcd.print(config.latitude);
        lcd.setCursor(0, 2);
        lcd.print("LON:");
        lcd.print(config.longitude);
        Serial.print("LAT:");
        Serial.println(config.latitude);
        lcd.setCursor(0, 3);
        Serial.print("LON:");
        Serial.println(config.longitude);
        delay(3000);  
    
    
    //setup update time in msec
        updateIntervalInMillis = updateIntervalInMinutes * 300000;                  // update time in ms
        //updateIntervalInMillis = updateIntervalInMinutes * 6000;                  // update time in ms
        unsigned long now1 = millis();
        nextExecuteMillis = now1 + updateIntervalInMillis;

    // say setup finished

 if (strcmp(config.intf, "3G") == 0) {
    lcd.clear();
    lcd.print("Starting up 3Gshield");
    lcd.setCursor(0, 1);
    lcd.print("counting pulses..");
    if (a3gs.start() == 0 && a3gs.begin() == 0)
           {
         }else {
           //a3gs.restart();
           lcd.setCursor(0, 0);
           lcd.print("no 3G connection ..");
       }
    }      
    // create logfile name 
    if (openlog_ready) {
        logfile_ready = true;
        createFile(logfile_name);
        

    }



/**************************************************************************/
// Print out the current device ID
/**************************************************************************/
 if (strcmp(config.intf, "EN") == 0) {
	// Initiate a DHCP session
        if (Ethernet.begin(macAddress) == 0)
	{
       		Serial.println(F("Failed DHCP"));
  // DHCP failed, so use a fixed IP address:
        	Ethernet.begin(macAddress, localIP);
	}
	lcd.clear();
	lcd.setCursor(0, 0);
	lcd.print(F("local_IP:"));
	lcd.setCursor(0, 1);
	lcd.print(Ethernet.localIP());
        Serial.print("Local IP:");
        Serial.println(Ethernet.localIP());	
        
	//delay(5000);
	Serial.println(F("setup OK."));	
	lcd.setCursor(0, 2);
	lcd.print("setup finished");
	lcd.setCursor(0, 3);
	lcd.print("no errors");
	delay(5000);
 }
//      counts_per_sample = 0;
//      counts_per_sample2 = 0;
	
}
/**************************************************************************/
// Degrees to NMEA format 
/**************************************************************************/

void deg2nmae(char *lat, char *lon, char *lat_lon_nmea)
{
 
  double lat_f = strtod(lat, NULL);
  double lon_f = strtod(lon, NULL);
  
  char NS = (lat_f >= 0) ? 'N' : 'S';
  lat_f = (lat_f >= 0) ? lat_f : -lat_f;
  int lat_d = (int)fabs(lat_f);
  double lat_min = (lat_f - lat_d)*60.;
  
  char lat_min_str[9];
  dtostrf(lat_min, 2, 4, lat_min_str);
  
  char EW = (lon_f >= 0) ? 'E' : 'W';
  lon_f = (lon_f >= 0) ? lon_f : -lon_f;
  int lon_d = (int)fabs(lon_f);
  double lon_min = (lon_f - lon_d)*60.;
  
  char lon_min_str[9];
  dtostrf(lon_min, 2, 4, lon_min_str);
  
  snprintf(lat_lon_nmea, 25, "%02d%s,%c,%03d%s,%c", lat_d, lat_min_str, NS, lon_d, lon_min_str, EW);

}
/**************************************************************************/
// display digits
/**************************************************************************/

void printDigits(int digits){
  // utility function for digital clock display: prints preceding colon and leading 0
  if(digits < 10)
    lcd.print('0');
    lcd.print(digits);
}

void printDigitsSerial(int digits){
  // utility function for digital clock display: prints preceding colon and leading 0
  if(digits < 10)
    Serial.print('0');
    Serial.print(digits);
}	
//**************************************************************************/
/*!
//  On each falling edge of the Geiger counter's output,
//  increment the counter and signal an event. The event
//  can be used to do things like pulse a buzzer or flash an LED
*/
/**************************************************************************/


void SendDataToServer(float CPM,float CPM2){ 
  
 // check if Ethetnet is selected
 if (strcmp(config.intf, "EN") == 0) {


// Convert from cpm to µSv/h with the pre-defined coefficient

    float uSv = CPM * conversionCoefficient;                   // convert CPM to Micro Sieverts Per Hour
    char CPM_string[16];
    dtostrf(CPM, 0, 0, CPM_string);
    float uSv2 = CPM2 * conversionCoefficient2;                   // convert CPM to Micro Sieverts Per Hour
     char CPM2_string[16];
    dtostrf(CPM2, 0, 0, CPM2_string);

    //display geiger info
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("1:");
      lcd.print(uSv);
      lcd.print("uSv/h"); 
      lcd.setCursor(0,1);    
      lcd.print(CPM_string); 
      lcd.print(" CPM");     
      lcd.setCursor(0,2);
      lcd.print("2:");
      lcd.print(uSv2);
      lcd.print("uSv/h");
      lcd.setCursor(0,3);
      lcd.print(CPM2_string); 
      lcd.print(" CPM");
      
      
	
 //send first sensor  
	if (client.connected())
	{
		Serial.println("Disconnecting");
		client.stop();
	}

	// Try to connect to the server
	Serial.println("Connecting");
	if (client.connect(serverIP, 80))
	{
		Serial.println("Connected");
		lastConnectionTime = millis();

		// clear the connection fail count if we have at least one successful connection
		ctrl.conn_fail_cnt = 0;
	}
	else
	{
     	   ctrl.conn_fail_cnt++;
           lcd.setCursor(0,3);
           lcd.print("no connect retry=");
           lcd.print(ctrl.conn_fail_cnt);
		if (ctrl.conn_fail_cnt >= MAX_FAILED_CONNS)
		{
                CPU_RESTART;
		}
		lastConnectionTime = millis();
		return;
	}


    // prepare the log entry

	memset(json_buf, 0, SENT_SZ);
	sprintf_P(json_buf, PSTR("{\"longitude\":\"%s\",\"latitude\":\"%s\",\"device_id\":\"%d\",\"value\":\"%s\",\"unit\":\"cpm\"}"),  \
	              config.latitude, \
	              config.longitude, \
	              config.user_id,  \
	              CPM_string);

	int len = strlen(json_buf);
	json_buf[len] = '\0';
	Serial.println(json_buf);

        if (config.dev) {
        	client.print("POST /scripts/indextest.php?api_key=");
        }
        
        if (!config.sensor1_enabled) {
                client.print("POST /scripts/index.php?api_key=");
        }     
	client.print(config.api_key);
	client.println(" HTTP/1.1");
	client.println("Accept: application/json");
	client.print("Host:");
        client.println(serverIP);
	client.print("Content-Length: ");
	client.println(strlen(json_buf));
	client.println("Content-Type: application/json");
	client.println();
	client.println(json_buf);
	Serial.println("Disconnecting");
        client.stop();
   

      
  //send second sensor  
	if (client.connected())
	{
		Serial.println("Disconnecting");
		client.stop();
	}

	// Try to connect to the server
	Serial.println("Connecting");
	if (client.connect(serverIP, 80))
	{
		Serial.println("Connected");
		lastConnectionTime = millis();

		// clear the connection fail count if we have at least one successful connection
		ctrl.conn_fail_cnt = 0;
	}
	else
	{
     	   ctrl.conn_fail_cnt++;
           lcd.setCursor(0,3);
           lcd.print("no connect retry=");
           lcd.print(ctrl.conn_fail_cnt);
		if (ctrl.conn_fail_cnt >= MAX_FAILED_CONNS)
		{
                CPU_RESTART;
		}
	    lastConnectionTime = millis();
	    return;
	}


    // prepare the log entry
	memset(json_buf2, 0, SENT_SZ);
	sprintf_P(json_buf2, PSTR("{\"longitude\":\"%s\",\"latitude\":\"%s\",\"device_id\":\"%d\",\"value\":\"%s\",\"unit\":\"cpm\"}"),  \
	              config.latitude, \
	              config.longitude, \
	              config.user_id2,  \
	              CPM2_string);

	int len2 = strlen(json_buf2);
	json_buf2[len2] = '\0';
	Serial.println(json_buf2);


        if (config.dev) {
        	client.print("POST /scripts/indextest.php?api_key=");
        }
        
        if (!config.sensor1_enabled) {
                client.print("POST /scripts/index.php?api_key=");
        }
	client.print(config.api_key);
	client.println(" HTTP/1.1");
	client.println("Accept: application/json");
	client.print("Host:");
        client.println(serverIP);
	client.print("Content-Length: ");
	client.println(strlen(json_buf2));
	client.println("Content-Type: application/json");
	client.println();
	client.println(json_buf2);
	Serial.println("Disconnecting");
        client.stop();



      //convert time in correct format
        memset(timestamp, 0, LINE_SZ);
        sprintf_P(timestamp, PSTR("%02d-%02d-%02dT%02d:%02d:%02dZ"),  \
					year(), month(), day(),  \
                    hour(), minute(), second());


      // convert degree to NMAE
		deg2nmae (config.latitude,config.longitude, lat_lon_nmea);

     //sensor 1 sd card string setup
          memset(buf, 0, LINE_SZ);
          sprintf_P(buf, PSTR("$BMRDD,%d,%s,,,%s,A,%s,1,A,,"),  \
                    config.user_id, \
                    timestamp, \
                    CPM_string, \
                    lat_lon_nmea);
  
          len = strlen(buf);
          buf[len] = '\0';
        
        // generate checksum
          chk = checksum(buf+1, len);
        
        // add checksum to end of line before sending
          if (chk < 16)
              sprintf_P(buf + len, PSTR("*0%X"), (int)chk);
          else
              sprintf_P(buf + len, PSTR("*%X"), (int)chk);
          Serial.println(buf);
       
     //sensor 2 sd card string setup
          memset(buf2, 0, LINE_SZ);     
          sprintf_P(buf2, PSTR("$BMRDD,%d,%s,,,%s,A,%s,1,A,,"),  \
                    config.user_id2, \
                    timestamp, \
                    CPM2_string, \
                    lat_lon_nmea);
  
          len2 = strlen(buf2);
          buf2[len2] = '\0';

        // generate checksum
          chk2 = checksum(buf2+1, len2);
        
        
        // add checksum to end of line before sending
          if (chk2 < 16)
              sprintf_P(buf2 + len2, PSTR("*0%X"), (int)chk2);
          else
              sprintf_P(buf2 + len2, PSTR("*%X"), (int)chk2);
              
          Serial.println(buf2);    

        //write to sd card sensor 1 info
          OpenLog.println(buf);
        //write to sd card sensor 2 info
          OpenLog.println(buf2);

       
    
    // report to LCD 
    
    lcd.setCursor(15, 4);
          printDigits(hour());
          lcd.print(":");
       	  printDigits(minute());
 }


 if (strcmp(config.intf, "3G") == 0) {
// Convert from cpm to µSv/h with the pre-defined coefficient

    float uSv = CPM * conversionCoefficient;                   // convert CPM to Micro Sieverts Per Hour
    char CPM_string[16];
    dtostrf(CPM, 0, 0, CPM_string);
    float uSv2 = CPM2 * conversionCoefficient2;                   // convert CPM to Micro Sieverts Per Hour
    char CPM2_string[16];
    dtostrf(CPM2, 0, 0, CPM2_string);

    //display geiger info
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("S1:");
      lcd.print(uSv);
      lcd.print("uSv/h");
      lcd.setCursor(0, 1);
      lcd.print("S2:");
      lcd.print(uSv2);
      lcd.print("uSv/h");

        // Create data string for sensor 1
        len = sizeof(res);
		lastConnectionTime = millis();
                  if (config.dev) {
                          sprintf_P(path, PSTR("/scripts/shorttest.php?api_key=%s&lat=%s&lon=%s&cpm=%s&id=%d"),
                          config.api_key, \
                          config.latitude, \
                          config.longitude, \
                          CPM_string, \
                          config.user_id);
                    }
                  
                  if (!config.dev) {
                          sprintf_P(path, PSTR("/scripts/short.php?api_key=%s&lat=%s&lon=%s&cpm=%s&id=%d"),
                          config.api_key, \
                          config.latitude, \
                          config.longitude, \
                          CPM_string, \
                          config.user_id);
                  }


                  
                  
       // Create data string for sensor 2
                  if (config.dev) {
                          sprintf_P(path2, PSTR("/scripts/shorttest.php?api_key=%s&lat=%s&lon=%s&cpm=%s&id=%d"),
                          config.api_key, \
                          config.latitude, \
                          config.longitude, \
                          CPM2_string, \
                          config.user_id2);   
                  }
                  
                  if (!config.sensor1_enabled) {
                          sprintf_P(path2, PSTR("/scripts/short.php?api_key=%s&lat=%s&lon=%s&cpm=%s&id=%d"),
                          config.api_key, \
                          config.latitude, \
                          config.longitude, \
                          CPM2_string, \
                          config.user_id2);  
                   }        
                  

        //convert time in correct format
        memset(timestamp, 0, LINE_SZ);
        sprintf_P(timestamp, PSTR("%02d-%02d-%02dT%02d:%02d:%02dZ"),  \
					year(), month(), day(),  \
                    hour(), minute(), second());
                    
                    
		// convert degree to NMAE
		deg2nmae (config.latitude,config.longitude, lat_lon_nmea);
     
     
       //sensor 1 sd card string setup
        memset(buf, 0, LINE_SZ);
        sprintf_P(buf, PSTR("$BMRDD,%d,%s,,,%s,A,%s,1,A,,"),  \
                  config.user_id, \
                  timestamp, \
                  CPM_string, \
                  lat_lon_nmea);

        len = strlen(buf);
        buf[len] = '\0';
        
        // generate checksum
        chk = checksum(buf+1, len);
        
        // add checksum to end of line before sending
        if (chk < 16)
            sprintf_P(buf + len, PSTR("*0%X"), (int)chk);
        else
            sprintf_P(buf + len, PSTR("*%X"), (int)chk);
        Serial.println(buf);
       
       //sensor 2 sd card string setup
        memset(buf2, 0, LINE_SZ);     
        sprintf_P(buf2, PSTR("$BMRDD,%d,%s,,,%s,A,%s,1,A,,"),  \
                  config.user_id2, \
                  timestamp, \
                  CPM2_string, \
                  lat_lon_nmea);

        len2 = strlen(buf2);
        buf2[len2] = '\0';
        //check if timestamp works
       

        // generate checksum
        chk2 = checksum(buf2+1, len2);
        
        
        // add checksum to end of line before sending
        if (chk2 < 16)
            sprintf_P(buf2 + len2, PSTR("*0%X"), (int)chk2);
        else
            sprintf_P(buf2 + len2, PSTR("*%X"), (int)chk2);
            
        Serial.println(buf2);    

        //write to sd card sensor 1 info
        OpenLog.println(buf);
        //write to sd card sensor 2 info
        OpenLog.println(buf2);

        //send to server
        if (a3gs.httpGET(server, port, path, res, len) == 0) {
	           Serial.println(F("Sent sensor 1 info to server OK!"));
            a3gs.httpGET(server, port, path2, res, len);
                   Serial.println(F("Sent sensor 2 info to server OK!"));
		  conn_fail_cnt = 0;
      
             //Display infomation 
                lcd.setCursor(0, 2);
            	lcd.print("Sent (GMT):");
                printDigits(hour());
                lcd.print(F(":"));
             	printDigits(minute());
                lcd.setCursor(0, 3);
                //lcd.print(     );
                lcd.print("CPM1:");
                lcd.print(CPM_string);
                lcd.print("  CPM2:");
                lcd.print(CPM2_string);
                         
            lastConnectionTime = millis();
        }
        else {
            
            lcd.setCursor(0,2);
            lcd.print("NC API! SDcard only");
            lastConnectionTime = millis();
            Serial.println("No connection to API!");
            Serial.println("saving to SDcard only");
            
            conn_fail_cnt++;
		if (conn_fail_cnt >= MAX_FAILED_CONNS)
		{
                      //first shut down 3G before reset
                      a3gs.end();
                      a3gs.shutdown();
		      
                      CPU_RESTART;
		}
                  lcd.setCursor(0, 2);
                  lcd.print("Retries left:");
                  lcd.print(MAX_FAILED_CONNS - conn_fail_cnt);
                  Serial.print("NC. Retries left:");
                  Serial.println(MAX_FAILED_CONNS - conn_fail_cnt);
		lastConnectionTime = millis();
		return;
        }
    //}
    //clean out the buffers
    memset(buf, 0, sizeof(buf));
    memset(path, 0, sizeof(path));
    lastConnectionTime = millis();


}
}



/**************************************************************************/
// Main Loop
/**************************************************************************/
void loop() {

    // Main Loop
      if (elapsedTime(lastConnectionTime) < updateIntervalInMillis)
      {
          return;
      }
  
      float CPM = (float)counts_per_sample / (float)updateIntervalInMinutes/5;
      counts_per_sample = 0;
      float CPM2 = (float)counts_per_sample2 / (float)updateIntervalInMinutes/5;
      counts_per_sample2 = 0;
      
      SendDataToServer(CPM,CPM2);
  }


/**************************************************************************/
// calculate elapsed time, taking into account rollovers
/**************************************************************************/
unsigned long elapsedTime(unsigned long startTime)
{
	unsigned long stopTime = millis();

	if (startTime >= stopTime)
	{
		return startTime - stopTime;
	}
	else
	{
		return (ULONG_MAX - (startTime - stopTime));
	}
}



/**************************************************************************/
// OpenLog code
/**************************************************************************/

/* wait for openlog prompt */
bool waitOpenLog(bool commandMode) {
    int safeguard = 0;
    bool result = false;

    while(safeguard < OPENLOG_RETRY) {
        safeguard++;
        if(OpenLog.available())
            if(OpenLog.read() == (commandMode ? '>' : '<')) break;
        delay(10);
    }

    if (safeguard >= OPENLOG_RETRY) {
    } else {
        result = true;
    }

    return result;
}

/* setups up the software serial, resets OpenLog */
void setupOpenLog() {
    pinMode(resetOpenLog, OUTPUT);
    OpenLog.listen();

    // reset OpenLog
    digitalWrite(resetOpenLog, LOW);
    delay(100);
    digitalWrite(resetOpenLog, HIGH);

    if (!waitOpenLog(true)) {
        logfile_ready = true;
    } else {
        openlog_ready = true;
    }
}

/* create a new file */
void createFile(char *fileName) {
    int result = 0;
    int safeguard = 0;

    OpenLog.listen();

    do {
        result = 0;

        do {
            OpenLog.print("append ");
            OpenLog.print(fileName);
            OpenLog.write(13); //This is \r

            if (!waitOpenLog(false)) {
                break;
            }
            result = 1;
        } while (0);

        if (0 == result) {
            // reset OpenLog
            digitalWrite(resetOpenLog, LOW);
            delay(100);
            digitalWrite(resetOpenLog, HIGH);

            // Wait for OpenLog to return to waiting for a command
            waitOpenLog(true);
        }
    } while (0 == result);

    //OpenLog is now waiting for characters and will record them to the new file
}

/**************************************************************************/
//RTC setup
/**************************************************************************/

    time_t getTeensy3Time()
    {
      return Teensy3Clock.get();
    }

/**************************************************************************/
//WDT setup
/**************************************************************************/

    void KickDog() {
      Serial.println("Patting the dog!");
      //digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
      noInterrupts();
      WDOG_REFRESH = 0xA602;
      WDOG_REFRESH = 0xB480;
      interrupts();
    }

//WDT error messages
    void printResetType() {
        if (RCM_SRS1 & RCM_SRS1_SACKERR)   Serial.println("[RCM_SRS1] - Stop Mode Acknowledge Error Reset");
        if (RCM_SRS1 & RCM_SRS1_MDM_AP)    Serial.println("[RCM_SRS1] - MDM-AP Reset");
        if (RCM_SRS1 & RCM_SRS1_SW)        Serial.println("[RCM_SRS1] - Software Reset");
        if (RCM_SRS1 & RCM_SRS1_LOCKUP)    Serial.println("[RCM_SRS1] - Core Lockup Event Reset");
        if (RCM_SRS0 & RCM_SRS0_POR)       Serial.println("[RCM_SRS0] - Power-on Reset");
        if (RCM_SRS0 & RCM_SRS0_PIN)       Serial.println("[RCM_SRS0] - External Pin Reset");
        if (RCM_SRS0 & RCM_SRS0_WDOG)      Serial.println("[RCM_SRS0] - Watchdog(COP) Reset");
        if (RCM_SRS0 & RCM_SRS0_LOC)       Serial.println("[RCM_SRS0] - Loss of External Clock Reset");
        if (RCM_SRS0 & RCM_SRS0_LOL)       Serial.println("[RCM_SRS0] - Loss of Lock in PLL Reset");
        if (RCM_SRS0 & RCM_SRS0_LVD)       Serial.println("[RCM_SRS0] - Low-voltage Detect Reset");
    }

//WDT hook setup
      #ifdef __cplusplus
      extern "C" {
      #endif
        void startup_early_hook() {
          WDOG_TOVALL = (10000); // The next 2 lines sets the time-out value. This is the value that the watchdog timer compare itself to.
          WDOG_TOVALH = 0;
          WDOG_STCTRLH = (WDOG_STCTRLH_ALLOWUPDATE | WDOG_STCTRLH_WDOGEN | WDOG_STCTRLH_WAITEN | WDOG_STCTRLH_STOPEN); // Enable WDG
          //WDOG_PRESC = 0; // prescaler 
        }
      #ifdef __cplusplus
      }
      #endif

 
