#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <FreeRTOSConfig.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <String.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "code.h"
#include "lock_icon.h"
#include <ArduinoJson.h>
#include <SoftwareSerial.h>
#include <TinyGPSPlus.h>
#include <Preferences.h>
#include <math.h>

// #include "BluetoothSerial.h"

/* I/O */
#define BTN_R 34 // right button pin
#define BTN_L 13 // left button pin
#define BTN_C 25 // center button pin

/* GPS */
#define MY_TX 5
#define MY_RX 18
#define BAUD 9600

/* WIFI */
const char *ssid = "buddy";
const char *password = "password";

/* GENERAL */
#define WIDTH 240
#define HEIGHT 240
#define DEPTH 8    // color depth of panels
#define ROTATION 0 // display rotation
#define VERSION 1.0

/* DELAYS */
#define HELLO_DELAY 1000 / portTICK_PERIOD_MS // run time of greetings func
#define BTN_DELAY 150 / portTICK_PERIOD_MS    // delay when btn is pressed
#define TASK_DELAY 400 / portTICK_PERIOD_MS
#define UNDERLINE_SPEED 10 // define the speed of blinking line => x % UNDERLINE_SPEED == 0
#define BAR_HEIGHT 50      // height of the bottom and top bars
#define TRANSITION_DELAY_TICKS 10
#define BTN_TIMER_TRIGGER 30
#define SAVE_INTERVAL 1000
#define GPS_INTERVAL 2000
#define FALL_HOLD 3000
#define MPU_INTERVAL 300

/* SPRITE RELATED*/
#define PAGES 5   // number of tabs (speed, distance, ...)
#define BAR_X 70  // x pos of text in top/bottom bars
#define BAR_Y 20  // y pos -||-
#define MAIN_X 60 // x pos of text in the main screen
#define MAIN_Y 70 // y pos -||-
#define main_font 7
#define main_font_secondary 4
#define secondary_font 2

/* SETTINGS RELAED */
#define settings_cursor_width 10
#define settings_cursor_height 10

/* TRESHOLDS */
#define FALL_TRESHOLD 10.0
#define MOVEMENT_TRESHOLD 0.5

/* GPS transfer params */
#define LONG_PARAM "long"
#define LAT_PARAM "lat"
#define SPEED_PARAM "speed"
#define ALT_PARAM "alt"

/* OBJECTS */
AsyncWebServer server(80);
AsyncEventSource events("/data");

const size_t capacity = 10000;
DynamicJsonDocument doc(capacity);

Adafruit_MPU6050 mpu;
// BluetoothSerial SerialBT;
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite top_bar = TFT_eSprite(&tft);
TFT_eSprite main_screen = TFT_eSprite(&tft);
TFT_eSprite bottom_bar = TFT_eSprite(&tft);
TFT_eSprite start = TFT_eSprite(&tft);
Preferences preferences;
EspSoftwareSerial::UART myPort;
TinyGPSPlus gps;

/* Timers */
hw_timer_t *time_counter = NULL;

/* GLOBAL vars */
int started = 0;
int btn_state;
int last_state = 0;
int color = TFT_RED;
int current_page_id = 0;
int last_page_id = 0;
int show = 1;
int in_settings = 0;
int in_sub_settings = 0;
int bluetooth_on = 0;
String ip_address = "";
String header_HTTP;

int setting_categories_n = 4;
int display_setting_categories_n = 2;
char *setting_categories[] = {"BACK", "CONNECT", "MESH", "INFO"};
char *display_setting_categories[] = {"BACK", "QR_CODE"};
int settings_start_x = 55;
int settings_start_y = 35;
int settings_font = 4;
int settings_pad = 10;
int settings_step = tft.fontHeight(settings_font);
int settings_cursor_x = settings_start_x - settings_pad - 5;
int settings_cursor_y = settings_start_y + (tft.fontHeight(settings_font) / 2) - 7;
int settings_cursor_step = settings_step + settings_pad;
int settings_cursor_index = 0;
int finished = 0;
int interrupted = 0;
int locked = 0;
int gps_recieved = 0;

unsigned long lastExecutedMillis = 0;    // vairable to save the last executed time
unsigned long lastExecutedMillisGPS = 0; // vairable to save the last executed time
int data_index = 0;

int center_btn_timer = 0;
int center_btn_panic_timer = 0;
int stop_timer = 3;

int time_passed = 0; // time that passed from the start
int minutes = 0;     // elapsed minutes
int hours = 0;       // elapsed hours from the start
float seconds = 0;   // elapsed seconds from the start
float displayed_seconds = 0.00;
int transition_ticks = 0;

String speed_measure = "Km/h";
int max_speed = 0; // maximum recorded speed per ride

float angle_lr = 0; // angle when movinf left and right
float angle_fr = 0; // angle when rotating to your self and from your self (Slope)
int fall_detected = 0;
float fell_time = 0;
int angle_index = 0;
float last_read_angle = 0.0;
float angle_lr_filter[4] = {0, 0, 0, 0};
float angle_fr_filter[4] = {0, 0, 0, 0};
float locked_accel_x = -1.0;
float locked_accel_y = -1.0;
float locked_accel_z = -1.0;

/* TIMER VARS -> WiFi*/
unsigned long lastTime = 0;
unsigned long timerDelay = 10000;

float last_lat = 0;
float last_long = 0;
float current_lat = 0;
float current_long = 0;
unsigned age = 0;
double total_dist = 0.00;
float altitude = 0;
float speed = 0.00;
int bike_moved = 0;

/* PROTOTYPES */
void IRAM_ATTR Timer_ISR();

void setup()
{
  Serial.begin(115200);

  /* PREFERECES */
  preferences.begin("buddy", false);

  /* GPS */
  myPort.begin(9600, SWSERIAL_8N1, MY_RX, MY_TX);
  if (!myPort)
  { // If the object did not initialize, then its configuration is invalid
    Serial.println("Invalid EspSoftwareSerial pin configuration, check config");
    while (1)
    { // Don't continue with invalid configuration
      delay(100);
    }
  }
  Serial.println("GPS setup successfull");

  /* WiFi */
  Serial.print("Setting AP (Access Point)…");
  WiFi.enableAP(true);
  WiFi.softAP(ssid, password, 1, 1, 1);

  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  ip_address = String(IP);
  Serial.println(IP);

  server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    if(!finished){
      save_data();
      String json;
      serializeJson(doc, json);
      request->send(200, "application/json", json);
      json = String();
    }
    else{
      request->send(200, "text/plain", "OVER");
    } });

  server.on("/lock-device", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    if(!finished){
      top_bar.fillSprite(TFT_BLACK);
      main_screen.fillSprite(TFT_BLACK);
      bottom_bar.fillSprite(TFT_BLACK);
      locked = 1;
      request->send(200, "text/plain", "LOCKED");
    } });

  server.on("/unlock-device", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    if(!finished){
      top_bar.fillSprite(TFT_BLACK);
      main_screen.fillSprite(TFT_BLACK);
      bottom_bar.fillSprite(TFT_BLACK);
      locked = 0;
      bike_moved = 0;
      request->send(200, "text/plain", "UNLOCKED");
    } });

  server.on("/data_transferred", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    doc.clear();
    data_index = 0;
    request->send(200, "text/plain", "OK"); });

  server.on("/GPS", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    if(request->hasParam(LONG_PARAM)){
      gps_recieved = 1;
      current_long = (float)request->getParam(LONG_PARAM)->value();
    }
    if(request->hasParam(LAT_PARAM)){
      current_lat = (float)request->getParam(LAT_PARAM)->value();
    }
    if(request->hasParam(ALT_PARAM)){
      altitude = (float)request->getParam(altitude)->value();
    }
    if(request->hasParam(SPEED_PARAM)){
      speed = (float)request->getParam(altitude)->value();
    } });

  server.begin();

  /*MPU-6050*/
  Wire.setPins(4, 16);
  mpu.begin();
  mpu.setAccelerometerRange(MPU6050_RANGE_16_G);
  mpu.setGyroRange(MPU6050_RANGE_250_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  /* TFT_ESPI */
  tft.init();
  tft.setRotation(ROTATION);

  /* I/O */
  pinMode(BTN_L, INPUT);
  pinMode(BTN_C, INPUT);
  pinMode(BTN_R, INPUT);

  /* TIMERS */
  time_counter = timerBegin(0, 80, true);
  timerAttachInterrupt(time_counter, &Timer_ISR, true);
  timerAlarmWrite(time_counter, 1000, true);

  startup_code();
}

void loop()
{
  if (locked)
  {
    lock_screen();
  }
  else
  {
    bar_init();
    handle_page_display();
    handle_page_movement();
    check_for_halt();
    wifi_fnc();
    check_for_fall();
  }
  unsigned long currentMillis = millis();
  if (currentMillis - lastExecutedMillis >= SAVE_INTERVAL)
  {
    lastExecutedMillis = currentMillis; // save the last executed time
    if (interrupted)
    {
      save_data();
    }
    get_GPS();
    handle_gps();
  }
}

void save_data()
{
  // JsonObject data = doc.createNestedObject(String(data_index));
  doc["speed"] = get_speed();
  doc["distance"] = get_distance();
  doc["time_elapsed"] = (double)time_passed / 1000;
  doc["fall_detected"] = fall_detected;
  doc["lat"] = current_lat;
  doc["long"] = current_long;
  doc["alt"] = altitude;
  doc["alarm"] = bike_moved;
  data_index++;
}

/* SETUP funcs */
void IRAM_ATTR Timer_ISR()
{
  if (!locked)
  {
    time_passed += 1;
  }
}

void greeting()
{
  tft.drawString("Cycling Buddy v" + String(VERSION), 60, HEIGHT / 2, 2);
  vTaskDelay(HELLO_DELAY);
}

void startup_code()
{
  /* TFT_eSPI */
  top_bar.setColorDepth(DEPTH);
  top_bar.createSprite(WIDTH, BAR_HEIGHT);
  top_bar.setTextColor(TFT_WHITE, TFT_BLACK);

  main_screen.setColorDepth(DEPTH);
  main_screen.createSprite(WIDTH, (HEIGHT - (2 * BAR_HEIGHT)));
  main_screen.setTextColor(TFT_WHITE, TFT_BLACK);

  bottom_bar.setColorDepth(DEPTH);
  bottom_bar.createSprite(WIDTH, BAR_HEIGHT);
  bottom_bar.setTextColor(TFT_WHITE, TFT_BLACK);

  start.setColorDepth(DEPTH);
  start.createSprite(WIDTH, HEIGHT);

  tft.fillScreen(TFT_BLACK);
  greeting();
  tft.fillScreen(TFT_BLACK);
  start_screen();

  while (!started)
  {
    if (!in_settings)
    {
      start_screen();
    }
    else
    {
      settings_screen();
    }
  }
  timerAlarmEnable(time_counter); // start the timer when user clicks START
}

void start_screen()
{
  int pad = 8;

  int start_x = 80;
  int start_y = 150;
  String start_txt = "START";

  int start_arrow_x = start_x + (tft.textWidth(start_txt, 4) / 2);
  int start_arrow_y = start_y + pad + tft.fontHeight(4);

  int settings_x = 40;
  int settings_y = 80;
  String settings_txt = "SETTINGS";

  int settings_arrow_x = settings_x + tft.textWidth(settings_txt, 4) + 15;
  int settings_arrow_y = settings_y + (tft.fontHeight(4) / 2) - 4;

  // start.pushSprite(0, 0);

  tft.drawString(start_txt, start_x, start_y, 4);
  tft.drawString(settings_txt, settings_x, settings_y, 4);

  /* arrow pointing downward */
  tft.drawFastVLine(start_arrow_x, start_arrow_y, 20, TFT_WHITE);
  tft.drawLine(start_arrow_x, start_arrow_y + 20, start_arrow_x - 10, start_arrow_y + 10, TFT_WHITE);
  tft.drawLine(start_arrow_x, start_arrow_y + 20, start_arrow_x + 10, start_arrow_y + 10, TFT_WHITE);

  /* arrow poiting right */
  tft.drawFastHLine(settings_arrow_x, settings_arrow_y, 20, TFT_WHITE);
  tft.drawLine(settings_arrow_x + 20, settings_arrow_y, (settings_arrow_x + 20) - 10, settings_arrow_y - 10, TFT_WHITE);
  tft.drawLine(settings_arrow_x + 20, settings_arrow_y, (settings_arrow_x + 20) - 10, settings_arrow_y + 10, TFT_WHITE);

  if (digitalRead(BTN_C))
  {
    tft.fillScreen(TFT_BLACK);
    vTaskDelay(BTN_DELAY);
    if (WiFi.softAPgetStationNum() > 0)
    {
      started = 1;
    }
    else
    {
      String prompt = "No device connected!";
      int x = (WIDTH - tft.textWidth(prompt, main_font_secondary)) / 2;
      int y = (HEIGHT - tft.fontHeight(main_font_secondary)) / 2;
      tft.drawString(prompt, x, y, main_font_secondary);
      vTaskDelay(BTN_DELAY * 4);
      tft.fillScreen(TFT_BLACK);
    }
  }

  if (digitalRead(BTN_R))
  {
    // start.fillSprite(TFT_BLACK);
    tft.fillScreen(TFT_BLACK);
    vTaskDelay(BTN_DELAY);
    in_settings = 1;
  }
}

void settings_screen()
{
  menu(setting_categories_n, setting_categories, &settings_cursor_index);
}

void bar_init()
{
  top_bar.pushSprite(0, 0);
  main_screen.pushSprite(0, BAR_HEIGHT);
  bottom_bar.pushSprite(0, HEIGHT - BAR_HEIGHT);

  top_bar.fillSprite(TFT_BLACK);
  top_bar.drawFastHLine(0, BAR_HEIGHT - 1, WIDTH, TFT_WHITE);

  main_screen.fillScreen(TFT_BLACK);

  bottom_bar.fillSprite(TFT_BLACK);
  bottom_bar.drawFastHLine(0, 0, WIDTH, TFT_WHITE);
}

/* MENU functions */

void menu(int n_items, char **setting_categories, int *index_var)
{
  // start.pushSprite(0, 0);
  handle_menu_array(setting_categories, n_items);
  handle_menu_cursor(index_var, &n_items);
  *index_var = handle_menu_buttons(*index_var);
}

void handle_menu_cursor(int *cursor_index, const int *N)
{
  if (*cursor_index < 0 || *cursor_index > (*N - 1))
  {
    *cursor_index = 0;
  }
  tft.fillSmoothRoundRect(settings_cursor_x, settings_cursor_y + (*cursor_index * settings_cursor_step), settings_cursor_width, settings_cursor_height, 2, TFT_WHITE);
}

void handle_menu_array(char **items, int N)
{
  for (int i = 0; i < N; i++)
  {
    tft.drawString(*items, settings_start_x, settings_start_y + (i * (settings_step + settings_pad)), settings_font);
    items++;
  }
}

int handle_menu_buttons(int cursor_index)
{
  if (digitalRead(BTN_C))
  {
    handle_menu_functions(&cursor_index, &in_settings);
    vTaskDelay(BTN_DELAY);
    // start.fillSprite(TFT_BLACK);
  }
  if (digitalRead(BTN_L))
  {
    cursor_index--;
    vTaskDelay(BTN_DELAY);
    tft.fillSmoothRoundRect(0, 0, settings_start_x, HEIGHT, 1, TFT_BLACK); // overdraw the cursor area
  }
  if (digitalRead(BTN_R))
  {
    cursor_index++;
    vTaskDelay(BTN_DELAY);
    tft.fillSmoothRoundRect(0, 0, settings_start_x, HEIGHT, 1, TFT_BLACK); // overdraw the cursor area
  }
  return cursor_index;
}

void handle_menu_functions(int *cursor_index, int *back_int)
{
  switch (*cursor_index)
  {
  case 0:
    tft.fillScreen(TFT_BLACK);
    *back_int = 0;
    break;
  case 1:
    in_sub_settings = 1;
    tft.fillScreen(TFT_BLACK);
    vTaskDelay(BTN_DELAY);
    while (in_sub_settings)
    {
      connect_page();
    }
    break;
  case 3:
    in_sub_settings = 1;
    tft.fillScreen(TFT_BLACK);
    vTaskDelay(BTN_DELAY);
    while (in_sub_settings)
    {
      info_page();
    }
    break;
  }
}

void info_page()
{
  // start.pushSprite(0,0);
  // tft.fillScreen(TFT_BLACK);
  String header = "Cycling buddy";
  int start_x = (WIDTH - tft.textWidth(header)) / 2;
  int start_y = 30;
  int text_height = tft.fontHeight(secondary_font);
  int pad = 10;

  tft.drawString("Cycling buddy", start_x, start_y, secondary_font);
  tft.drawFastHLine(0, start_y + text_height + pad, WIDTH, TFT_WHITE);

  int text_start_x = 30;
  int text_start_y = start_y + text_height + pad * 2;

  tft.drawString("Version: " + String(VERSION), text_start_x, text_start_y, secondary_font);
  tft.drawString("IP address: " + get_ip(WiFi.softAPIP()), text_start_x, text_start_y + text_height, secondary_font);
  tft.drawString("MAC address: " + String(WiFi.macAddress()), text_start_x, text_start_y + text_height * 2, secondary_font);
  tft.drawString("Connected devices " + String(WiFi.softAPgetStationNum()), text_start_x, text_start_y + text_height * 3, secondary_font);

  // tft.drawString("Connection: " + connection_status(), text_start_x, text_start_y + text_height * 2, secondary_font);

  if (digitalRead(BTN_C) | digitalRead(BTN_L) | digitalRead(BTN_R))
  {
    in_sub_settings = 0;
    vTaskDelay(BTN_DELAY);
    tft.fillScreen(TFT_BLACK);
  }
}

void connect_page()
{
  tft.pushImage(60, 60, 125, 125, qr_code);
  if (digitalRead(BTN_C) | digitalRead(BTN_L) | digitalRead(BTN_R))
  {
    in_sub_settings = 0;
    vTaskDelay(BTN_DELAY);
    tft.fillScreen(TFT_BLACK);
  }
}

String get_ip(IPAddress ip)
{
  String s = "";
  for (int i = 0; i < 4; i++)
    s += i ? "." + String(ip[i]) : String(ip[i]);
  return s;
}

/*LOOP (PAGE) FUNCTIONS */

void lock_screen()
{
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  float accel_x = a.acceleration.x;
  float accel_y = a.acceleration.y;
  float accel_z = a.acceleration.z;

  if (round(locked_accel_x) == -1 && round(locked_accel_y) == -1 && round(locked_accel_y) == -1)
  {
    // locked position not set yet
    locked_accel_x = accel_x;
    locked_accel_y = accel_y;
    locked_accel_z = accel_z;
  }
  else
  {
    // check for movement
    float delta_x = abs(accel_x - locked_accel_x);
    float delta_y = abs(accel_y - locked_accel_y);
    float delta_z = abs(accel_z - locked_accel_z);

    if (delta_x > MOVEMENT_TRESHOLD || delta_y > MOVEMENT_TRESHOLD || delta_z > MOVEMENT_TRESHOLD)
    {
      // bike moved -> inform the user
      bike_moved = 1;
    }
  }
  // main_screen.pushImage(25,0, 95, 110, lock);
}

void get_GPS()
{
  /*
  myPort.listen();
  if(myPort.available() > 0){
    int teststr = myPort.read();
     if (gps.encode((char) teststr))
      displayInfo();
  }
  */
  const char *gpsStream =
      "$GPRMC,154533.00,A,5028.24345,N,01324.32805,E,0.039,,080523,,,A*70\r\n"
      "$GPVTG,,T,,M,0.039,N,0.073,K,A*2D\r\n"
      "$GPGGA,154533.00,5028.24345,N,01324.32805,E,1,09,0.96,358.7,M,44.7,M,,*52\r\n"
      "$GPGSA,A,3,03,08,32,10,27,02,14,21,01,,,,1.81,0.96,1.54*0E\r\n"
      "$GPGSV,3,1,10,01,42,286,25,02,26,099,32,03,11,228,30,08,60,188,39*7C\r\n"
      "$GPGSV,3,2,10,10,36,056,46,14,20,309,25,21,71,296,35,22,32,131,42*7C\r\n"
      "$GPGSV,3,3,10,27,31,158,41,32,42,101,39*7B\r\n"
      "$GPGLL,5028.24345,N,01324.32805,E,154533.00,A,A*6F\r\n"
      "$GPRMC,154534.00,A,5028.24346,N,01324.32811,E,0.059,,080523,,,A*77\r\n"
      "$GPVTG,,T,,M,0.059,N,0.109,K,A*27\r\n"
      "$GPGGA,154534.00,5028.24346,N,01324.32811,E,1,09,0.96,358.3,M,44.7,M,,*57\r\n"
      "$GPGSA,A,3,03,08,32,10,27,02,14,21,01,,,,1.81,0.96,1.54*0E\r\n"
      "$GPGSV,3,1,10,01,42,286,25,02,26,099,34,03,11,228,30,08,60,188,40*74\r\n"
      "$GPGSV,3,2,10,10,36,056,47,14,20,309,26,21,71,296,35,22,32,131,43*7F\r\n"
      "$GPGSV,3,3,10,27,31,158,42,32,42,101,40*76\r\n"
      "$GPGLL,5028.24346,N,01324.32811,E,154534.00,A,A*6E\r\n"
      "$GPRMC,154535.00,A,5028.24347,N,01324.32817,E,0.064,,080523,,,A*7F\r\n";

  while (*gpsStream)
  {
    gps.encode(*gpsStream++);
  }
}

void handle_gps()
{
  current_lat = gps.location.lat();
  current_long = gps.location.lng();
  altitude = gps.altitude.meters();
  if (last_lat == 0 && last_long == 0)
  {
    last_lat = current_lat;
    last_long = current_long;
    Serial.println("NEW LOCATION!");
  }
  // read the distance between 2 points (last and new)
  double dist = distance(last_lat, last_long, current_lat, current_long);

  if (dist > 5)
  {
    // add current dist to total dist
    total_dist += dist;
  }

  last_lat = current_lat;
  last_long = current_long;
}

void displayInfo()
{
  Serial.print(F("Location: "));
  if (gps.location.isValid())
  {
    Serial.print(gps.location.lat(), 6);
    Serial.print(F(","));
    Serial.print(gps.location.lng(), 6);
  }
  else
  {
    Serial.print(F("INVALID"));
  }

  Serial.print(F("  Date/Time: "));
  if (gps.date.isValid())
  {
    Serial.print(gps.date.month());
    Serial.print(F("/"));
    Serial.print(gps.date.day());
    Serial.print(F("/"));
    Serial.print(gps.date.year());
  }
  else
  {
    Serial.print(F("INVALID"));
  }

  Serial.print(F(" "));
  if (gps.time.isValid())
  {
    if (gps.time.hour() < 10)
      Serial.print(F("0"));
    Serial.print(gps.time.hour() + 2);
    Serial.print(F(":"));
    if (gps.time.minute() < 10)
      Serial.print(F("0"));
    Serial.print(gps.time.minute());
    Serial.print(F(":"));
    if (gps.time.second() < 10)
      Serial.print(F("0"));
    Serial.print(gps.time.second());
    Serial.print(F("."));
    if (gps.time.centisecond() < 10)
      Serial.print(F("0"));
    Serial.print(gps.time.centisecond());
  }
  else
  {
    Serial.print(F("INVALID"));
  }

  Serial.println();
}

void wifi_fnc()
{
  int n = WiFi.softAPgetStationNum();
  if (n == 0)
  {
    interrupted = 1;
  }
  else
  {
    interrupted = 0;
  }
}

void check_for_halt()
{
  if (digitalRead(BTN_C))
  {
    if (center_btn_timer < BTN_TIMER_TRIGGER)
    {
      center_btn_timer++;
    }
    else
    {
      // call halt function
      top_bar.fillSprite(TFT_BLACK);
      main_screen.fillSprite(TFT_BLACK);
      bottom_bar.fillSprite(TFT_BLACK);
      display_stop("STOP");
      if (center_btn_panic_timer < BTN_TIMER_TRIGGER)
      {
        center_btn_panic_timer++;
      }
      else
      {
        // end functions
        finished = 1;
        esp_deep_sleep_start();
      }
    }
  }
  else
  {
    center_btn_timer = 0;
    center_btn_panic_timer = 0;
  }
}

void check_for_fall()
{
  // FALL DETECTION
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  float accel_x = a.acceleration.x;
  float accel_y = a.acceleration.y;
  float accel_z = a.acceleration.z;

  float accel_total = sqrt(pow(accel_x, 2) + pow(accel_y, 2) + pow(accel_z, 2));

  if (accel_total > FALL_TRESHOLD)
  {
    fell_time = millis();
    fall_detected = 1;
  }
  else if (fell_time - millis() >= FALL_HOLD)
  {
    fell_time = 0;
    fall_detected = 0;
  }
}

void handle_page_movement()
{
  if (current_page_id != last_page_id)
  {
    transition_ticks = 0;
    bar_init();
    last_page_id = current_page_id;
  }

  if (digitalRead(BTN_R))
  {
    if (current_page_id > PAGES)
      current_page_id = 0;
    else
      current_page_id++;
    vTaskDelay(BTN_DELAY);
  }
  if (digitalRead(BTN_L))
  {
    if (current_page_id == 0)
      current_page_id = PAGES - 1;
    else
      current_page_id--;
    vTaskDelay(BTN_DELAY);
  }
}

void handle_page_display()
{
  switch (current_page_id)
  {
  case 0:
    page_0_fnc();
    break;
  case 1:
    page_1_fnc();
    break;
  case 2:
    page_2_fnc();
    break;
  case 3:
    page_3_func();
    break;
  case 4:
    page_4_fnc();
    break;
  case 5:
    if (interrupted)
    {
      page_5_func();
    }
    else
    {
      current_page_id++;
    }
    break;
  default:
    page_0_fnc();
    break;
  }
}

String get_time()
{
  /* function, that returns a str with current time -> has error of formating seconds to minutes*/
  String time;
  String minutes_seconds;
  String hours_str;
  String seconds_adder = "";
  float seconds_passed = (float)time_passed / 1000;
  float minutes_passed = seconds_passed / 60;
  float hours_passed = minutes_passed / 60;

  seconds_passed = seconds_passed - (int)minutes_passed * 60;
  minutes_passed = minutes_passed - (int)hours_passed * 60;

  if (seconds_passed < 10)
  {
    seconds_adder = "0";
  }

  if (minutes_passed >= 10)
  {
    minutes_seconds = String((int)minutes_passed) + ":" + seconds_adder + String(seconds_passed);
  }
  else
  {
    minutes_seconds = "0" + String((int)minutes_passed) + ":" + seconds_adder + String(seconds_passed);
  }

  if (hours_passed >= 1)
  {
    hours_str = String((int)hours_passed) + ":";
    minutes_seconds = String((int)minutes_passed) + ":" + seconds_adder + String((int)seconds_passed);
  }

  else
  {
    hours_str = "";
  }
  return hours_str + minutes_seconds;
}

String get_date()
{
  String date = "";
  if (gps.date.isValid())
  {
    date = String(gps.date.day()) + "/" + String(gps.date.month()) + "/" + String(gps.date.year());
  }
  else
  {
    date = "Waiting for GPS ...";
  }
}

String get_speed()
{
  /* function displaying current speed -> GPS interactiob*/
  /*
  if (gps.speed.isUpdated())
  {
    speed = gps.speed.kmph();
    if(speed > max_speed){
      max_speed = speed;
    }
  }
  */
  if (speed > max_speed)
  {
    max_speed = speed;
  }
  return String(speed);
}

String get_average_speed()
{
  float total_passed_hrs = (time_passed / 1000) / 3600;
  float avg = total_dist / total_passed_hrs;
  return String(avg);
}

String get_distance()
{
  // preferences.putFLoat("total_dist", total_dist);
  return String(0);
}

double toRadians(const long double degree)
{
  double one_deg = (M_PI) / 180;
  return (one_deg * degree);
}

double distance(double lat1, double long1, double lat2, double long2)
{
  // Convert the latitudes and longitudes from degree to radians.
  lat1 = toRadians(lat1);
  long1 = toRadians(long1);
  lat2 = toRadians(lat2);
  long2 = toRadians(long2);

  // Haversine Formula
  double dlong = long2 - long1;
  double dlat = lat2 - lat1;

  double ans = pow(sin(dlat / 2), 2) + cos(lat1) * cos(lat2) * pow(sin(dlong / 2), 2);
  ans = 2 * asin(sqrt(ans));
  // Radius of Earth in Kilometers, R = 6371
  // Use R = 3956 for miles
  double R = 6371;
  // Calculate the result
  ans = ans * R;
  return ans;
}

float avg(float array[], int len)
{
  float sum = 0.00;
  for (int i = 0; i < len; i++)
  {
    sum += array[i];
  }
  return sum / len;
}

/* PAGE funcs*/

void page_0_fnc()
{
  /* Page showing SPEED */
  current_page_id = 0;
  String top_txt;
  String txt;
  String bottom_txt;

  if (transition_ticks <= TRANSITION_DELAY_TICKS)
  {
    top_txt = get_time();
    txt = "SPEED";
    bottom_txt = "";
    transition_ticks++;
    display_text(top_txt, txt, bottom_txt);
  }
  else
  {
    top_txt = "MAX: " + max_speed);
    txt = get_speed();
    bottom_txt = "AVG: " + get_average_speed();
    display_number_unit(top_txt, txt, bottom_txt, "Km/h");
  }
}

void page_1_fnc()
{
  /* page showing DISTANCE */
  current_page_id = 1;
  String txt;
  String top_txt;
  String bottom_txt;

  if (transition_ticks <= TRANSITION_DELAY_TICKS)
  {
    txt = "DISTANCE";
    top_txt = get_time();
    bottom_txt = "";
    transition_ticks++;
    display_text(top_txt, txt, bottom_txt);
  }
  else
  {
    txt = get_distance();
    top_txt = get_time();
    bottom_txt = "TOTAL: " + String(preferences.getUInt("total_dist", 0)) + "Km";
    ;
    display_number_unit(top_txt, txt, bottom_txt, "Km");
  }
}

void page_2_fnc()
{
  /* page showing ELAPSED TIME*/
  current_page_id = 2;
  String txt;
  String top_txt;
  String bottom_txt;
  if (transition_ticks <= TRANSITION_DELAY_TICKS)
  {
    txt = "TIME";
    top_txt = "";
    bottom_txt = "";
    display_text(top_txt, txt, bottom_txt);
    transition_ticks++;
  }
  else
  {
    txt = get_time();
    top_txt = get_speed() + "Km/h";
    bottom_txt = get_distance() + " Kms";
    display_number(top_txt, txt, bottom_txt);
  }
}

void page_3_func()
{
  /* page showing GYRO */
  current_page_id = 3;
  String txt;
  String top_txt;
  String bottom_txt;

  if (transition_ticks <= TRANSITION_DELAY_TICKS)
  {
    txt = "GYRO";
    top_txt = "SPEED";
    bottom_txt = "BOTTOM";
    display_text(top_txt, txt, bottom_txt);
    transition_ticks++;
  }
  else
  {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);

    float current_time = millis();
    if (current_time - last_read_angle >= MPU_INTERVAL)
    {
      if (angle_index > 3)
      {
        angle_index = 0;
      }
      last_read_angle = 0; // possible error -> change for current_time if needed
      angle_lr_filter[angle_index] = a.acceleration.roll;
      angle_fr_filter[angle_index] = a.acceleration.pitch;
      angle_index++;
    }

    angle_fr = avg(angle_lr_filter, 4);
    angle_lr = avg(angle_fr_filter, 4);

    top_txt = "SPEED";
    bottom_txt = "BOTTOM";

    int radius = 30;
    int pad = 10;
    int line_width = 2;

    int circ_one_x = (WIDTH / 2) - radius - pad;
    int circ_one_y = (HEIGHT - (2 * BAR_HEIGHT)) / 2;

    int circ_two_x = (WIDTH / 2) + radius + pad;
    int circ_two_y = (HEIGHT - (2 * BAR_HEIGHT)) / 2;

    int line_one_x1 = circ_one_x + cos(angle_lr) * radius;
    int line_one_y1 = circ_one_y + sin(angle_lr) * radius;

    int line_two_x1 = circ_two_x + cos(angle_fr) * radius;
    int line_two_y1 = circ_two_y + sin(angle_fr) * radius;

    int line_one_x1_reverse = circ_one_x - cos(angle_lr) * radius;
    int line_one_y1_reverse = circ_one_y - sin(angle_lr) * radius;

    int line_two_x1_reverse = circ_two_x - cos(angle_fr) * radius;
    int line_two_y1_reverse = circ_two_y - sin(angle_fr) * radius;

    int bar_x_top = (WIDTH - tft.textWidth(top_txt, secondary_font)) / 2;
    int bar_x_bottom = (WIDTH - tft.textWidth(bottom_txt, secondary_font)) / 2;
    int bar_y = (BAR_HEIGHT - tft.fontHeight(secondary_font)) / 2;

    top_bar.drawString(top_txt, bar_x_top, bar_y, secondary_font);

    main_screen.fillSmoothCircle(circ_one_x, circ_one_y, radius, TFT_WHITE); // circle 1
    main_screen.fillSmoothCircle(circ_two_x, circ_two_y, radius, TFT_WHITE); // circle 2

    // line 1
    main_screen.drawWideLine(circ_one_x, circ_one_y, line_one_x1, line_one_y1, line_width, TFT_RED);
    main_screen.drawWideLine(circ_one_x, circ_one_y, line_one_x1_reverse, line_one_y1_reverse, line_width, TFT_RED);

    // line 2
    main_screen.drawWideLine(circ_two_x, circ_two_y, line_two_x1, line_two_y1, line_width, TFT_GREEN);
    main_screen.drawWideLine(circ_two_x, circ_two_y, line_two_x1_reverse, line_two_y1_reverse, line_width, TFT_GREEN);

    bottom_bar.drawString(bottom_txt, bar_x_bottom, bar_y, secondary_font);
  }
}

void page_4_func()
{
  /* page showing DISTANCE */
  current_page_id = 4;
  String txt;
  String top_txt;
  String bottom_txt;

  if (transition_ticks <= TRANSITION_DELAY_TICKS)
  {
    txt = "GPS";
    transition_ticks++;
    display_text("", txt, "");
  }
  else
  {
    display_text("ALT: " + String(altitude) + "m", "LONG: " + String(current_long +) "\nLAT: " + String(current_lat), get_speed() + " Km/h");
  }
}

void page_5_func()
{
  /* page showing DISTANCE */
  current_page_id = 5;
  String txt;
  String top_txt;
  String bottom_txt;

  if (transition_ticks <= TRANSITION_DELAY_TICKS)
  {
    txt = "CONNECT";
    transition_ticks++;
    display_text("", txt, "");
  }
  else
  {
    main_screen.pushImage(60, 7, 125, 125, qr_code);
    display_text("CONNECT", "", get_time());
  }
}

void display_text(String top_str, String main_str, String bottom_str)
{

  int x = (WIDTH - tft.textWidth(main_str, main_font_secondary)) / 2;
  int y = ((HEIGHT - (2 * BAR_HEIGHT)) - tft.fontHeight(main_font_secondary)) / 2;

  int bar_x_top = (WIDTH - tft.textWidth(top_str, secondary_font)) / 2;
  int bar_x_bottom = (WIDTH - tft.textWidth(bottom_str, secondary_font)) / 2;
  int bar_y = (BAR_HEIGHT - tft.fontHeight(secondary_font)) / 2;

  top_bar.drawString(top_str, bar_x_top, bar_y, secondary_font);
  main_screen.drawString(main_str, x, y, main_font_secondary);
  bottom_bar.drawString(bottom_str, bar_x_bottom, bar_y, secondary_font);
}

void display_number(String top_str, String main_str, String bottom_str)
{
  int x = (WIDTH - tft.textWidth(main_str, main_font)) / 2;
  int y = ((HEIGHT - (2 * BAR_HEIGHT)) - tft.fontHeight(main_font)) / 2;

  int bar_x_top = (WIDTH - tft.textWidth(top_str, secondary_font)) / 2;
  int bar_x_bottom = (WIDTH - tft.textWidth(bottom_str, secondary_font)) / 2;
  int bar_y = (BAR_HEIGHT - tft.fontHeight(secondary_font)) / 2;

  top_bar.drawString(top_str, bar_x_top, bar_y, secondary_font);
  main_screen.drawString(main_str, x, y, main_font);
  bottom_bar.drawString(bottom_str, bar_x_bottom, bar_y, secondary_font);
}

void display_number_unit(String top_str, String main_str, String bottom_str, String unit)
{
  int pad = 10;
  int speed_x_offset = 30;

  int x = ((WIDTH - tft.textWidth(main_str, main_font)) / 2) - speed_x_offset;
  int y = ((HEIGHT - (2 * BAR_HEIGHT)) - tft.fontHeight(main_font)) / 2;
  int unit_x = x + tft.textWidth(main_str, main_font) + pad;
  int unit_y = y + tft.fontHeight(main_font) - tft.fontHeight(main_font_secondary);

  int bar_x_top = (WIDTH - tft.textWidth(top_str, secondary_font)) / 2;
  int bar_x_bottom = (WIDTH - tft.textWidth(bottom_str, secondary_font)) / 2;
  int bar_y = (BAR_HEIGHT - tft.fontHeight(secondary_font)) / 2;

  top_bar.drawString(top_str, bar_x_top, bar_y, secondary_font);

  main_screen.drawString(main_str, x, y, main_font);
  main_screen.drawString(unit, unit_x, unit_y, main_font_secondary);

  bottom_bar.drawString(bottom_str, bar_x_bottom, bar_y, secondary_font);
}

void display_stop(String main_str)
{
  int x = (WIDTH - tft.textWidth(main_str, main_font_secondary)) / 2;
  int y = ((HEIGHT - (2 * BAR_HEIGHT)) - tft.fontHeight(main_font_secondary)) / 2;

  main_screen.drawString(main_str, x, y, main_font_secondary);
}