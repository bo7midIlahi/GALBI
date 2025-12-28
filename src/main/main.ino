#include <U8g2lib.h>
//U8G2_ST7920_128X64_F_SW_SPI u8g2(U8G2_R0, /* clock=*/ 18, /* data=*/ 19, /* CS=*/ 17, /* reset=*/ 8);
U8G2_ST7920_128X64_F_SW_SPI u8g2(U8G2_R0,18, 19, 17,U8X8_PIN_NONE);

#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"

MAX30105 particleSensor;

const byte RATE_SIZE = 8; //Increase this for more averaging. 4 is good.
byte rates[RATE_SIZE]; //Array of heart rates
byte rateSpot = 0;
long lastBeat = 0; //Time at which the last beat occurred

float beatsPerMinute;
int beatAvg;

#define BTN_PIN 22

void setup() {
  Serial.begin(115200);

  u8g2.begin();

  Wire.setSDA(4);
  Wire.setSCL(5);
  Wire.begin();

  if (!particleSensor.begin(Wire)) {
    Serial.println("MAX30105 NOT FOUND");
    while (1);
  }

  particleSensor.setup();
  particleSensor.setPulseAmplitudeRed(0x0A);
  particleSensor.setPulseAmplitudeGreen(0);

  pinMode(BTN_PIN, INPUT_PULLUP);
}

bool lastBtnState = LOW;
bool btn_pressed() {
  bool currentBtnState = digitalRead(BTN_PIN);
  
  if (currentBtnState == HIGH && lastBtnState == LOW){
    lastBtnState = currentBtnState;
    return true;
  }

  lastBtnState = currentBtnState;
  return false;
}

void drawVitals(int8_t page, long beatsPerMinute, long beatAvg){
  Serial.print("HR: ");
  Serial.println(beatsPerMinute);

  char buff[50];
  sprintf(buff,"%d",page);
  
  u8g2.drawStr(0,5, "HR:");
  u8g2.drawStr(15,5, buff);
  u8g2.drawStr(30,5, "[ECG]");

  dtostrf(beatAvg, 4, 1, buff);
  u8g2.drawStr(55,5, buff);
  u8g2.drawStr(73,5, "[MAX]");
}

void drawHR(){
  u8g2.drawStr(30, 40, "HEART");
}

void drawTable() {
  u8g2.drawStr(30, 40, "TABLE");
}

int8_t page = 0;
void loop() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_04b_03_tr);


  if(btn_pressed()) {
    page += 1;
    page %=3;
  }
  Serial.print("PAGE: ");
  Serial.println(page);

  long irValue = particleSensor.getIR();
  if (checkForBeat(irValue) == true)
  {
    //We sensed a beat!
    long delta = millis() - lastBeat;
    lastBeat = millis();

    beatsPerMinute = 60 / (delta / 1000.0);

    if (beatsPerMinute < 255 && beatsPerMinute > 20)
    {
      rates[rateSpot++] = (byte)beatsPerMinute; //Store this reading in the array
      rateSpot %= RATE_SIZE; //Wrap variable

      //Take average of readings
      beatAvg = 0;
      for (byte x = 0 ; x < RATE_SIZE ; x++)
        beatAvg += rates[x];
      beatAvg /= RATE_SIZE;
    }
  }

  if(page==0) { // first page: VITALS READINGS
    drawVitals(page,beatsPerMinute,beatAvg);
  }

  if(page==1) {// second page: HR WAVEFORM
    drawHR();
  }

  if(page==2) {// second page: HR WAVEFORM
    drawTable();
  }

  u8g2.sendBuffer();

}
