//COOLING STATION
// Arduino Nano board
#include <LiquidCrystal_I2C.h>
#include <GyverDS18.h>             // GyverDS18 v1.1.2 DS18B20 Temp Sensor lib
#include <EncButton.h>
#include <EEPROM.h>

#define EN_PIN A7
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

// ЗАВИСИМОСТЬ СКОРОСТИ КУЛЕРОВ ОТ ТЕМПЕРАТУРЫ ВОДЫ
// ЛИНЕЙНЫЙ MAP() НЕ ПОДХОДИТ

void flow () // Interrupt function
{ 
  flow_frequency++;
}

LiquidCrystal_I2C lcd(0x27, 16, 2); // DISPLAY
EncButton<EB_TICK, S1_PIN, S2_PIN, BTN_PIN> enc; //ENCODER
GyverDS18Single sensor_water(TW_PIN); // DS18B29 WATER TEMP

int pwm_pow;         // PWM %
bool pump_on = 0;
bool zoom_on = 0;
bool error_t_sensor = 0; 
float water_temp;    // WATER TEMP
float flow_rate;     // FLOW 
String mode_desc;    // MODE DESCRIPTION

void setup() {

Serial.begin(9600); 

pinMode(EN_PIN, INPUT);    // СИГНАЛ ВКЛЮЧЕНИЯ ПОМПЫ АНАЛОГОВЫЙ до 3.3В С ПЛАТЫ УПРАВЛЕНИЯ на ESP32
pinMode(BTN_PIN, INPUT);   // КНОПКА ЭНКОДЕРА
pinMode(S1_PIN, INPUT);    // ЭНКОДЕР S1
pinMode(S2_PIN, INPUT);    // ЭНКОДЕР S2
pinMode(PWM_PIN, OUTPUT);  // ШИМ НА КУЛЕРЫ
digitalWrite(PWM_PIN, 0);  // ВЫСТАВЛЯЕМ 0 на ШИМ
pinMode(PUMP_PIN, OUTPUT); // МОСФЕТ ПОМПЫ
pinMode(ZOOM_PIN, OUTPUT); // ЗУМЕР
digitalWrite(ZOOM_PIN, 1); // ВЫСТАВЛЯЕМ 1 на ZOOM (Используется LOW LEVEL BOOZER)
pinMode(FLOW_PIN, INPUT);  // ПРЕРЫВАНИЯ ОТ ДАТЧИКА ПОТОКА
pinMode(13, OUTPUT);       // ДИОД НА ПЛАТЕ ARDUINO

attachInterrupt(0, flow, RISING);
currentTime = millis();
cloopTime = currentTime;

lcd.init();
lcd.backlight();

lcd.setCursor(0, 0); lcd.print("BLACK BOX V6.2 ");
delay(1000);
lcd.setCursor(0, 1); lcd.print("COOLING STATION");
delay(1500);
lcd.clear();
}

void loop() {
enc.tick();

// ПОЛУЧАЕМ ТЕМПЕРАТУРУ РАЗ В СЕКУНДУ, обнуляем если датчик не ответил
static uint32_t tmr_temp;
if (millis() - tmr_temp >= 1000) {
  tmr_temp = millis();
if (sensor_water.readTemp()) {
    water_temp = sensor_water.getTemp();
    sensor_water.requestTemp();
}
else {
  sensor_water.requestTemp();
  water_temp = 0.00;
 }
}


if (enc.press()) {
if (mode < 3) {mode = mode + 1;}
if (mode == 3) {mode = 0;} 
}

if (mode == 0) { mode_desc = "RAUTO"; }
if (mode == 1) { mode_desc = "MAN  "; }
if (mode == 2) { mode_desc = "PAUTO"; }
if (enc.right()) {
  if (mode == 1 && error_t_sensor == 0) { pwm_pow = constrain(pwm_pow + 1, 0, 100); }
}
if (enc.left()) {
  if (mode == 1 && error_t_sensor == 0) { pwm_pow = constrain(pwm_pow - 1, 0, 100); }
}

//МОЩНОСТЬ КУЛЕРОВ НА АВТОМАТИЧЕСКИХ РЕЖИМАХ
static uint32_t tmr_cool;
if (millis() - tmr_cool >= 1000) {
  tmr_cool = millis();
  if (mode == 0 || mode == 2 && error_t_sensor == 0) {
  if (int(water_temp) < 25 ) { pwm_pow = 0; }
  if (int(water_temp) >= 36) { pwm_pow = 100; }
// ПО МАССИВУ ОПРЕДЕЛЯЕМ PWM КУЛЕРОВ ОТ ТЕМПЕРАТУРЫ
  if (int(water_temp) >= 25 && int(water_temp) < 36) {
  pwm_pow = map(water_temp, 25, 35, 20, 90);
   }
  }
 }

//ОПРАШИВАЕМ ENABLE НА ПОМПУ
static uint32_t tmr_pump;
if (millis() - tmr_pump >= 500) {
  tmr_pump = millis();
if (analogRead(EN_PIN) > 100 ) { 
  digitalWrite(PUMP_PIN, 1); 
  pump_on = 1;
   }
else {
    if (mode == 2) {digitalWrite(PUMP_PIN, 1);
    pump_on = 1; }
    else {digitalWrite(PUMP_PIN, 0);
    pump_on = 0;
    } 
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

// СИГНАЛ ОБ ОШИБКЕ ПОТОКА
static uint32_t tmr_zoom;
if (millis() - tmr_zoom >= 1000) {
tmr_zoom = millis();
if (pump_on == 1 && l_hour == 0 ) { 
  if (zoom_on == 0) {  
  digitalWrite(ZOOM_PIN, 0);
  zoom_on = 1; }
  else {
    digitalWrite(ZOOM_PIN, 1);
    zoom_on = 0;}
  }
else { digitalWrite(ZOOM_PIN, 1); }
}

// СИГНАЛ ОБ ОШИБКЕ ДАТЧИКА
static uint32_t tmr_sens_err;
if (millis() - tmr_sens_err >= 1000) {
tmr_zoom = millis();
if (pump_on == 1 && water_temp == 0.00 ) { 
  pwm_pow = 80; // ЕСЛИ ОТКАЗАЛ ДАТЧИК ТЕМПЕРАТУРЫ, ВКЛЮЧАЕМ КУЛЕРЫ НА 80% в постоянном режиме
  error_t_sensor = 1;
  if (zoom_on == 0) {  
  digitalWrite(ZOOM_PIN, 0);
  zoom_on = 1; }
  else {
    digitalWrite(ZOOM_PIN, 1);
    zoom_on = 0;}
  }
else { digitalWrite(ZOOM_PIN, 1); 
      error_t_sensor = 0; }
}

}//LOOP

void disp_print() {
lcd.setCursor(0,0); lcd.print("Tw:");
lcd.setCursor(3,0); lcd.print(water_temp); lcd.write(223);
lcd.setCursor(0,1); lcd.print("Flow:    "); lcd.setCursor(5,1); lcd.print(l_hour);
lcd.setCursor(9, 0); lcd.print("M:");
lcd.setCursor(11, 0); lcd.print(mode_desc);
lcd.setCursor(9, 1); lcd.print("P:   "); lcd.setCursor(11, 1); lcd.print(pwm_pow);
lcd.setCursor(14, 1); lcd.print("%");
if (pump_on == 1) {lcd.setCursor(15, 1); lcd.print("*");}
else {lcd.setCursor(15, 1); lcd.print(" ");}
}



