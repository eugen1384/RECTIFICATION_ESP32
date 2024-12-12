//COOLING STATION
#include <LiquidCrystal_I2C.h>
#include <microDS18B20.h>
#include <EncButton.h>
#include <EEPROM.h>

#define EN_PIN 10
#define BTN_PIN 8
#define S1_PIN 6
#define S2_PIN 7
#define PWM_PIN 9
#define TW_PIN 4
#define PUMP_PIN 12
#define ZOOM_PIN 3
#define FLOW_PIN 2

volatile int flow_frequency;
unsigned int l_hour;
unsigned long currentTime;
unsigned long cloopTime;

int mode = 1;

void flow () // Interrupt function
{ 
  flow_frequency++;
}

LiquidCrystal_I2C lcd(0x27, 16, 2);
EncButton<EB_TICK, S1_PIN, S2_PIN, BTN_PIN> enc;
MicroDS18B20<TW_PIN> sensor_water;

int pwm_pow;
float water_temp;
float flow_rate;
String mode_desc;

void setup() {
pinMode(EN_PIN, INPUT);
pinMode(BTN_PIN, INPUT);
pinMode(S1_PIN, INPUT);
pinMode(S2_PIN, INPUT);
pinMode(PWM_PIN, OUTPUT);
digitalWrite(PWM_PIN, 0);
pinMode(PUMP_PIN, OUTPUT);
pinMode(ZOOM_PIN, OUTPUT);
pinMode(FLOW_PIN, INPUT);
pinMode(13, OUTPUT);

attachInterrupt(0, flow, RISING);
currentTime = millis();
cloopTime = currentTime;

lcd.init();
lcd.backlight();

lcd.setCursor(0, 0); lcd.print("BLACK BOX V5.2 ");
delay(1000);
lcd.setCursor(0, 1); lcd.print("COOLING STATION");
delay(1500);
lcd.clear();
}

void loop() {
enc.tick();

if (enc.press()) {
if (mode < 3) {mode = mode + 1;}
if (mode == 3) {mode = 0;} 
}

if (mode == 0) { mode_desc = "RAUTO"; }
if (mode == 1) { mode_desc = "MAN  "; }
if (mode == 2) { mode_desc = "PAUTO"; }
if (enc.right()) {
  if (mode == 1) { pwm_pow = constrain(pwm_pow + 1, 0, 100); }
}

if (enc.left()) {
  if (mode == 1) { pwm_pow = constrain(pwm_pow - 1, 0, 100); }
}

//МОЩНОСТЬ КУЛЕРОВ
static uint32_t tmr_cool;
if (millis() - tmr_cool >= 1000) {
  tmr_cool = millis();
  if (mode == 0 || mode == 2) {
  pwm_pow = map(water_temp, 10, 55, 0, 100);
  }
}

// ПОЛУЧАЕМ ТЕМПЕРАТУРУ РАЗ В СЕКУНДУ
static uint32_t tmr_temp;
if (millis() - tmr_temp >= 1000) {
  tmr_temp = millis();
if (sensor_water.readTemp()) {
    water_temp = sensor_water.getTemp();
    sensor_water.requestTemp();
}
else {
  sensor_water.requestTemp();
 }
}

//ОПРАШИВАЕМ ENABLE НА ПОМПУ
static uint32_t tmr_pump;
if (millis() - tmr_pump >= 500) {
  tmr_pump = millis();
if (digitalRead(EN_PIN)) {
digitalWrite(PUMP_PIN, 1);  
  }
else {
    if (mode == 2) {digitalWrite(PUMP_PIN, 1);}
    else {digitalWrite(PUMP_PIN, 0);} 
  }

//мигаем диодом сигнализируя работу
if (digitalRead(13)) {digitalWrite(13, 0);}
else {digitalWrite(13, 1);
  }
}

//ВЫВОДИМ НА ДИСПЛЕЙ
static uint32_t tmr_disp;
if (millis() - tmr_disp >= 300) {
  tmr_disp = millis();
  disp_print();
}

//ОПРАШИВАЕМ FLOW ДАТЧИК 
static uint32_t tmr_flow;
if (millis() - tmr_flow >= 1000) {
  tmr_flow = millis();
l_hour = (flow_frequency * 60 / 7.5);
flow_frequency = 0;
}

// ЗАДЕМ МОЩНОСТЬ PWM СИГНАЛА НА КУЛЕРЫ
analogWrite(PWM_PIN, map(pwm_pow, 0, 100, 0, 255));

//ДОБАВИТЬ ОБРАБОТКУ ИСКЛЮЧЕНИЙ, ПРОВЕРКУ ПОТОКА И СИГНАЛИЗАЦИЮ О ПЕРЕГРЕВЕ
//если есть EN сигнал, но датчик потока ничего не показал за 2 секунды - сигнализируем зумером (что то с помпой/датчиком)
//если температура жидкости превысила 40 градусов тоже сигналим (что то с кулерами, в помещении потеплело, или на MAN режиме не правильная мощность)

}//LOOP

void disp_print() {
lcd.setCursor(0,0); lcd.print("Tw:");
lcd.setCursor(3,0); lcd.print(water_temp); lcd.write(223);
lcd.setCursor(0,1); lcd.print("Flow:    "); lcd.setCursor(5,1); lcd.print(l_hour);
lcd.setCursor(9, 0); lcd.print("M:");
lcd.setCursor(11, 0); lcd.print(mode_desc);
lcd.setCursor(9, 1); lcd.print("P:   "); lcd.setCursor(12, 1); lcd.print(pwm_pow);
lcd.setCursor(14, 1); lcd.print("%");
}



