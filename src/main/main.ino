#include <U8g2lib.h>
U8G2_ST7920_128X64_F_SW_SPI u8g2(U8G2_R0, /* clock=*/ 13, /* data=*/ 11, /* CS=*/ 10, /* reset=*/ 8);

#define BTN_PIN 2

void setup() {
  Serial.begin(9600);
  u8g2.begin();
  pinMode(BTN_PIN,INPUT_PULLUP);
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

uint8_t count = 0;

void loop() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_04b_03_tr);


  if(btn_pressed()) {
    count += 1;
  }
  Serial.print("COUNT: ");
  Serial.println(count);

  
  char buff[50];
  sprintf(buff,"%d",count+100);
  
  u8g2.drawStr(0,5, "HR:");
  u8g2.drawStr(15,5, buff);
  u8g2.drawStr(30,5, "[ECG]");
  u8g2.drawStr(55,5, buff);
  u8g2.drawStr(70,5, "[MAX]");
  
  u8g2.drawStr(0, 15, "SpO2:");
  u8g2.drawStr(23,15, buff);
  u8g2.drawStr(45,15, "RR-interval");
  u8g2.drawStr(100,15, buff);

  u8g2.sendBuffer();

}
