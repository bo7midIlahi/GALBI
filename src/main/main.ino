#include "frame.h"
#include <U8g2lib.h>
//U8G2_ST7920_128X64_F_SW_SPI u8g2(U8G2_R0, /* clock=*/ 18, /* data=*/ 19, /* CS=*/ 17, /* reset=*/ 8);
U8G2_ST7920_128X64_F_SW_SPI u8g2(U8G2_R0,18, 19, 17,U8X8_PIN_NONE);

#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"
MAX30105 particleSensor;

#define ECG_PIN 28
#define IR_THRESHOLD 1000
#define ECG_THRESHOLD 550
//waveform buffer
#define WAVE_X 0
#define WAVE_Y 63          // bottom of screen
#define WAVE_W 128         // full width
#define WAVE_H 50          // almost full height
//waveform struct
struct Waveform {
  int16_t buf[WAVE_W];
  uint8_t idx;
  long dc;
  int16_t minVal;
  int16_t maxVal;
};

Waveform ppgWave;   // MAX30102
Waveform ecgWave;   // AD8232


const byte RATE_SIZE = 4; //Increase this for more averaging. 4 is good.
byte rates[RATE_SIZE]; //Array of heart rates
byte rateSpot = 0;
long lastBeat = 0; //Time at which the last beat occurred

float beatsPerMinute;
int beatAvg;

#define BTN_PIN 22


const uint16_t bytesPerFrame = 928;  
// Helper to get a pointer to a specific frame
const unsigned char* getFrame(uint8_t frame) {
  return epd_bitmap_ + (frame * bytesPerFrame);
}

// Function to draw a frame
void drawFrame(U8G2 &u8g2, uint8_t frame) {
  u8g2.drawXBMP(0, 2, 128, 58, getFrame(frame));
}

void welcome_animation(U8G2 &u8g2) {
  for(uint8_t iter = 0; iter<2; iter++){
    for (uint8_t f = 0; f < 15; f++) {
      drawFrame(u8g2, f);
      u8g2.sendBuffer();
      delay(150); // frame delay
    }
  }
}

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

  //Setup to sense a nice looking saw tooth on the plotter
  byte ledBrightness = 0x1F; //Options: 0=Off to 255=50mA
  byte sampleAverage = 8; //Options: 1, 2, 4, 8, 16, 32
  byte ledMode = 3; //Options: 1 = Red only, 2 = Red + IR, 3 = Red + IR + Green
  int sampleRate = 100; //Options: 50, 100, 200, 400, 800, 1000, 1600, 3200
  int pulseWidth = 411; //Options: 69, 118, 215, 411
  int adcRange = 4096; //Options: 2048, 4096, 8192, 16384

  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange); //Configure sensor with these settings

  particleSensor.setPulseAmplitudeRed(0x0A);
  particleSensor.setPulseAmplitudeGreen(0);

  pinMode(BTN_PIN, INPUT_PULLUP);

  welcome_animation(u8g2);

    //Take an average of IR readings at power up
  const byte avgAmount = 64;
  long baseValue = 0;
  for (byte x = 0 ; x < avgAmount ; x++)
  {
    baseValue += particleSensor.getIR(); //Read the IR value
  }
  baseValue /= avgAmount;

  //Pre-populate the plotter so that the Y scale is close to IR values
  for (int x = 0 ; x < 500 ; x++)
    Serial.println(baseValue);

  //initialize waveform parameters
  ppgWave = { {}, 0, 0, -2500, 2500 };   // MAX30102
  ecgWave = { {}, 0, 0, -1200, 1200 };   // AD8232 (example range)

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

void drawWaveform(const Waveform &w) {
  for (int i = 1; i < WAVE_W; i++) {
    int i1 = (w.idx + i - 1) % WAVE_W;
    int i2 = (w.idx + i) % WAVE_W;

    int y1 = map(w.buf[i1], w.minVal, w.maxVal,
                 WAVE_Y - WAVE_H, WAVE_Y);
    int y2 = map(w.buf[i2], w.minVal, w.maxVal,
                 WAVE_Y - WAVE_H, WAVE_Y);

    u8g2.drawLine(i - 1, y1, i, y2);
  }

  //center line (helps visually)
  int midY = WAVE_Y - WAVE_H / 2;
  u8g2.drawHLine(0, midY, 128);
}

void drawWaveHeader(int8_t page) {
  u8g2.setFont(u8g2_font_04b_03_tr);

  char buf[6];
  if(page == 0) {
    sprintf(buf, "%d", beatAvg);
    u8g2.drawStr(2, 6, buf);
    u8g2.drawStr(17, 6, "BPM [MAX 30102]");
    u8g2.drawStr(0, 62, "PLACE YOUR FINGER ON SENSOR");
  }
  if(page == 1){
    sprintf(buf, "%d", beatAvg);
    u8g2.drawStr(2, 6, buf);
    u8g2.drawStr(17, 6, "BPM [AD 8323]");
    u8g2.drawStr(0, 60, "PLACE ELECTRODES ON BODY");
  }

  // optional signal indicator
  if (abs(beatsPerMinute - beatAvg) > 15)
    u8g2.drawStr(90, 6, "UNSTABLE");
}

//calculate RR-interval from AD
bool detectRPeak(int ecgValue) {
  static int prev = 0;
  static bool above = false;

  if (ecgValue > ECG_THRESHOLD && !above && prev < ecgValue) {
    above = true;
    return true;
  }
  if (ecgValue < ECG_THRESHOLD) {
    above = false;
  }

  prev = ecgValue;
  return false;
}

unsigned long lastECGBeat = 0;
unsigned long rrECG = 0;

void onECGBeatDetected() {
  unsigned long now = millis();
  rrECG = now - lastECGBeat;
  lastECGBeat = now;
}


//calculates RR-INTERVAL from MAX
unsigned long lastPPGBeat = 0;
unsigned long rrPPG = 0;

void onPPGBeatDetected() {
  unsigned long now = millis();
  rrPPG = now - lastPPGBeat;
  lastPPGBeat = now;
}

void drawTable(long irValue) {
  u8g2.drawFrame(0, 0, 128 , 64);
  // columns headers
  u8g2.drawStr(25, 7, "R-R");
  u8g2.drawStr(46, 7, "HR_E");
  u8g2.drawStr(74, 7, "HR_P");
  u8g2.drawStr(104, 7, "dHR");

  //lines headers
  u8g2.drawStr(4, 20, "ECG");
  u8g2.drawStr(3, 35, "MAX");
  u8g2.drawStr(4, 55, "ACT:");

  //columns seperators
  u8g2.drawVLine(20, 1, 40);
  u8g2.drawVLine(44, 1, 40);
  u8g2.drawVLine(72, 1, 40);
  u8g2.drawVLine(100, 1, 40);


  //lines seperators
  u8g2.drawHLine(1,  10, 126);
  u8g2.drawHLine(1,  25, 100);
  u8g2.drawHLine(1,  40, 126);

	char buff[6];
  //values
  float bpmECG = 60000.0 / rrECG;
  float bpmPPG = 60000.0 / rrPPG;
  int dHR = abs((int)bpmECG - (int)bpmPPG);


  //ECG ROW
  dtostrf(rrECG, 4, 0, buff);
  u8g2.drawStr(23, 20, buff);

  dtostrf(bpmECG, 4, 0, buff);
  u8g2.drawStr(46, 20, buff);

  //MAX ROW
  dtostrf(rrPPG, 4, 0, buff);
  u8g2.drawStr(23, 35, buff);

  dtostrf(bpmPPG, 4, 0, buff);
  u8g2.drawStr(74, 35, buff);

  //draw dHR
  sprintf(buff, "%d", dHR);
  u8g2.drawStr(104, 27, buff);

  //visual feedback
  if (dHR > 10) {
    u8g2.drawFrame(102, 15, 24, 20);  // warning box
  }
  
  //ACT: interpretation
	bool tachy = beatAvg > 100;
	bool brady = beatAvg < 50;
	bool noSignal = irValue < IR_THRESHOLD;

  if (noSignal){
    u8g2.drawStr(20, 55, "NO_SIGNAL");
  }else if (dHR > 15){
    u8g2.drawStr(20, 55, "UNSTABLE");
  }else{
    u8g2.drawStr(20, 55, "NORMAL");
  }

  if (brady){
    u8g2.drawFrame(67, 47, 56, 11);
    u8g2.drawStr(69, 55, "BRADYCARDIA");
  }

  if (tachy) {
    u8g2.drawFrame(67, 47, 54, 11);
    u8g2.drawStr(69, 55, "TACHYCARDIA");
  }
}

void getVitals(){
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

void drawUI(int8_t page, long irValue){
  if (page == 0) { // draw HR waveforms from MAX 30102
    updateWaveform(ppgWave, irValue, 0.03);
    drawWaveHeader(page);
    drawWaveform(ppgWave);
  }

  if(page==1) {// draw HR waveforms from AD8232
    long ecgValue = analogRead(ECG_PIN);
    updateWaveform(ecgWave, ecgValue, 0.01); // slower DC tracking
    drawWaveHeader(page);
    drawWaveform(ecgWave);
  }

  if(page==2) {// Summary
    drawTable(irValue);
  }
}

void updateWaveform(Waveform &w, long value, float dcAlpha) {
  w.dc = w.dc * (1.0 - dcAlpha) + value * dcAlpha;

  int16_t ac = value - w.dc;
  ac = constrain(ac, w.minVal, w.maxVal);

  w.buf[w.idx] = ac;
  w.idx = (w.idx + 1) % WAVE_W;
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
    onPPGBeatDetected();
    getVitals();
  }

  Serial.print("IR=");Serial.print(irValue);
  Serial.print(", BPM=");Serial.print(beatsPerMinute);
  Serial.print(", Avg BPM=");Serial.print(beatAvg);

  if (irValue < 50000){
    Serial.print(" No finger?");
  }
  Serial.println();

  Serial.print("EKG:");Serial.println(analogRead(ECG_PIN));

  //updateWaveform(irValue);

  drawUI(page,irValue);

  u8g2.sendBuffer();
}