#include <Arduino.h>
#include <Adafruit_ADS1X15.h>
#include <SPI.h>
#include <Metro.h>
#include <SdFat.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#define RESTART_ADDR 0xE000ED0C
#define READ_RESTART() (*(volatile uint32_t *)RESTART_ADDR)
#define WRITE_RESTART(val) ((*(volatile uint32_t *)RESTART_ADDR) = (val))
LiquidCrystal_I2C lcd(0x27,16,2); //yeet
//running/standby status LEDS nonsense
elapsedMillis LED1micro;
elapsedMillis LED2micro;
const int LED1 = 8; //green one
const int LED2 = 9; //red one
unsigned long LED1_Interval = 200;
unsigned long LED2_Interval = 200;
//SD card logging stuff
String longAssString="";
#define SPI_CLOCK SD_SCK_MHZ(50)
//fuck
const int ledPin = 13;
// Try to select the best SD card configuration.
#if HAS_SDIO_CLASS
#define SD_CONFIG SdioConfig(FIFO_SDIO)
#elif  ENABLE_DEDICATED_SPI
#define SD_CONFIG SdSpiConfig(SD_CS_PIN, DEDICATED_SPI, SPI_CLOCK)
#else  // HAS_SDIO_CLASS
#define SD_CONFIG SdSpiConfig(SD_CS_PIN, SHARED_SPI, SPI_CLOCK)
#endif  // HAS_SDIO_CLASS
#define SD_FAT_TYPE 1
#if SD_FAT_TYPE == 0
SdFat sd;
typedef File file_t;
#elif SD_FAT_TYPE == 1
SdFat32 sd;
typedef File32 file_t;
#elif SD_FAT_TYPE == 2
SdExFat sd;
typedef ExFile file_t;
#elif SD_FAT_TYPE == 3
SdFs sd;
typedef FsFile file_t;
#else  // SD_FAT_TYPE
#error Invalid SD_FAT_TYPE
#endif  // SD_FAT_TYPE
#define FILE_BASE_NAME "FuseTestLog"
file_t myFile;
const uint8_t BASE_NAME_SIZE=sizeof(FILE_BASE_NAME)-1;
char fileName[] = FILE_BASE_NAME "00.txt";
//end of the sd stuff, other stuff here
//LOGGING RATE
//MUST BE 5ms FOR REAL TESTING!!!
Metro logRate=Metro(1);
Adafruit_ADS1115 ads;
float volts0, volts3;
float logStarttime;
float cellVolts; //
float timestamp; //test runtime
//Relay control defs
#define OPEN 0
#define CLOSED 1

int relayStatus;
int relayPin =32;
//tester states
#define STARTUP 2
#define STANDBY 0
#define RUNNING 1
int testerState=STARTUP;
int lastTesterState;
elapsedMillis restartTimer;
unsigned long restartTimeout=5000;
void setup() {
  lcd.init();                      // initialize the lcd 
  lcd.backlight();
  pinMode(relayPin,OUTPUT);
  pinMode(33,INPUT_PULLUP);
  pinMode(23,INPUT);
  pinMode(LED1,OUTPUT); digitalWrite(LED1,HIGH);
  pinMode(LED2,OUTPUT);
  digitalWrite(relayPin,OPEN);
  // put your setup code here, to run once:
  Serial.begin(115200);
  ads.setGain(GAIN_FOUR);
  if (!ads.begin()) {
    Serial.println("Failed to initialize ADS.");
    //while (1);
  }
  if (!sd.begin(SD_CONFIG)) {
    lcd.clear();
    lcd.print("SD failed");lcd.setCursor(0,1);lcd.print("Check Serial");
    sd.initErrorPrint(&Serial);
    while(digitalRead(33)==1){
    }
    lcd.clear();
    lcd.print("Continued NO SD");
  }
  //check sd card for existing logs
  while (sd.exists(fileName)) {
    if (fileName[BASE_NAME_SIZE + 1] != '9') {
      fileName[BASE_NAME_SIZE + 1]++;
    } else if (fileName[BASE_NAME_SIZE] != '9') {
      fileName[BASE_NAME_SIZE + 1] = '0';
      fileName[BASE_NAME_SIZE]++;
    } else {
      Serial.println(F("Can't create file name"));
      return;
    }
  }
  if (!myFile.open(fileName, FILE_WRITE)) {
    sd.errorPrint("opening new log for write failed");
  }
  myFile.open(fileName);
  //myFile.println(fileName);
  myFile.println("Time,Voltage,Current");
  myFile.close();
  lcd.clear();
  lcd.print("Waiting for button press lol");
  Serial.print("Waiting for button press lol\n");
  while(digitalRead(33)==1){
  }
  Serial.println("STARTING LOG");
  testerState=RUNNING;
  logStarttime=millis();
}
void statusLEDS();  //blinks the mf LEDS
void delayedRelayClose(float closingTime);//to close relay and start current flow **AFTER** we start logging
void restartLogger(int testerState);
void loop() {
  digitalWrite(ledPin,LOW);
  volts0=ads.readADC_Differential_0_1();
  float realVolts=ads.computeVolts(volts0);
  float realAmps=realVolts/0.00075;
  float now = millis()-logStarttime;
  float timestamp = now/1000;
  cellVolts = analogRead(23)*3.3/1024;
  char buffer[80];
  int n = sprintf(buffer,"%f,%f,%f,%d\n",timestamp,cellVolts,realAmps,digitalRead(relayPin));
  if(logRate.check()==1 && testerState==RUNNING){
  digitalWrite(ledPin, HIGH);
  // myFile.open(fileName,FILE_WRITE);
  // myFile.write(buffer);
  // myFile.close();
  longAssString+=buffer;
  Serial.print(buffer);
  lcd.clear();
  lcd.print(timestamp);
  lcd.setCursor(0,1);
  lcd.print(realAmps);
  }
  delayedRelayClose(timestamp);
   if(cellVolts<=0.05 && timestamp>=1 && testerState==RUNNING){
    char buffer2[80];
    sprintf(buffer2,"FUSE BLOWN AT %f\n",timestamp);
    lcd.clear();
    lcd.print(buffer2);
    Serial.println(buffer2);
    myFile.open(fileName,FILE_WRITE);
    myFile.print(longAssString);
    myFile.write(buffer2);
    myFile.write(sizeof(longAssString));
    myFile.close();
    lcd.setCursor(0,1);
    lcd.print(fileName);
    testerState=STANDBY;
    restartTimer=0;
  }else if(digitalRead(33)==0 && timestamp>=10 && testerState==RUNNING){
    //do not let relay close command happen immediately after
    //starting the test
    Serial.println("LOG stopped manually");
    char buffer2[80];
    sprintf(buffer2,"LogStopped@ %f\n",timestamp);
    lcd.clear();
    lcd.print(buffer2);
    Serial.println(buffer2);
    myFile.open(fileName,FILE_WRITE);
    myFile.print(longAssString);
    myFile.write(buffer2);
    myFile.write(sizeof(longAssString));
    myFile.close();
    lcd.setCursor(0,1);
    lcd.print(fileName);
    testerState=STANDBY;
    restartTimer=0;
  }
  statusLEDS(); 
  restartLogger(testerState);
}
void statusLEDS(){
    if(testerState==STANDBY){
    if (LED1micro >= LED1_Interval)
  {
    digitalWrite(LED1, !(digitalRead(LED1))); // toggle the LED state
    LED1micro = 0;                            // reset the counter to 0 so the counting starts over...
  }}
  else if(testerState==RUNNING){
    if (LED2micro >= LED2_Interval)
  {
    digitalWrite(LED2, !(digitalRead(LED2))); // toggle the LED state
    LED2micro = 0;                            // reset the counter to 0 so the counting starts over...
  }}
}
void delayedRelayClose(float closingTime){
  if(digitalRead(relayPin)!=testerState && closingTime>.5){
    digitalWrite(relayPin,testerState);
  }
}
void restartLogger(int testerState){
  if(testerState==STANDBY && restartTimer>=restartTimeout){
    lcd.clear();
    lcd.print("RESTARTING");
    WRITE_RESTART(0x5FA0004);
  }
  else if(testerState==RUNNING){
    restartTimer=0;
  }
}
