#include <GyverTimers.h>

#define INT_NUM 0
#define DIM_PIN 4
#define ZC_PIN 2

int dimmer = 0; 

void setup() {
Serial.begin(9600);
Serial.setTimeout(50);
pinMode(DIM_PIN, OUTPUT);
pinMode(ZC_PIN, INPUT_PULLUP);
Timer2.enableISR();
attachInterrupt(INT_NUM, isr, FALLING);
}

void loop() {

if (Serial.available()) {
  dimmer = Serial.parseInt();
  if (dimmer > 8600) {
    detachInterrupt(INT_NUM);
    digitalWrite(DIM_PIN, 0);
  }
  if (dimmer < 1200) {
    detachInterrupt(INT_NUM);
    digitalWrite(DIM_PIN, 1);
  }
  if (dimmer > 1200 && dimmer < 8600) {
    attachInterrupt(INT_NUM, isr, FALLING);
  }
}

} // loop

void isr() {
  static int lastDim;
  if (lastDim != dimmer) Timer2.setPeriod(lastDim = dimmer);
  else Timer2.restart();
}

ISR(TIMER2_A) {
  digitalWrite(DIM_PIN, 1);  // включаем симистор на оставшейся длине полуволны после отсчета таймером
  Timer2.stop();             // останавливаем таймер до следующего прерывания где он будет перезапущен
  digitalWrite(DIM_PIN, 0);  // выставляем 0 на оптопаре сразу, но симистор остается открытым до прохождения нуля
}
