// DOIT ESP32 DEVKIT V1 board
// БИБЛИОТЕКИ (LIBS)
#include <LiquidCrystal_I2C.h>     // LCD Display
#include <GyverDS18.h>             // GyverDS18 v1.1.2 DS18B20 Temp Sensor lib
#include <EncButton.h>             // Encoder
#include <EEPROM.h>                // EEPROM
#include <WiFi.h>                  // Wi-Fi
#include <Wire.h>                  // Wire BMP180
#include <Adafruit_BMP085.h>       // BMP180
#include <PZEM004Tv30.h>           // PZEM-004t 
// # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # #
// КОНСТАНТЫ ПИНОВ
#define PUMP_PIN 15                // ВКЛЮЧЕНИЕ ПОМПЫ (ПОДАЕТСЯ НА АНАЛОГОВЫЙ ПИН СИСТЕМЫ ОХЛАЖДЕНИЯ)
#define KL1_PIN 13                 // КЛАПАН 1
#define KL2_PIN 12                 // КЛАПАН 2
#define KL3_PIN 14                 // КЛАПАН 3 (не используется, но на плате есть MOSFET) 
#define CONT_PIN 27                // ТВЕРДОТЕЛЬНОЕ РЕЛЕ ПУСК КОНТАКТОРА
#define TC_PIN 26                  // DS18B20 КУБ
#define TD_PIN 33                  // DS18B20 ТСА/ДЕФЛЕГМАТОРА
#define TUO_PIN 32                 // DS18B20 УЗЛА ОТБОРА/ЦАРГИ
#define TS_PIN 25                  // DS18B20 СИСМИСТОРА
#define BTN_PIN 5                  // КНОПКА ЭНКОДЕРА
#define S1_PIN 19                  // S1 ЭНКОДЕРА
#define S2_PIN 18                  // S2 ЭНКОДЕРА
#define ZOOM_PIN 4                 // ZOOMER
#define MQ3_PIN 23                 // MQ3 СЕНСОР ПАРОВ СПИРТА (РАБОТАЕТ ПО high/low уровням с предварительной настройкой)
#define PZEM_RX_PIN 3              // RX UART0 PZEM-004
#define PZEM_TX_PIN 1              // TX UART0 PZEM-004
#define PZEM_SERIAL Serial         // Serial   PZEM-004
// # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # #
// ГРАНИЧНЫЕ УСЛОВИЯ
#define P_START_UO 75              // Включение помпы по температуре царги/узла отбора
#define P_START_C  75              // Включение помпы по температуре в кубе
// # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # #
// ДАННЫЕ Wi-Fi СЕТИ 2.4GHz 
const char* ssid = "JHOME_2GHz";
const char* password = "JurHome22NeT";
WiFiServer server(80);                                    // http сервер на 80-ом порту
LiquidCrystal_I2C lcd(0x27, 20, 4);                       // Дисплей 20х4
PZEM004Tv30 pzem(PZEM_SERIAL, PZEM_RX_PIN, PZEM_TX_PIN);  // Измеритель мощности PZEM-004t
EncButton<EB_TICK, S1_PIN, S2_PIN, BTN_PIN> enc;          // Энкодер 
Adafruit_BMP085 bmp;                                      // Датчик атмосферного давления
// DS18B20 ТЕРМО ДАТЧИКИ
GyverDS18Single sensor_cube(TC_PIN); // куб
GyverDS18Single sensor_out(TUO_PIN); // узел отбора/царга
GyverDS18Single sensor_defl(TD_PIN); // ТСА, или дефлегматор
GyverDS18Single sensor_sim(TS_PIN);  // симистор
// ОПИСЫВАЕМ ЗАДАЧУ ДЛЯ CPU 0
TaskHandle_t Task1;
// ПЕРЕМЕННЫЕ ПО ТИПАМ
float cube_temp;                  // ТЕМПЕРАТУРА В КУБЕ
float defl_temp;                  // ТЕМПЕРАТУРА ДЕФЛЕГМАТОРА
float uo_temp;                    // ТЕМПЕРАТУРА УЗЛА ОТБОРА
float uo_temp_fix;                // ТЕМПЕРАТУРА ФИКСАЦИИ ОТБОРА (КОРРЕКТИРУЕМАЯ)
float sim_temp;                   // ТЕМПЕРАТУРА СИМИСТОРА
float delt;                       // ДЕЛЬТА ЗАВЫШЕНИЯ ТЕМПЕРАТУРЫ
float pr_temp;                    // ТЕМПЕРАТУРА КИПЕНИЯ СПИРТА ПРИ АТМ ДАВЛЕНИИ
float press_delta = 0;            // ТЕМПЕРАТУРНАЯ РАЗНИЦА ПО ДАВЛЕНИЮ В ПРОЦЕССЕ
float press_init  = 0;            // НАЧАЛЬНОЕ ДАВЛЕНИЕ НА МОМЕНТ ФИКСАЦИИ ТЕМПЕРАТУРЫ
float press_curr  = 0;            // ТЕКУЩЕЕ ДАВЛЕНИЕ НА МОМЕНТ РАСЧЕТА
float voltage;                    // НАПРЯЖЕНИЕ ПИТАЮЩЕЙ СЕТИ
float current;                    // ТОК ЧЕРЕЗ НАГРУЗКУ
float power;                      // МОЩНОСТЬ НА НАГРУЗКЕ
float energy;                     // ЗАТРАТЫ ЭНЕРГИИ
float frequency;                  // ЧАСТОТА СЕТИ
float watt_pow;                   // ВЫЧИСЛЯЕМАЯ МОЩНОСТЬ ПО % ОТ МОЩНОСТИ ТЭН
float ten_init_f_pow;             // ВЫЧЕСЛЯЕМАЯ МОЩНОСТЬ
// # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # #
int mode = 4;                     // РЕЖИМ РАБОТЫ. ПО УМОЛЧАНИЮ 4(MANUAL)
int ten_init_pow;                 // НОМИНАЛЬНАЯ МОЩНОСТЬ ТЭНА
int ten_pow;                      // ЗАДАНИЕ МОЩНОСТИ ТЭНА
int dimmer;                       // ДИММЕР. ХРАНИТ ВРЕМЯ ВКЛ СИМИСТОРА В мкс ОТ ПРОХОЖДЕНИЯ НУЛЯ СИНУСОИДОЙ
int count_stab;                   // СЧЕТЧИК СТАБИЛИЗАЦИИ
int cnt_stab;                     // СЧЕТЧИК СТАБИЛИЗАЦИИ в мин
int count_head;                   // СЧЕТЧИК ОТБОРА ГОЛОВ
int cnt_head;                     // СЧЕТЧИК ОТБОРА ГОЛОВ в мин
int count_body;                   // СЧЕТЧИК ВРЕМЕНИ РАБОТЫ ОТБОРА ТЕЛА
int cnt_body;                     // СЧЕТЧИК ВРЕМЕНИ РАБОТЫ ОТБОРА ТЕЛА в мин
int xflag_count;                  // СЧЕТЧИК ПРЕВЫШЕНИЙ ТЕМПЕРАТУРЫ УО
int head_time;                    // ВРЕМЕНЯ ОТБОРА ГОЛОВ (минуты)
int stab_time;                    // ВРЕМЯ СТАБИЛИЗАЦИИ (минуты)
int k1_per;                       // ПЕРИОД РАБОТЫ КЛАПАНА 1 (сек)
int k2_per;                       // ПЕРИОД РАБОТЫ КЛАПАНА 2 (сек)
int k1_time;                      // ВРЕМЯ ОТКРЫТИЯ КЛАПАНА 1 (мс)
int k2_time;                      // ВРЕМЯ ОТКРЫТИЯ КЛАПАНА 2 (мс)
int k1_per2;                      // ПЕРИОД КЛАПАНА 1 НА ДОБОРЕ ГОЛОВ (сек)
int k1_time2;                     // ВРЕМЕНИ ОТКРЫТИЯ КЛАПАНА 1 НА ДОБОРЕ ГОЛОВ (мс) 
int decr;                         // ДЕКРЕМЕНТ СНИЖЕНИЯ СКОРОСТИ ОТБОРА, УВЕЛИЧЕНИЕ ПЕРИОДА РАБОТЫ КЛАПАНА (сек)
int re_pwr_stab;                 // % МОЩНОСТИ ТЭН В НАЧАЛЕ РЕКТИФИКАЦИИ
int re_pwr_work;                   // % МОЩНОСТИ ТЭН В КОНЦЕ РЕКТИФИКАЦИИ  
int ps_pwr_start;                 // % МОЩНОСТИ ТЭН POTSTILL НАЧАЛО
int ps_pwr_end;                   // % МОЩНОСТИ ТЭН POTSTILL КОНЕЦ
int man_pwr;                      // % МОЩНОСТИ НА РУЧНОМ РЕЖИМЕ 
int rpower;                       // % МОЩНОСТИ ТЭН ДЛЯ РАЗГОНА КУБА
int ptr;                          // УКАЗАТЕЛЬ В МЕНЮ УСТАНОВОК
int start_stop = 0;               // ФЛАГ СТАРТ/СТОП
int fail_c = 99;                  // АВАРИЙНЫЙ СТОП ПО ТЕМПЕРАТУРЕ КУБА 
int fail_d = 55;                  // АВАРИЙНЫЙ СТОП ПО ТЕМПЕРАТУРЕ ДЕФЛЕГМАТОРА
int sim_fail_temp = 65;           // ПРЕДЕЛЬНАЯ ТЕМПЕРАТУРА СИМИСТОРА
int ps_stop_temp;                 // ТЕМПЕРАТУРА ОСТАНОВКИ НА POTSTILL
int zoom_per = 500;               // ПЕРИОД ЗУМЕРА МС
int alarm_counter = 0;            // СЧЕТЧИК СЕКУНД ПРЕДУПРЕЖДЕНИЯ ОБ АВАРИИ ПЕРЕД ОТКЛЮЧЕНИЕМ
int bmp_press;                    // АТМОСФЕРНОЕ ДАВЛЕНИЕ В мм рт.ст.
int ten_pow_delt = 0;             // ВЫЧИСЛЯЕМАЯ ДЕЛЬТУ ПО МОЩНОСТИ в %
int ten_pow_calc = 0;             // ВЫЧИСЛЯЕМАЯ КОРРЕКТИРОВКА МОЩНОСТИ В %
int tuo_ref;                      // ТЕМПЕРАТУРА ЦАРГИ/УЗЛА_ОТБОРА ДЛЯ СТАРАТ РЕЖИМА СТАБИЛИЗАЦИИ 
int overtemp_limit = 5;           // ЛИМИТ ЧИСЛА ПРЕВЫШЕНИЙ ПО ТЕМПЕРАТУРЕ НА RE_1KL, RE_2KL
// ФЛАГИ АВАРИЙ
bool alarm_tsa = 0;               // ФЛАГ ОШИБКИ ПО ТЕМПЕРАТУРЕ ТСА
bool alarm_cube = 0;              // ФЛАГ ОШИБКИ ПО ТЕМПЕРАТУРЕ КУБА
bool alarm_mq3 = 0;               // ФЛАГ ОШИБКИ ПО MQ3 ДАТЧИКУ ПАРОВ СПИРТА
bool alarm_power = 0;             // ФЛАГ ОШИБКИ ПО МОЩНОСТИ
bool alarm_sim = 0;               // ФЛАГ ОШИБКИ ПО ПЕРЕГРЕВУ СИМИСТОРА
bool alarm_t_sensors = 0;         // ФЛАГ ОШИБКИ ПО ДАТЧИКАМ ТЕМПЕРАТУРЫ
bool alarm_sim_t_sensor = 0;      // ФЛАГ ПО ДАТЧИКУ СИМИСТОРА
bool alarm_all = 0;               // ОБЩИЙ ФЛАГ АВАРИИ, СРАЗУ ОСТАНАВЛИВАЕТ ПРОЦЕСС  
// ДОПОЛНИТЕЛЬНЫЕ ФЛАГИ
bool bmp_err = 0;                 // ФЛАГ ПОДКЛЮЧЕНИЯ BMP180 СЕНСОРА. 
bool adv_disp = 0;                // ДОП ЭКРАН ВЛЕВО
bool err_disp = 0;                // ПОКАЗ ЭКРАНА ОШИБОК
bool is_set = 0;                  // ФЛАГ УСТАНОВКИ ЗНАЧЕНИЯ ПЕРМЕННОЙ В МЕНЮ
bool in_menu = 0;                 // ФЛАГ ПОПАДАНИЯ В МЕНЮ УСТАНОВОК
bool tflag = 0;                   // ФЛАГ ФИКСАЦИИ ТЕМПЕРАТУРЫ УО/ЦАРГИ
bool xflag = 0;                   // ФЛАГ ЗАВЫШЕНИЯ ТЕМПЕРАТУРЫ УО/ЦАРГИ
bool zoom_enable = 1;             // ВКЛ/ОТКЛ ЗУМЕРА 
bool mq3_enable = 0;              // ВКЛ/ОТКЛ ДАТЧИКА ПАРОВ СПИРТА
bool pow_stab = 1;                // ВКЛ/ОТКЛ СТАБИЛИЗАЦИИ МОЩНОСТИ
// СТРОКОВЫЕ
String submode;                  // ИНДИКАТОР ПОДРЕЖИМА РАБОТЫ РЕКТИФИКАЦИИ
String mode_desc;                // ОПИСАНИЕ РЕЖИМА ДЛЯ ДИСПЛЕЯ
String start_desc;               // ОПИСАНИЕ СТАРТ/СТОП
String html_page;                // СТРАНИЦА WEB СЕРВЕРА
String err_desc;                 // СТРОКА ОШИБКИ
// ДВУМЕРНЫЙ МАССИВ СТРОК МЕНЮ УСТАНОВОК. РАЗБИТ ПО ЭКРАНАМ(СТОЛБЦЫ)
String menu_settings[4][7] = 
{
{"K1 CYCLE 1:   ","K2 CYCLE :    ","DELTA TEMP  : ","PS PWR START: ","MODE     :   " ,"ERR CUBE TEMP:","MQ3 SENSOR EN:"},
{"K1 TIME  1:   ","K2 TIME  :    ","DECREMENT   : ","PS PWR END  : ","WORK/STOP:    ","ERR TSA TEMP :","POW STAB EN  :"},
{"K1 CYCLE 2:   ","STAB TIME:    ","RE PWR STAB:  ","PS STOP TEMP: ","TEN FULL POW: ","TUO STAB TEMP:","SAVE SETTINGS "},
{"K1 TIME  2:   ","HEAD TIME:    ","RE PWR WORK:  ","MANUAL POWER: ","TEN RAZG POW: ","ZOOMER ENABLE:","EXIT          "}
};
// ДВУМЕРНЫЙ МАССИВ ТЕМПЕРАТУР КИПЕНИЯ СПИРТА(второй столбец) ПРИ АТМОСФЕРНОМ ДАВЛЕНИИ(первый столбец) (мм.рт.ст.)
float alco_temps[68][2] =
{
{713,76.36}, {714,76.40}, {715,76.44}, {716,76.49}, {717,76.52}, {718,76.55}, {719,76.59}, {720,76.63}, {721,76.67}, {722,76.70},
{723,76.74}, {724,76.78}, {725,76.82}, {726,76.86}, {727,76.90}, {728,76.93}, {729,76.97}, {730,77.01}, {731,77.05}, {732,77.09},
{733,77.12}, {734,77.16}, {735,77.20}, {736,77.24}, {737,77.28}, {738,77.31}, {739,77.35}, {740,77.39}, {741,77.43}, {742,77.47},
{743,77.50}, {744,77.54}, {745,77.58}, {746,77.62}, {747,77.67}, {748,77.69}, {749,77.73}, {750,77.77}, {751,77.81}, {752,77.85},
{753,77.88}, {754,77.92}, {755,77.96}, {756,77.99}, {757,78.04}, {758,78.07}, {759,78.12}, {760,78.15}, {761,78.19}, {762,78.23},
{763,78.26}, {764,78.30}, {765,78.34}, {766,78.38}, {767,78.42}, {768,78.45}, {769,78.49}, {770,78.53}, {771,78.57}, {772,78.61},
{773,78.64}, {774,78.68}, {775,78.72}, {776,78.76}, {777,78.80}, {778,78.83}, {779,78.87}, {780,78.91} 
};
// МАССИВ ЗАДЕРЖЕК ДИММЕРА 0-100%(101 элемент). ДЛЯ БОЛЕЕ ЛИНЕЙНОГО ИЗМЕНЕНИЯ МОЩНОСТИ
int power_array[] = {9100, 8670, 8416, 8220, 8057, 7914, 7787, 7671, 7563, 7463, 
                     7369, 7279, 7194, 7112, 7033, 6957, 6884, 6813, 6743, 6676, 
                     6610, 6546, 6483, 6421, 6361, 6301, 6242, 6185, 6128, 6071, 
                     6016, 5961, 5907, 5853, 5800, 5747, 5695, 5643, 5591, 5539, 
                     5488, 5438, 5387, 5337, 5286, 5236, 5186, 5136, 5087, 5037, 
                     4987, 4937, 4888, 4838, 4788, 4738, 4688, 4637, 4587, 4536, 
                     4485, 4434, 4382, 4331, 4278, 4226, 4173, 4119, 4065, 4011, 
                     3955, 3900, 3843, 3786, 3727, 3668, 3608, 3547, 3485, 3421, 
                     3356, 3290, 3221, 3151, 3079, 3004, 2927, 2847, 2763, 2676, 
                     2584, 2487, 2383, 2271, 2150, 2016, 1864, 1686, 1466, 1158, 
                     500};
// # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # #
// SETUP
void setup() {
Serial.begin(9600);                         // Serial 0 для PZEM-004t
Serial.setTimeout(10);                      // Обязательно выставить таймаут, иначе ждет окончания передачи секунду. 
Serial2.begin(9600, SERIAL_8N1, 16, 17);    // Serial 2 на 16 и 17 GPIO для Arduino регулятора мощности
Serial2.setTimeout(10);                     // Таймаут так же выставляем по минимуму
pinMode(ZOOM_PIN, OUTPUT);                  // Выключить пищалку сразу
digitalWrite(ZOOM_PIN, 1);                  // Выставляем высокий уровень, так как используется LowLevel zoomer
lcd.init();                                 // Инициализация дисплея
lcd.backlight();                            // Подсветка дисплея
lcd.blink();                                // Включаем блинк для заставки
char line1[] = "BLACK BOX AUTO V7";       
char line2[] = "....................";
lcd.setCursor(0, 1);
  for (int i = 0; i < strlen(line1); i++) { lcd.print(line1[i]); delay(50); }
lcd.noBlink();
lcd.setCursor(0, 3);
  for (int i = 0; i < strlen(line2); i++) { lcd.print(line2[i]); delay(30); }
// ПИНЫ
pinMode(BTN_PIN, INPUT_PULLUP);             // кнопка энкодера
pinMode(PUMP_PIN, OUTPUT);                  // enable сигнал на помпу
pinMode(KL1_PIN, OUTPUT);                   // клапан 1
pinMode(KL2_PIN, OUTPUT);                   // клапан 2
pinMode(KL3_PIN, OUTPUT);                   // клапан 3 (не задействован в коде)
pinMode(CONT_PIN, OUTPUT);                  // реле контактора
pinMode(S1_PIN, INPUT);                     // S1 энкодера
pinMode(S2_PIN, INPUT);                     // S2 энкодера
pinMode(MQ3_PIN, INPUT);                    // MQ3 сенсор (работает по низкому уровню)
pinMode(2, OUTPUT);                         // ДИОД НА ПЛАТЕ ESP32 DEV KIT
// ПОДКЛЮЧАЕМСЯ К Wi-Fi СЕТИ 2.4GHz
WiFi.begin(ssid, password);
lcd.clear();
while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    lcd.setCursor(0,1);
    lcd.print("Connecting to WiFi:"); 
    lcd.setCursor(0,2); lcd.print(ssid);
  }
lcd.clear();
lcd.setCursor(0,0); lcd.print("Connected to:"); 
lcd.setCursor(0,1); lcd.print(ssid);
lcd.setCursor(0,3); lcd.print("IP: "); lcd.print(WiFi.localIP()); // Показать IP + проверка клапанов
digitalWrite(KL1_PIN, 1);
delay(300);
digitalWrite(KL1_PIN, 0);
delay(300);
digitalWrite(KL2_PIN, 1);
delay(300);
digitalWrite(KL2_PIN, 0);
delay(2000);
lcd.clear();
server.begin();   // Стартуем http сервер
// НАСТРОЙКА ЗАДАЧИ ДЛЯ CPU 0
xTaskCreatePinnedToCore(
Task1code,         //Функция для задачи
"Task1",           // Имя задачи
10000,             // Размер стека
NULL,              // Параметр задачи
0,                 // Приоритет (0 - низший)
&Task1,            // Выполняемая операция
0);                // Номер ядра, на котором она должна выполняться
//
lcd.clear();
// ИНИЦИАЛИЗИРУЕМ РАБОТУ С EEPROM 
EEPROM.begin(100);
// ИНИЦИАЛИЗИРУЕМ ДАТЧИК ДАВЛЕНИЯ с проверкой на ошибку. Если не проверить то вешает плату в "panic"
if (!bmp.begin()) {
  lcd.setCursor(0,1); lcd.print("ERR BMP SENSOR");
  bmp_err = 1;
  delay(2000);
  lcd.clear();
}
// ЧИТАЕМ НАСТРОЙКИ ИЗ EEPROM (float 4байта, int32 тоже 4 байта)
eeprom_read();
lcd.noBlink();
pzem.resetEnergy();   // Сброс счетчика энергии
}
//SETUP END

// # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # #
// ОСНОВНОЙ ЦИКЛ
void loop() {
// # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # #
// ПОЛУЧЕНИЕ ТЕМПЕРАТУР И ДАВЛЕНИЯ С ДАТЧИКОВ
static uint32_t tmr_temp;
if (millis() - tmr_temp >= 1000) {
tmr_temp = millis();
if (sensor_cube.readTemp()) { cube_temp = sensor_cube.getTemp(); sensor_cube.requestTemp(); }
else { sensor_cube.requestTemp(); } 
if (sensor_defl.readTemp()) { defl_temp = sensor_defl.getTemp(); sensor_defl.requestTemp(); }
else { sensor_defl.requestTemp(); }
if (sensor_out.readTemp()) { uo_temp = sensor_out.getTemp(); sensor_out.requestTemp(); }
else { sensor_out.requestTemp();  }
if (sensor_sim.readTemp()) { sim_temp = sensor_sim.getTemp(); sensor_sim.requestTemp(); }
else { sensor_sim.requestTemp();  }
// нужна проверка доступности датчика.. но только после изменений в железе и подаче нормального питающего напряжения
if (!bmp_err) { bmp_press = bmp.readPressure() * 0.00750062;}   // получаем давление в Па и переводим в мм ртутного столба
else { bmp_press = 0; }
// получаем температуру кипения спирта при текущем атм давлении. Только для сравнения 
get_temp_atm();
}// КОНЕЦ ОПРОСА ДАТЧИКОВ
// # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # #
// ENCODER
if (enc.right()) { // ОБРАБОТКА ПОВОРОТОВ ЭНКОДЕРА (ВПРАВО)
  if (!is_set && !in_menu && !adv_disp) {err_disp = 1; }
  if (!is_set && !in_menu && !err_disp && adv_disp) { adv_disp = 0; }
  if (!is_set && in_menu){ ptr = constrain(ptr + 1, 0, 28); }
    if (ptr > 27) {ptr = 0;} // циклическая прокрутка
    if (is_set && in_menu) {
        if (ptr == 0)  {k1_per = constrain(k1_per + 1, 0, 120);}
        if (ptr == 1)  {k1_time = constrain(k1_time + 10, 0, 5000);}
        if (ptr == 2)  {k1_per2 = constrain(k1_per2 + 1, 0, 120);}
        if (ptr == 3)  {k1_time2 = constrain(k1_time2 + 10, 0, 5000);}
//
        if (ptr == 4)  {k2_per = constrain(k2_per + 1, 0, 120);}
        if (ptr == 5)  {k2_time = constrain(k2_time + 10, 0, 5000);}
        if (ptr == 6)  {stab_time = constrain(stab_time + 1, 0, 60);}
        if (ptr == 7)  {head_time = constrain(head_time + 1, 0, 240);}
//
        if (ptr == 8)  {delt = constrain(delt + 0.01, 0, 2.0);}
        if (ptr == 9)  {decr = constrain(decr + 1, 0, 60);}
        if (ptr == 10) {re_pwr_stab = constrain(re_pwr_stab + 1, 0, 100);}
        if (ptr == 11) {re_pwr_work = constrain(re_pwr_work + 1, 0, 100);}
//
        if (ptr == 12) {ps_pwr_start = constrain(ps_pwr_start + 1, 0, 100);}
        if (ptr == 13) {ps_pwr_end = constrain(ps_pwr_end + 1, 0, 100);}
        if (ptr == 14) {ps_stop_temp = constrain(ps_stop_temp + 1, 0, 100);}
        if (ptr == 15) {man_pwr = constrain(man_pwr + 1, 0, 100);}
//
        if (ptr == 16) {mode = constrain(mode + 1, 1, 4);}
        if (ptr == 17) {start_stop = constrain(start_stop + 1, 0, 1);}
        if (ptr == 18) {ten_init_pow = constrain(ten_init_pow + 100, 0, 5000);}
        if (ptr == 19) {rpower = constrain(rpower + 1, 0, 100);}
//
        if (ptr == 20) {fail_c = constrain(fail_c + 1, 0, 100);}
        if (ptr == 21) {fail_d = constrain(fail_d + 1, 0, 100);}
        if (ptr == 22) {tuo_ref = constrain(tuo_ref + 1, 0, 100);}
        if (ptr == 23) {zoom_enable = constrain(zoom_enable + 1, 0, 1);}
//
        if (ptr == 24) {mq3_enable = constrain(mq3_enable + 1, 0, 1);}
        if (ptr == 25) {pow_stab = constrain(pow_stab + 1, 0, 1);}
      }
}
if (enc.left()) { // ОБРАБОТКА ПОВОРОТОВ ЭНКОДЕРА (ВЛЕВО)
  if (!is_set && !in_menu && !err_disp) { adv_disp = 1; }
  if (!is_set && !in_menu) {err_disp = 0; }
  if (!is_set && in_menu){ ptr = constrain(ptr - 1, -1, 27); }
     if (ptr < 0) {ptr = 27;} // циклическая прокрутка
     if (is_set && in_menu) {
        if (ptr == 0)  {k1_per = constrain(k1_per - 1, 0, 120);}
        if (ptr == 1)  {k1_time = constrain(k1_time - 10, 0, 5000);}
        if (ptr == 2)  {k1_per2 = constrain(k1_per2 - 1, 0, 120);}
        if (ptr == 3)  {k1_time2 = constrain(k1_time2 - 10, 0, 5000);}
//
        if (ptr == 4)  {k2_per = constrain(k2_per - 1, 0, 120);}
        if (ptr == 5)  {k2_time = constrain(k2_time - 10, 0, 5000);}
        if (ptr == 6)  {stab_time = constrain(stab_time - 1, 0, 60);}
        if (ptr == 7)  {head_time = constrain(head_time - 1, 0, 240);}
//
        if (ptr == 8)  {delt = constrain(delt - 0.01, 0, 2.0);}
        if (ptr == 9)  {decr = constrain(decr - 1, 0, 60);}
        if (ptr == 10) {re_pwr_stab = constrain(re_pwr_stab - 1, 0, 100);}
        if (ptr == 11) {re_pwr_work = constrain(re_pwr_work - 1, 0, 100);}
//
        if (ptr == 12) {ps_pwr_start = constrain(ps_pwr_start - 1, 0, 100);}
        if (ptr == 13) {ps_pwr_end = constrain(ps_pwr_end - 1, 0, 100);}
        if (ptr == 14) {ps_stop_temp = constrain(ps_stop_temp - 1, 0, 100);}
        if (ptr == 15) {man_pwr = constrain(man_pwr - 1, 0, 100);}
//
        if (ptr == 16) {mode = constrain(mode - 1, 1, 3);}
        if (ptr == 17) {start_stop = constrain(start_stop - 1, 0, 1);}
        if (ptr == 18) {ten_init_pow = constrain(ten_init_pow - 100, 0, 5000);}
        if (ptr == 19) {rpower = constrain(rpower - 1, 0, 100);}
//
        if (ptr == 20) {fail_c = constrain(fail_c - 1, 0, 100);}
        if (ptr == 21) {fail_d = constrain(fail_d - 1, 0, 100);}
        if (ptr == 22) {tuo_ref = constrain(tuo_ref - 1, 0, 100);}
        if (ptr == 23) {zoom_enable = constrain(zoom_enable - 1, 0, 1);}
//
        if (ptr == 24) {mq3_enable = constrain(mq3_enable - 1, 0, 1);}
        if (ptr == 25) {pow_stab = constrain(pow_stab - 1, 0, 1);}
      }
}

// ОБРАБОТЧИК НАЖАТИЯ КНОПКИ ЭНКОДЕРА
if (enc.press()) { 
  if (in_menu) { is_set = !is_set; }
  if (!in_menu) { in_menu = 1; } // ВХОД В МЕНЮ УСТАНОВОК
  if (is_set && ptr == 27 && in_menu) { in_menu = 0; is_set = 0; } // ВЫХОД ИЗ МЕНЮ
  if (is_set && ptr == 26 && in_menu) { eeprom_write(); } // ЗАПИСЬ ВСЕХ ЗНАЧИМЫХ ПЕРЕМЕННЫХ В EEPROM 
}
// # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # #
// ОСТАНОВКA НА РЕЖИМЕ POTSTILL
if (mode == 1 && int(cube_temp) >= ps_stop_temp) { stop_proc(); err_desc = "NORMAL PS STOP";} // ПО ТЕМП. В КУБЕ на POTSTILL
// # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # #
// ГРУППА БЕЗОПАСНОСТИ!
// ПРОВЕРКА АВАРИЙ
if (alarm_tsa || alarm_cube || alarm_power || alarm_mq3 || alarm_sim || alarm_t_sensors || alarm_sim_t_sensor ) {alarm_all = 1;}
else {alarm_all = 0; }
// ВКЛЮЧЕНИЕ ЗУМЕРА ПРИ АВАРИИ С ПЕРИОДОМ 'zoom_per'(мс) и проверкой на вкл в настройках
if (zoom_enable) {
static uint32_t tmr_alarm;
if ((millis() - tmr_alarm >= zoom_per) && alarm_all) {
tmr_alarm = millis();
if (digitalRead(ZOOM_PIN)) {
  digitalWrite(ZOOM_PIN, 0);}
else {digitalWrite(ZOOM_PIN, 1);}
  }
}
// ПРИНУДИТЕЛЬНО ВЫКЛЮЧАЕМ ЗУМЕР ЕСЛИ НЕТ АВАРИЙ
if (!alarm_all) {
  digitalWrite(ZOOM_PIN, 1); }
//СЧИТАЕМ СЕКУНДЫ ПОСЛЕ НАЧАЛА ПРЕДУПРЕЖДЕНИЯ ОБ АВАРИИ
static uint32_t tmr_alarm_counter;
if (millis() - tmr_alarm_counter >= 1000 && alarm_all) {
tmr_alarm_counter = millis();
alarm_counter = alarm_counter + 1; }
// ОБНУЛЯЕМ СЧЕТЧИК ЕСЛИ АВАРИЯ УШЛА ДО ТАЙМАУТА
if (!alarm_all) { alarm_counter = 0; }  
// ОБРАБОТКА ПЕРЕГРЕВА TSA
if (int(defl_temp) >= fail_d) { alarm_tsa = 1;
  if (alarm_counter >= 120) {stop_proc(); } }
else { alarm_tsa = 0; }
// ОБРАБОТКА ПЕРЕГРЕВА КУБА
if (int(cube_temp) >= fail_c) { alarm_cube = 1;
  if (alarm_counter >= 120) { stop_proc(); } }
else { alarm_cube = 0; }
// ОБРАБОТКА ДАТЧИКА ПАРОВ MQ3
if (mq3_enable) {
  if (!digitalRead(MQ3_PIN)) { alarm_mq3 = 1;
    if (alarm_counter >= 120) { stop_proc(); } }
else { alarm_mq3 = 0; } }
else { alarm_mq3 = 0; }    // ОТКЛЮЧАЕМ АВАРИЮ ПРИ ВЫКЛЮЧЕНИИ ДАТЧИКА В НАСТРОЙКАХ
// ОБРАБОТКА ПЕРЕГРЕВА СИМИСТОРА
if (sim_temp > sim_fail_temp) { alarm_sim = 1;
  if (alarm_counter >= 120) { stop_proc(); } }
else { alarm_sim = 0; }
// ОБРАБОТКА ОТКАЗА ДАТЧИКОВ
// СИМИСТР ПРОВЕРЯЕМ ВСЕГДА!
if (sim_temp == 0.00 && start_stop == 1) {
  alarm_sim_t_sensor = 1; 
  if (alarm_counter >= 120) { stop_proc(); } }
else {alarm_sim_t_sensor = 0;}
// ПРОВЕРЯЕМ ПОКАЗАНИЯ ДАТЧИКОВ ТОЛЬКО В РЕЖИМЕ РАБОТЫ
// НА ПРЯМОТОКЕ(POTSTILL) НУЖНЫ ТОЛЬКО 2 ДАТЧИКА, КУБ и ДЕФЛЕГМАТОР
if (mode == 1 && start_stop == 1 ) {
  if (cube_temp == 0.00 || defl_temp == 0.00) { 
  alarm_t_sensors = 1;
  if (alarm_counter >= 120) { stop_proc(); } }
  else {alarm_t_sensors = 0;} }
// НА РЕКТИФИКАЦИИ НУЖНЫ ВСЕ 3 ДАТЧИКА
if (mode == 2 && start_stop == 1 ) {
  if (cube_temp == 0.00 || defl_temp == 0.00 || uo_temp == 0.00 ) {
  alarm_t_sensors = 1;
  if (alarm_counter >= 120) { stop_proc(); } }
  else {alarm_t_sensors = 0;} }
if (mode == 3 && start_stop == 1 ) {
  if (cube_temp == 0.00 || defl_temp == 0.00 || uo_temp == 0.00 ) {
  alarm_t_sensors = 1;
  if (alarm_counter >= 120) { stop_proc(); } }
  else {alarm_t_sensors = 0;} }
// В РУЧНОМ РЕЖИМА АНАЛОГИЧНО ПРЯМОТОКУ(POTSTILL)
if (mode == 4 && start_stop == 1 ) {
  if (cube_temp == 0.00 || defl_temp == 0.00 ) {
  alarm_t_sensors = 1;
  if (alarm_counter >= 120) { stop_proc(); } }
  else {alarm_t_sensors = 0;} }
// сбрасываем ошибку по датчикам если не в режиме работы
if (start_stop == 0 ) {alarm_t_sensors = 0;}
// # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # #
// ОПИСАНИЕ РЕЖИМОВ ДЛЯ ДИСПЛЕЯ, СТАРТ/СТОП и ОШИБОК
if (mode == 1) { mode_desc = "PSTILL";}
if (mode == 2) { mode_desc = "REC1KL";}
if (mode == 3) { mode_desc = "REC2KL";}
if (mode == 4) { mode_desc = "MANUAL";}
if (start_stop == 1) {start_desc = "WORK";}
if (start_stop == 0) {start_desc = "STOP";}
if (alarm_cube)  {err_desc = "ERR CUBE TEMP ";}
if (alarm_tsa)   {err_desc = "ERR TSA TEMP  ";}
if (alarm_sim)   {err_desc = "ERR SIM TEMP  ";}
if (alarm_mq3)   {err_desc = "ERR MQ3 ALCO  ";}
if (alarm_power) {err_desc = "ERR POWER SET ";}
// # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # #
// Опрос PZEM-004 по Serial 0, быстрее чем раз в 1 сек не отадет изменения
static uint32_t tmr_pzem;
if (millis() - tmr_pzem >= 1000) {
  tmr_pzem = millis();
voltage = pzem.voltage();
if (isnan(voltage)) { voltage = 0;}
current = pzem.current();
if (isnan(current)) { current = 0;}
power = pzem.power();
if (isnan(power)) { power = 0;}
energy = pzem.energy();
if (isnan(energy)) { energy = 0;}
frequency = pzem.frequency();
if (isnan(frequency)) {frequency = 0;}
}
// # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # #
// ЗАДАНИЕ МОЩНОСТИ ТЭН ПО РЕЖИМАМ
// РЕЖИМ "POTSTILL"
static uint32_t tmr_ten_pow;
if (millis() - tmr_ten_pow >= 1000) { tmr_ten_pow = millis();      //  Таймер регулировки, 1 сек, достаточно при высокой инерции куба и при медленной обратной связи от PZEM-004. 
if (start_stop) {            // регулируем мощность только если начали процесс
digitalWrite(CONT_PIN, 1);   // включили контактор на ТЭН
// Разгон до 75 градусов в кубе
if (mode == 1 && cube_temp < 75 ) {
  ten_pow = rpower;   // не всегда нужна полная мощность, сделана регулируемой
  submode = "R"; }
// Плавная регулировка в процессе
if (mode == 1 && cube_temp >= 75) {
  ten_pow = map(cube_temp, 75, ps_stop_temp, ps_pwr_start, ps_pwr_end);
  submode = "P"; }
// РЕЖИМЫ РЕКТИФИКАЦИИ МОЩНОСТЬ ФИКСИРОВАННАЯ (сомнительный момент, со снижением концентрации спирта в кубе, скорее всего будет меняться объем испаряемого спирта в час)
// возможно стоит задать автоматическое приращение мощности в % к заданной начальной. Начальную принять для 80 в кубе, конечную для 97. 
// но тут надо почитать.. возможно количество испаряемого спирта не меняется
if ((mode == 2 || mode == 3) && uo_temp < tuo_ref) { ten_pow = rpower; submode = "R";}
if ((mode == 2 || mode == 3) && uo_temp >= tuo_ref && submode == "S") { ten_pow = re_pwr_stab; }
if ((mode == 2 || mode == 3) && uo_temp >= tuo_ref && submode == "H") { ten_pow = re_pwr_stab; }
if ((mode == 2 || mode == 3) && uo_temp >= tuo_ref && submode == "B") { ten_pow = re_pwr_work; } 
// РУЧНОЙ РЕЖИМ
if (mode == 4){ submode = "-";
ten_pow = man_pwr; }
// КОРРЕКЦИЯ МОЩНОСТИ
// ВЫЧИСЛЯЕМ В ВАТТАХ ЗАДАННУЮ В % МОЩНОСТЬ
ten_init_f_pow = float(ten_init_pow);  // для результата с запятой нужно преобразование переменной
watt_pow = (ten_init_f_pow / 100) * ten_pow;
// Регулировка мощности приращением/уменьшением % мощности через поправку к заданной. Только при включенном параметре стабилизации. 
if (pow_stab == 1) {
if (ten_pow > 10 && ten_pow < 90) {                              // Есть смысл регулировать только в пределах до 90%, дальше мощности просто неоткуда браться в плюс
  if ((int(watt_pow) - int(power)) > 15 ) {ten_pow_delt += 1;}   // Разброс в 15 Ватт компенсирует инерцию обратной связи от PZEM-004
  if ((int(power) - int(watt_pow)) > 15 ) {ten_pow_delt -= 1;}   // Аналогично в обратную сторону
 }
}
else {ten_pow_delt = 0;}  // сбрасываем поправку если отключили стабилизацию
// В целом процесс стабилизации занимает 2-5 секунд. 
ten_pow_calc = constrain(ten_pow + ten_pow_delt, 0, 100);        // Ограничиваем значение на всякий случай
// пишем в serial arduino Int число со значением задержки включения симистора из массива
Serial2.print(power_array[ten_pow_calc] );
// Arduino в блоке управления сисмистором само понимает и полностью выключает симистор при значении меньше 2%, и так же открывает полностью если выставлено 98%

}// конец старт/стоп
else { ten_pow = 0; Serial2.print(9100); // Если "STOP" то передаем минимальную мощность, чтобы не было искры в контакторе
  digitalWrite(CONT_PIN, 0); }            // Отключаем контактор после выставления минимальной мощности. 
}//КОНЕЦ ТАЙМЕРА УПРАВЛЕНИЯ МОЩНОСТЬЮ

// # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # #
// ОСНОВНАЯ ЛОГИКА РАБОТЫ РЕЖИМОВ РЕКТИФИКАЦИИ
// # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # #
if (start_stop) { // работаем клапанами только в режиме нагрева!
// СТАБИЛИЗАЦИЯ
// СЧЕТЧИК ВРЕМЕНИ РАБОТЫ ПРИ СТАБИЛИЗАЦИИ RE_1KL, RE_2KL 
static uint32_t tmr_stab;
if (millis() - tmr_stab >= 1000) { tmr_stab = millis();
if ((mode == 2 || mode == 3) && count_stab < (stab_time * 60) && uo_temp >= tuo_ref) { 
  count_stab = count_stab + 1; submode = "S"; 
  cnt_stab = count_stab / 60;}
}//КОНЕЦ РАБОТЫ СЧЕТЧИКА

// # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # #
// ОТБОР ГОЛОВ RE_2KL, RE_1KL. ПОКА СЧЕТКИК НЕ ДОЙДЕТ ДО ЗАДАННОГО ВРЕМЕНИ
if ((mode == 2 || mode == 3) && uo_temp > tuo_ref && count_stab >= (stab_time * 60) && count_head <= (head_time * 60)) {
    submode = "H";                                                        // ИНДИКАЦИЯ "ОТБОР ГОЛОВ"
    digitalWrite(KL2_PIN, 0);                                             // Принудительно закрываем клапан 2 
//СЧЕТЧИК ВРЕМЕНИ ОТБОРА
static uint32_t tmr_head;
if (millis() - tmr_head >= 1000) { tmr_head = millis();
    count_head = count_head + 1;   // Наращиваем по таймеру счетчик
    cnt_head = count_head / 60;
    tflag = 0; }                   // Сбрасываем флаг задания эталонной температуры      
kl1_work_cycle(); // РАБОТА КЛАПАНА 1
}// КОНЕЦ РЕЖИМА ОТБОРА ГОЛОВ 

// # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # #
// ОТБОР ПРОДУКТА НА РЕЖИМЕ RE_1KL(1 КЛАПАН)
if (mode == 2 ) {
fix_temp(); // ФИКСАЦИЯ ТЕМПЕРАТУРЫ ОТБОРА И НАЧАЛЬНОГО ДАВЛЕНИЯ
if (uo_temp > tuo_ref && count_stab >= (stab_time * 60) && count_head > (head_time * 60) && tflag) {
submode = "B"; // ИНДИКАЦИЯ "ОТБОР ТЕЛА"
static uint32_t tmr_body; // Счетчик времени отбора, для дисплея
if (millis() - tmr_body >= 1000) { 
  tmr_body = millis(); 
  count_body = count_body + 1; 
  cnt_body = count_body / 60; }  
check_tf();       //ПРОВЕРКА НА ЗАВЫШЕНИЕ ТЕМПЕРАТУРЫ 
kl1_work_cycle2(); // РАБОТА КЛАПАНА ОТБОРА 1 по параметрам для второго цикла
// нормальная остановка по превышению лимита 
if (xflag_count > overtemp_limit) { stop_proc(); err_desc = "NORMAL RE STOP";}
 }
} // КОНЕЦ РАБОТЫ ПО ОТБОРУ ПРОДУКТА НА РЕЖИМЕ RE_1KL

// # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # #
// ОТБОР ПРОДУКТА НА РЕЖИМЕ RE_2KL(2 КЛАПАНА)
if (mode == 3 ) {
fix_temp(); // ФИКСАЦИЯ ТЕМПЕРАТУРЫ ОТБОРА И НАЧАЛЬНОГО ДАВЛЕНИЯ
if (uo_temp > tuo_ref && count_stab >= (stab_time * 60) && count_head > (head_time * 60) && tflag) {
submode = "B"; // ИНДИКАЦИЯ "ОТБОР ТЕЛА"
static uint32_t tmr_body; // Счетчик времени отбора, для дисплея
if (millis() - tmr_body >= 1000) { 
  tmr_body = millis(); 
  count_body = count_body + 1; 
  cnt_body = count_body / 60; }  
kl2_work_cycle(); // РАБОТА КЛАПАНА ОТБОРА 2
// отбор голов из царги пастеризации в процессе отбора тела 
kl1_work_cycle2(); // РАБОТА КЛАПАНА ОТБОРА 1 СО СНИЖЕННОЙ СКОРОСТЬЮ ОТБОРА
// нормальная остановка по превышению лимита 
check_tf();       //ПРОВЕРКА НА ЗАВЫШЕНИЕ ТЕМПЕРАТУРЫ 
if (xflag_count > overtemp_limit) { stop_proc(); err_desc = "NORMAL RE STOP";}
 }
} // КОНЕЦ РАБОТЫ ПО ОТБОРУ ПРОДУКТА НА РЕЖИМЕ RE_2KL
} // КОНЕЦ РАБОТЫ ПО start_stop
else { //сбрасываем все счетчики и флаги если остановили нагрев! start_stop = 0  
count_body = 0;
cnt_body = 0;
submode = "-";
count_body = 0;
cnt_body = 0;
count_head = 0;
cnt_head = 0;
uo_temp_fix = 0;
xflag = 0;
tflag = 0;
xflag_count = 0;
}
// # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # #
// УПРАВЛЕНИЕ ПОМПОЙ НА РАЗНЫХ РЕЖИМАХ
static uint32_t tmr_pump;
if (millis() - tmr_pump >= 500) { tmr_pump = millis();
// POTSTILLs & MANUAL
if ((mode == 1 || mode == 4) && cube_temp >= P_START_C) { digitalWrite(PUMP_PIN, 1); }
if ((mode == 1 || mode == 4) && cube_temp < P_START_C) { digitalWrite(PUMP_PIN, 0); }
// RECTIFICATION
if ((mode == 2 || mode == 3) && uo_temp >= P_START_UO) { digitalWrite(PUMP_PIN, 1); }
if ((mode == 2 || mode == 3) && uo_temp < P_START_UO) { digitalWrite(PUMP_PIN, 0); }
}// КОНЕЦ УПРАВЛЕНИЯ ПОМПОЙ

// # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # #
// ОСНОВНОЙ ЭКРАН
static uint32_t tmr;
if (millis() - tmr >= 200 && !in_menu && !err_disp && !adv_disp) { tmr = millis();
main_screen();
}
// # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # #
// МЕНЮ УСТАНОВОК
static uint32_t tmr_menu;
if (millis() - tmr_menu >= 200 && in_menu){ tmr_menu = millis();
menu_screen();
}

// # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # #
// ЭКРАН ОШИБОК
static uint32_t tmr_err_disp;
if ((millis() - tmr_err_disp >= 200) && err_disp && !adv_disp && !in_menu) {
tmr_err_disp = millis();
disp_errors();
}

// # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # # #
// ДОП ЭКРАН ДЛЯ ВСЯКОГО
static uint32_t tmr_adv_disp;
if ((millis() - tmr_adv_disp >= 200) && !err_disp && adv_disp && !in_menu) {
tmr_adv_disp = millis();
disp_advanced();
}

// Индикация работы диодом на плате ESP32 DEV KIT
static uint32_t tmr_led;
if (millis() - tmr_led >= 500) { tmr_led = millis();
if (!digitalRead(2)) { digitalWrite(2,1);}
else {digitalWrite(2,0);}
}
}// Конец цикла loop()

/// ФУНКЦИИ
void main_screen() {    // Основной экран
lcd.noBlink();
// Вывод температуры куба
lcd.setCursor(0,0); lcd.print("Tc:");
lcd.setCursor(3,0); lcd.print(cube_temp); lcd.write(223); lcd.print(" ");
// Вывод температуры дефлегматора/ТСА
lcd.setCursor(0,1); lcd.print("Td:");
lcd.setCursor(3,1); lcd.print(defl_temp); lcd.write(223); lcd.print(" ");
// Вывод температуры узла отбора
lcd.setCursor(0,2); lcd.print("To:");
lcd.setCursor(3,2); lcd.print(uo_temp); lcd.write(223); lcd.print(" ");
// Вывод зафиксированной температуры узла отбора при ректификации по каждому режиму ректификации
lcd.setCursor(0,3); lcd.print("Tf:");
lcd.setCursor(3,3); lcd.print(uo_temp_fix); lcd.write(223); lcd.print(" ");
// Вывод режима работы
lcd.setCursor(9,3); lcd.print(" "); lcd.print(mode_desc); lcd.print(" ");
// Вывод мощности нагрева (%)
lcd.setCursor(9,0); lcd.print(" TEN:");
lcd.setCursor(14,0); lcd.print("   ");
lcd.setCursor(14,0); lcd.print(ten_pow);
lcd.setCursor(17,0); lcd.print("%");
lcd.setCursor(18,0); lcd.print("  ");
// Вывод темпертауры радиатора симистора
lcd.setCursor(9,1); lcd.print(" TS:");
lcd.setCursor(13,1); lcd.print(sim_temp); lcd.write(223); lcd.print(" ");
lcd.setCursor(19,1); lcd.print(" ");
//Вывод счетчика и индикатора подрежима
if (mode == 1 || mode == 4) { lcd.setCursor(9,2); lcd.print(" P/M  "); }
if (mode == 2 || mode == 3) { lcd.setCursor(9,2); lcd.print("      "); lcd.setCursor(10,2); lcd.print(submode); }
// Вывод счтетчиков по режимам работы
if (submode == "S") {lcd.setCursor(12,2); lcd.print(cnt_stab);}
if (submode == "H") {lcd.setCursor(12,2); lcd.print(cnt_head);}
if (submode == "B") {lcd.setCursor(12,2); lcd.print(cnt_body);}
if (submode == "R") {lcd.setCursor(12,2); lcd.print("N/A");}
// СТАРТ/СТОП
lcd.setCursor(15,2); lcd.print(" ");
if (start_stop) {lcd.setCursor(16,2); lcd.print(" WRK");}
else {lcd.setCursor(16,2); lcd.print(" STP");}
// ИНДИКАТОР ВКЛ/ОТКЛ ПОМПЫ
if (digitalRead(PUMP_PIN) == 1 ) {lcd.setCursor(17,3); lcd.print("PMP");}
if (digitalRead(PUMP_PIN) == 0 ) {lcd.setCursor(17,3); lcd.print("OFF");}
}

void menu_screen() { // Меню настроек, постраничный вывод в зависимости от указателя ptr который меняем поворотами энкодера
lcd.noBlink();
// PAGE1
if (ptr < 4) {
mprint(0);
pprint(ptr); // сдвиг вывода для указателя
lcd.setCursor(15,0); lcd.print("    s");
lcd.setCursor(15,0); lcd.print(k1_per);
lcd.setCursor(15,1); lcd.print("   ms");
lcd.setCursor(15,1); lcd.print(k1_time);
lcd.setCursor(15,2); lcd.print("    s");
lcd.setCursor(15,2); lcd.print(k1_per2);
lcd.setCursor(15,3); lcd.print("   ms");
lcd.setCursor(15,3); lcd.print(k1_time2);
}
// PAGE2
if (ptr > 3 && ptr < 8) {
mprint(1);
pprint(ptr - 4); // сдвиг вывода для указателя
lcd.setCursor(15,0); lcd.print("    s");
lcd.setCursor(15,0); lcd.print(k2_per);
lcd.setCursor(15,1); lcd.print("   ms");
lcd.setCursor(15,1); lcd.print(k2_time);
lcd.setCursor(15,2); lcd.print("    m");
lcd.setCursor(15,2); lcd.print(stab_time);
lcd.setCursor(15,3); lcd.print("    m");
lcd.setCursor(15,3); lcd.print(head_time);
}
// PAGE3
if (ptr > 7 && ptr < 12) {
mprint(2);
pprint(ptr - 8); // сдвиг вывода для указателя
lcd.setCursor(15,0); lcd.print("     ");
lcd.setCursor(15,0); lcd.print(delt); lcd.write(223);
lcd.setCursor(15,1); lcd.print("    s");
lcd.setCursor(15,1); lcd.print(decr);
lcd.setCursor(15,2); lcd.print("    %");
lcd.setCursor(15,2); lcd.print(re_pwr_stab);
lcd.setCursor(15,3); lcd.print("    %");
lcd.setCursor(15,3); lcd.print(re_pwr_work);
}
// PAGE4
if (ptr > 11 && ptr < 16) {
mprint(3);
pprint(ptr - 12); // сдвиг вывода для указателя
lcd.setCursor(15,0); lcd.print("    %");
lcd.setCursor(15,0); lcd.print(ps_pwr_start);
lcd.setCursor(15,1); lcd.print("    %");
lcd.setCursor(15,1); lcd.print(ps_pwr_end);
lcd.setCursor(15,2); lcd.print("     ");
lcd.setCursor(15,2); lcd.print(ps_stop_temp); lcd.write(223);
lcd.setCursor(15,3); lcd.print("    %");
lcd.setCursor(15,3); lcd.print(man_pwr);
}
// PAGE5
if (ptr > 15 && ptr < 20) {
mprint(4);
pprint(ptr - 16); // сдвиг вывода для указателя
lcd.setCursor(14,0); lcd.print("     ");
lcd.setCursor(14,0); lcd.print(mode_desc);
lcd.setCursor(15,1); lcd.print("     ");
lcd.setCursor(15,1); lcd.print(start_desc);
lcd.setCursor(15,2); lcd.print("     ");
lcd.setCursor(15,2); lcd.print(ten_init_pow); lcd.print("W");
lcd.setCursor(15,3); lcd.print("    %");
lcd.setCursor(15,3); lcd.print(rpower);
}
// PAGE6
if (ptr > 19 && ptr < 24) {
mprint(5);
pprint(ptr - 20); // сдвиг вывода для указателя
lcd.setCursor(15,0); lcd.print("     ");
lcd.setCursor(16,0); lcd.print(fail_c); lcd.write(223);
lcd.setCursor(15,1); lcd.print("     ");
lcd.setCursor(16,1); lcd.print(fail_d); lcd.write(223);
lcd.setCursor(15,2); lcd.print("     ");
lcd.setCursor(16,2); lcd.print(tuo_ref); lcd.write(223);
lcd.setCursor(15,3); lcd.print("     ");
lcd.setCursor(17,3); lcd.print(zoom_enable); 
  }
// PAGE7
if (ptr > 23 && ptr < 28) {
mprint(6);
pprint(ptr - 24); // сдвиг вывода для указателя
lcd.setCursor(15,0); lcd.print("     ");
lcd.setCursor(17,0); lcd.print(mq3_enable);
lcd.setCursor(15,1); lcd.print("     ");
lcd.setCursor(17,1); lcd.print(pow_stab);
lcd.setCursor(15,2); lcd.print("     ");
lcd.setCursor(15,3); lcd.print("     ");  
  }
}
// ЭКРАН ДОП ПАРАМЕТРОВ
void disp_advanced() {
// Напряжение сети
lcd.setCursor(0,0); lcd.print("V:        ");
lcd.setCursor(2,0); lcd.print(int(voltage));
// Ток через нагрузку
lcd.setCursor(0,1); lcd.print("I:        ");
lcd.setCursor(2,1); lcd.print(current);
// Мощность на нагрузке
lcd.setCursor(0,2); lcd.print("P:        ");
lcd.setCursor(2,2); lcd.print(int(power));
// Частота питающей сети
lcd.setCursor(0,3); lcd.print("f:        ");
lcd.setCursor(2,3); lcd.print(int(frequency));
// Атмосферное давление в мм ртутного столба
lcd.setCursor(10,0); lcd.print("ATM:      ");
lcd.setCursor(14,0); lcd.print(bmp_press);
// Референсная температура кипения спирта при данном давлении
lcd.setCursor(10,1); lcd.print("Tr:       ");
lcd.setCursor(13,1); lcd.print(pr_temp); lcd.write(223);
// Счетчки завышений температуры в царге/УО
lcd.setCursor(10,2); lcd.print("^Tf:      ");
lcd.setCursor(14,2); lcd.print(xflag_count);
// Референсное значение мощноси исходя из номинальной мощности ТЭН и % заданной
lcd.setCursor(10,3); lcd.print("Prf:      "); 
lcd.setCursor(14,3);lcd.print(int(watt_pow));
}

// РАБОТА ПЕРВОГО КЛАПАНА НА РЕЖИМЕ R1_KL и R2_KL - ПРИ ОТБОРЕ ГОЛОВ
void kl1_work_cycle() {
static uint32_t tmr_kl1_head; 
if ((millis() - tmr_kl1_head >= (k1_per * 1000))) { //асинхронная работа по таймеру
    digitalWrite(KL1_PIN, 1);
    tmr_kl1_head = millis(); } 
if (millis() - tmr_kl1_head >= k1_time) {
    digitalWrite(KL1_PIN, 0); }
}
//РАБОТА ВТОРОГО КЛАПАНА НА РЕЖИМЕ R2_KL, ОТБОР ТЕЛА
void kl2_work_cycle() {
static uint32_t tmr_kl2_body;
if ((millis() - tmr_kl2_body >= (k2_per * 1000)) && (uo_temp < (uo_temp_fix + delt))) {
    digitalWrite(KL2_PIN, 1);
    tmr_kl2_body = millis();
    xflag = 0; }              // Сбрасываем флаг завышения если температура пришла в норму после завышения
if (millis() - tmr_kl2_body >= k2_time) {
    digitalWrite(KL2_PIN, 0); }
}
// РАБОТА ПЕРВОГО КЛАПАНА В РЕЖИМЕ ДОБОРА ГОЛОВ НА RE_2KL, ИЛИ ОТБОРА ТЕЛА ПРИ R1_KL
void kl1_work_cycle2() {
static uint32_t tmr_kl1_past; 
if ((millis() - tmr_kl1_past >= (k1_per2 * 1000)) && (uo_temp < (uo_temp_fix + delt))) {
    digitalWrite(KL1_PIN, 1);
    tmr_kl1_past = millis(); 
    if (mode == 2) { xflag = 0; } // для режима R1_KL нужно сбрасывать флаг завышения температуры в работе клапана 1
    }
if (millis() - tmr_kl1_past >= k1_time2) {
    digitalWrite(KL1_PIN, 0); }
}
// ПРОВЕРКА НА ЗАВЫШЕНИЕ ФИКСИРОВАННОЙ ТЕМПЕРАТУРЫ
void check_tf(){
// если температура зашла выше uo_temp_fix + delt, убавляем скорость отбора, ставим флаг завышения, увеличиваем счетчик завышений.
// xflag сбрасывается при нормализации температуры
if ((uo_temp >= (uo_temp_fix + delt)) && !xflag) {
  k2_per = k2_per + decr;
  k1_per = k1_per + decr;
  xflag = 1;
  xflag_count = xflag_count + 1; }
}

// ФИКСАЦИЯ ТЕМПЕРАТУРЫ ОТБОРА И НАЧАЛЬНОГО ДАВЛЕНИЯ
void fix_temp() {
if (uo_temp > tuo_ref && count_stab >= (stab_time * 60) && count_head > (head_time * 60) && !tflag) {
   uo_temp_fix = uo_temp;
   tflag = 1;
   digitalWrite(KL1_PIN, 0); // Клапан закрываем на случай если возвращались добирать головы
   // Если датчик давления полностью исправен то забираем начальное давление в переменную
   if (!bmp_err && bmp_press > 712 && bmp_press > 781) { 
   press_init = float(bmp_press); }
   }
}

//ЦИКЛ loop() НА ВТОРОМ CPU. ВСЕ ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ДОСТУПНЫ
void Task1code( void * parameter) {
for(;;) {                                    // БЕСКОНЕЧНЫЙ FOR
enc.tick();                                  // "Тикаем" энкодером, очень улучшает его отклик
WiFiClient client = server.available();      // Проверяем сервер
if (client) {                                // Если есть клиентское подключение то формируем HTML страничку
html_page = "";
html_page = html_page + "<!DOCTYPE html><html translate=\"no\">";
html_page = html_page + "<head><title>AUTO V7 WEB</title><style> table, th, td { border: 2px solid gray; border-collapse: collapse; background-color: white;}</style></head>";
html_page = html_page + "<body style=\"background-color:black;\"> <h2 style=\"color: white;\"> BLACK BOX V7 </h2>";
// РЕЖИМ РАБОТЫ
html_page = html_page + "<h3 style=\"color: white;\">Mode</h3>";
html_page = html_page + "<table cellspacing=\"2\" cellpadding=\"2\">";
html_page = html_page + "<tr><td>Mode</td><td align=\"center\">" + mode_desc + "</td><td align=\"center\">" + submode + "</td></tr>";
if (!start_stop) {
html_page = html_page + "<tr><td>Start/Stop</td><td colspan=\"2\" align=\"center\" style=\"background-color: yellow;\">" + start_desc + "</td></tr>"; }
else {
html_page = html_page + "<tr><td>Start/Stop</td><td colspan=\"2\" align=\"center\" style=\"background-color: red;\">" + start_desc + "</td></tr>"; }
html_page = html_page + "</table>";
// ДАТЧИКИ ТЕМПЕРАТУРЫ И ДАВЛЕНИЯ
html_page = html_page + "<h3 style=\"color: white;\">Sensors</h3>";
html_page = html_page + "<table cellspacing=\"2\" cellpadding=\"2\">";
// Окрашиваем строку в зависимости от значения температуры
if (int(cube_temp) < 70) {
html_page = html_page + "<tr><td>Cube Temp</td><td align=\"left\" style=\"background-color: green;\">" + String(cube_temp) + "</td><td align=\"center\">&#176C</td></tr>";
}
if (int(cube_temp) >= 70 && int(cube_temp) < 96 ) {
html_page = html_page + "<tr><td>Cube Temp</td><td align=\"left\" style=\"background-color: yellow;\">" + String(cube_temp) + "</td><td align=\"center\">&#176C</td></tr>";
}
if (int(cube_temp) >= 96 ) {
html_page = html_page + "<tr><td>Cube Temp</td><td align=\"left\" style=\"background-color: red;\">" + String(cube_temp) + "</td><td align=\"center\">&#176C</td></tr>";
}
// для температуры в царге
if (int(uo_temp) < 80) {
html_page = html_page + "<tr><td>Tsarg Temp</td><td align=\"left\" style=\"background-color: green;\">" + String(uo_temp) + "</td><td align=\"center\">&#176C</td></tr>";
}
if (int(uo_temp) >= 80 && int(uo_temp) < 85) {
html_page = html_page + "<tr><td>Tsarg Temp</td><td align=\"left\" style=\"background-color: yellow;\">" + String(uo_temp) + "</td><td align=\"center\">&#176C</td></tr>";
}
if (int(uo_temp) >= 85) {
html_page = html_page + "<tr><td>Tsarg Temp</td><td align=\"left\" style=\"background-color: red;\">" + String(uo_temp) + "</td><td align=\"center\">&#176C</td></tr>";
}
// для температуры в ТСА/дефлегматоре
if (int(defl_temp) < 35) {
html_page = html_page + "<tr><td>TSA Temp</td><td align=\"left\" style=\"background-color: green;\">" + String(defl_temp) + "</td><td align=\"center\">&#176C</td></tr>";
}
if (int(defl_temp) >= 35 && int(defl_temp) < 40) {
html_page = html_page + "<tr><td>TSA Temp</td><td align=\"left\" style=\"background-color: yellow;\">" + String(defl_temp) + "</td><td align=\"center\">&#176C</td></tr>";
}
if (int(defl_temp) >= 40) {
html_page = html_page + "<tr><td>TSA Temp</td><td align=\"left\" style=\"background-color: red;\">" + String(defl_temp) + "</td><td align=\"center\">&#176C</td></tr>";
}
// температура симистора
if (int(sim_temp) < 40) {
html_page = html_page + "<tr><td>Sim Temp</td><td align=\"left\" style=\"background-color: green;\">" + String(sim_temp) + "</td><td align=\"center\">&#176C</td></tr>";
}
if (int(sim_temp) >= 40 && int(sim_temp) < 45) {
html_page = html_page + "<tr><td>Sim Temp</td><td align=\"left\" style=\"background-color: yellow;\">" + String(sim_temp) + "</td><td align=\"center\">&#176C</td></tr>";
}
if (int(sim_temp) >= 45) {
html_page = html_page + "<tr><td>Sim Temp</td><td align=\"left\" style=\"background-color: red;\">" + String(sim_temp) + "</td><td align=\"center\">&#176C</td></tr>";
}
html_page = html_page + "<tr><td>Atm.pressure</td><td align=\"left\">" + String(bmp_press) + "</td><td align=\"center\"> mm rt.st. </td></tr>";
html_page = html_page + "</table>";
// ПАРАМЕТРЫ КОНФИГУРАЦИИ
html_page = html_page + "<h3 style=\"color: white;\">Config Parameters</h3>";
html_page = html_page + "<table cellspacing=\"2\" cellpadding=\"2\">";
html_page = html_page + "<tr><td>K1 Cycle 1</td><td align=\"left\">" + String(k1_per) + "</td><td align=\"center\"> sec </td></tr>";
html_page = html_page + "<tr><td>K1 Time 1</td><td align=\"left\">" + String(k1_time) + "</td><td align=\"center\"> ms </td></tr>";
html_page = html_page + "<tr><td>K1 Cycle 2</td><td align=\"left\">" + String(k1_per2) + "</td><td align=\"center\"> sec </td></tr>";
html_page = html_page + "<tr><td>K1 Time 2</td><td align=\"left\">" + String(k1_time2) + "</td><td align=\"center\"> ms </td></tr>";
html_page = html_page + "<tr><td>K2 Cycle</td><td align=\"left\">" + String(k2_per) + "</td><td align=\"center\"> sec </td></tr>";
html_page = html_page + "<tr><td>K2 Time</td><td align=\"left\">" + String(k2_time) + "</td><td align=\"center\"> ms </td></tr>";
html_page = html_page + "<tr><td>Stab Time</td><td align=\"left\">" + String(stab_time) + "</td><td align=\"center\"> min </td></tr>";
html_page = html_page + "<tr><td>Head Time</td><td align=\"left\">" + String(head_time) + "</td><td align=\"center\"> min </td></tr>";
html_page = html_page + "<tr><td>Delta T</td><td align=\"left\">" + String(delt) + "</td><td align=\"center\">&#176C</td></tr>";
html_page = html_page + "<tr><td>Cycle Decrement</td><td align=\"left\">" + String(decr) + "</td><td align=\"center\"> sec </td></tr>";
html_page = html_page + "<tr><td>Stab Enable</td><td align=\"left\">" + String(pow_stab) + "</td><td align=\"center\"> - </td></tr>";
html_page = html_page + "<tr><td>Razgon Power</td><td align=\"left\">" + String(rpower) + "</td><td align=\"center\"> % </td></tr>";
html_page = html_page + "</table>";
// ПАРАМЕТРЫ ПОЛУЧАЕМЫЕ В РАБОТЕ
html_page = html_page + "<h3 style=\"color: white;\">Progress values</h3>";
html_page = html_page + "<table cellspacing=\"2\" cellpadding=\"2\">";
html_page = html_page + "<tr><td>Fixed Temp</td><td align=\"left\">" + String(uo_temp_fix) + "</td><td align=\"center\">&#176C</td></tr>";
html_page = html_page + "<tr><td>Init Fix Press</td><td align=\"left\">" + String(press_init) + "</td><td align=\"center\">mm rt.st.</td></tr>";
html_page = html_page + "<tr><td>Delta Press</td><td align=\"left\">" + String(press_init) + "</td><td align=\"center\">mm rt.st.</td></tr>";
// подсвечиваем завышения температуры
if (int(xflag_count) < 1) {
html_page = html_page + "<tr><td>Temp Spikes</td><td align=\"left\" style=\"background-color: green;\">" + String(xflag_count) + "</td><td align=\"center\"> - </td></tr>";
}
if (int(xflag_count) >= 1 && int(xflag_count) < 3) {
html_page = html_page + "<tr><td>Temp Spikes</td><td align=\"left\" style=\"background-color: yellow;\">" + String(xflag_count) + "</td><td align=\"center\"> - </td></tr>";
}
if (int(xflag_count) >= 3) {
html_page = html_page + "<tr><td>Temp Spikes</td><td align=\"left\" style=\"background-color: red;\">" + String(xflag_count) + "</td><td align=\"center\"> - </td></tr>";
}
html_page = html_page + "<tr><td>Spent Stab</td><td align=\"left\">" + String(cnt_stab) + "</td><td align=\"center\"> min </td></tr>";
html_page = html_page + "<tr><td>Spent Head</td><td align=\"left\">" + String(cnt_head) + "</td><td align=\"center\"> min </td></tr>";
html_page = html_page + "<tr><td>Spent Main</td><td align=\"left\">" + String(cnt_body) + "</td><td align=\"center\"> min </td></tr>";
html_page = html_page + "</table>";
// ПОКАЗАТЕЛИ ПИТАНИЯ
html_page = html_page + "<h3 style=\"color: white;\">Power</h3>";
html_page = html_page + "<table cellspacing=\"2\" cellpadding=\"2\">";
html_page = html_page + "<tr><td>Heat Power</td><td align=\"left\">" + String(ten_pow) + "</td><td align=\"center\"> % </td></tr>";
html_page = html_page + "<tr><td>Calc Power</td><td align=\"left\">" + String(watt_pow) + "</td><td align=\"center\"> Wt </td></tr>";
html_page = html_page + "<tr><td>Real Power</td><td align=\"left\">" + String(power) + "</td><td align=\"center\"> Wt </td></tr>";
html_page = html_page + "<tr><td>Voltage</td><td align=\"left\">" + String(voltage) + "</td><td align=\"center\"> V </td></tr>";
html_page = html_page + "<tr><td>Current</td><td align=\"left\">" + String(current) + "</td><td align=\"center\"> A </td></tr>";
html_page = html_page + "<tr><td>Energy</td><td align=\"left\">" + String(energy) + "</td><td align=\"center\"> kWt*h </td></tr>";
html_page = html_page + "</table>";
//
html_page = html_page + "</body></html>";
client.println(html_page);                     // выдем страницу клиенту
client.println();                            
client.stop();                                 // закрываем сессию
}
 }// Конец бесконечного FOR
}//Конец функции для CPU 0

// ВЫВОД МЕНЮ УСТАНОВОК ПО НОМЕРУ СТОЛБЦА
void mprint(int mcol) {
  lcd.noBlink();
  lcd.setCursor(1,0); lcd.print(menu_settings[0][mcol]);
  lcd.setCursor(1,1); lcd.print(menu_settings[1][mcol]);
  lcd.setCursor(1,2); lcd.print(menu_settings[2][mcol]);
  lcd.setCursor(1,3); lcd.print(menu_settings[3][mcol]);
}
// ВЫВОД УКАЗАТЕЛЯ МЕНЮ НАСТРОЕК
void pprint(int snum){
lcd.noBlink();
if (!is_set) {                          // МЕНЯЕМ ОТРИСОВКУ ЕСЛИ НЕ ЗАДАЕМ ПАРАМЕТР
  lcd.setCursor(0,0); lcd.print(" ");
  lcd.setCursor(0,1); lcd.print(" ");
  lcd.setCursor(0,2); lcd.print(" ");
  lcd.setCursor(0,3); lcd.print(" ");
  lcd.setCursor(0,snum); lcd.write(126); // СИМВОЛ СТРЕЛОЧКИ ВПРАВО
}
else {                                 // МЕНЯЕМ ОТРИСОВКУ ЕСЛИ ЗАДАЕМ ПАРАМЕТР
  lcd.setCursor(0,0); lcd.print(" ");
  lcd.setCursor(0,1); lcd.print(" ");
  lcd.setCursor(0,2); lcd.print(" ");
  lcd.setCursor(0,3); lcd.print(" ");
  lcd.setCursor(0,snum); lcd.print("*");
   }
}

// ЗАПИСЬ ПЕРЕМННЫХ В EEPROM, В ESP32 INT весит 4 байта, так же как и float
void eeprom_write() {
if (EEPROM.readFloat(0) != delt)        { EEPROM.writeFloat(0,  delt); }
if (EEPROM.readInt(4)  != k1_per)       { EEPROM.writeInt(4,  k1_per); }   
if (EEPROM.readInt(8)  != k2_per)       { EEPROM.writeInt(8,  k2_per); }
if (EEPROM.readInt(12) != k1_time)      { EEPROM.writeInt(12, k1_time); }
if (EEPROM.readInt(16) != k2_time)      { EEPROM.writeInt(16, k2_time); }
if (EEPROM.readInt(20) != stab_time)    { EEPROM.writeInt(20, stab_time); }
if (EEPROM.readInt(24) != head_time)    { EEPROM.writeInt(24, head_time); }
if (EEPROM.readInt(28) != decr)         { EEPROM.writeInt(28, decr); }
if (EEPROM.readInt(32) != re_pwr_stab)  { EEPROM.writeInt(32, re_pwr_stab); }
if (EEPROM.readInt(36) != re_pwr_work)  { EEPROM.writeInt(36, re_pwr_work); }
if (EEPROM.readInt(40) != ps_pwr_start) { EEPROM.writeInt(40, ps_pwr_start); }
if (EEPROM.readInt(44) != ps_pwr_end)   { EEPROM.writeInt(44, ps_pwr_end); }
if (EEPROM.readInt(48) != ps_stop_temp) { EEPROM.writeInt(48, ps_stop_temp); }
if (EEPROM.readInt(52) != man_pwr)      { EEPROM.writeInt(52, man_pwr); }
if (EEPROM.readInt(56) != fail_c)       { EEPROM.writeInt(56, fail_c); }
if (EEPROM.readInt(60) != fail_d)       { EEPROM.writeInt(60, fail_d); }
if (EEPROM.readInt(64) != ten_init_pow) { EEPROM.writeInt(64, ten_init_pow); }
if (EEPROM.readInt(68) != tuo_ref)      { EEPROM.writeInt(68, tuo_ref); }
if (EEPROM.readInt(72) != k1_per2)      { EEPROM.writeInt(72, k1_per2); }
if (EEPROM.readInt(76) != k1_time2)     { EEPROM.writeInt(76, k1_time2); }
if (EEPROM.readInt(80) != rpower)       { EEPROM.writeInt(80, rpower); }
if (EEPROM.readInt(84) != pow_stab)     { EEPROM.writeInt(84, pow_stab); }
EEPROM.commit();              // Обязательно COMMIT в память
//БИПАЕМ ЗУМЕРОМ 
digitalWrite(ZOOM_PIN, 0);
delay(200);
digitalWrite(ZOOM_PIN, 1);
is_set = 0;                  // и сразу возвращаемся к навигации по меню 
}
// Чтение переменных из EEPROM
void eeprom_read() { 
if (!isnan(EEPROM.readFloat(0))) { delt = EEPROM.readFloat(0); } // Проверяем есть ли данные, если есть то берем их в перменную
else {delt = 0.0; }                                              // Если нет данных (то есть = nan, это не число и нельзя логику использовать) то присваиваем default значение
k1_per = EEPROM.readInt(4);
k2_per = EEPROM.readInt(8);
k1_time = EEPROM.readInt(12);
k2_time = EEPROM.readInt(16);
stab_time = EEPROM.readInt(20);
head_time = EEPROM.readInt(24);
decr = EEPROM.readInt(28);
re_pwr_stab = EEPROM.readInt(32);
re_pwr_work = EEPROM.readInt(36);
ps_pwr_start = EEPROM.readInt(40);
ps_pwr_end = EEPROM.readInt(44);
ps_stop_temp = EEPROM.readInt(48);
man_pwr = EEPROM.readInt(52);
if (EEPROM.readInt(56) <= 0) { fail_c = 99; }         // Чтобы не сработали остановки при первом запуске с пустой памятью          
else { fail_c = EEPROM.readInt(56); }                 // Если данные есть - забиарем в температуру аварии по кубу
if (EEPROM.readInt(60) <= 0) { fail_d = 55; }         
else {fail_d = EEPROM.readInt(60); }                  // Если данные есть - забираем в температуру аварии по ТСА/дефлегматору
if (EEPROM.readInt(64) < 0) { ten_init_pow = 0; }     // Аналогично с номинальной мощностью ТЭН-а
else {ten_init_pow = EEPROM.readInt(64); }
if (EEPROM.readInt(68) <= 0) {tuo_ref = 73; }
else {tuo_ref = EEPROM.readInt(68); }
k1_per2 = EEPROM.readInt(72);
k1_time2 = EEPROM.readInt(76);
rpower = EEPROM.readInt(80);
pow_stab = EEPROM.readInt(84);
}

//ПОИСК ТЕМПЕРАТУРЫ КИПЕНИЯ СПИРТА ПО МАССИВУ И АТМ ДАВЛЕНИЮ. В Arduino IDE нет словарей, 
//пишем свой обработчик двумерного массива, где слева ключ(давление), справа значение(температура кипения). 
void get_temp_atm() {
  for (int i=0; i <= 67; i++) {
  if (float(bmp_press) == alco_temps[i][0] ) { pr_temp = alco_temps[i][1]; }
  if (bmp_press == 0 ) { pr_temp = 0;}
   }
}  // Нужно только для визуального сравнения с тем что по факту в УО/царге. Не используется для вычислений поправок и т.п. 

// ОСТАНОВКА ПРОЦЕССА 
void stop_proc() {
lcd.clear();
lcd.noBlink();
while (true) {    // бесконечный WHILE вешающий контроллер
    Serial2.print(9100);          // передаем минимальную мощность на Arduino
    digitalWrite(CONT_PIN, 0);    // выключаем контактор
    digitalWrite(PUMP_PIN, 0);    // выключаем помпу
    digitalWrite(KL1_PIN, 0);     // закрываем клапан 1
    digitalWrite(KL2_PIN, 0);     // закрываем клапан 2
    digitalWrite(KL2_PIN, 0);     // закрываем клапан 3
    disp_stats();                 // показываем статистику и ERROR MESSAGE 
if (zoom_enable) { // ПИЩИМ ЗУМЕРОМ ПОКА НЕ НАДОЕСТ(ЕСЛИ ОН ВКЛЮЧЕН)
  digitalWrite(ZOOM_PIN, 0);
  delay(1000);
  digitalWrite(ZOOM_PIN, 1);
  delay(1000);
}
else {delay(1000);}
  } // конец WHILE
} // конец функции остановки

// ВЫВОД ОТЧЕТА
void disp_stats() {
lcd.noBlink();
// Выводим на экран отчет с температурами, временем и т.д.
lcd.setCursor(0,0); lcd.print("Tc:");
lcd.setCursor(3,0); lcd.print(cube_temp); lcd.write(223);
lcd.setCursor(0,1); lcd.print("Td:");
lcd.setCursor(3,1); lcd.print(defl_temp); lcd.write(223);
lcd.setCursor(0,2); lcd.print("To:");
lcd.setCursor(3,2); lcd.print(uo_temp); lcd.write(223);
// Счетчики времени
lcd.setCursor(9,0); lcd.print("Ht:");
lcd.setCursor(12,0); lcd.print(cnt_head);
lcd.setCursor(9,1); lcd.print("Bt:");
lcd.setCursor(12,1); lcd.print(cnt_body);
// Зафиксированная в УО температура
lcd.setCursor(9,2); lcd.print("Tf:");
lcd.setCursor(12,2); lcd.print(uo_temp_fix);
// Описание ошибки
lcd.setCursor(0,3); lcd.print(err_desc);
// Режим/подрежим работы
lcd.setCursor(14,3); lcd.print(mode_desc);
}
// LДИАГНОСТИЧЕСКИЙ ЭКРАН ОШИБОК ПО КОМПОНЕНТАМ
void disp_errors() {
lcd.noBlink();
lcd.setCursor(0,0); lcd.print("CUB_ERR:"); lcd.print(alarm_cube);
lcd.setCursor(0,1); lcd.print("TSA_ERR:"); lcd.print(alarm_tsa);
lcd.setCursor(0,2); lcd.print("SIM_ERR:"); lcd.print(alarm_sim);
lcd.setCursor(0,3); lcd.print("MQ3_ERR:"); lcd.print(alarm_mq3);
lcd.setCursor(9,0); lcd.print("  POW_ERR:"); lcd.print(alarm_power);
lcd.setCursor(9,1); lcd.print("  T-SENS :"); lcd.print(alarm_t_sensors);
lcd.setCursor(9,2); lcd.print("  S-SENS :"); lcd.print(alarm_sim_t_sensor);
lcd.setCursor(9,3); lcd.print("  ********"); lcd.print("0");
}