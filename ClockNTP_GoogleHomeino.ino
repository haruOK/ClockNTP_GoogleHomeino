#include <M5Stack.h>
#include <WiFi.h>
#include <stdio.h>
#include <time.h> // time() ctime()
#include <esp8266-google-home-notifier.h>
#include "CronAlarms.h"
#include "DHT12.h"
#include "defile.h"
#include <Wire.h> //The DHT12 uses I2C comunication.

/** global variables **/
GoogleHomeNotifier ghn;
bool setup_flag = false;
static bool m5_setup_flag = false;
static bool wifi_setup_flag = false;
static bool googlehome_setup_flag = false;
static bool ntp_setup_flag = false;
static bool cron_setup_flag = false;
static bool thread_setup_flag = false;
CronID_t TimeSignalAlarm_CronID;
CronID_t MorningAlarm_CronID;
CronID_t EveningAlarm_CronID;

/** static functions **/
static void ghnSendMessage(const char *str);
static void getTimeString(char *str);
static void TimeSignalAlarm(void);
static void MorningAlarm(void);
static void EveningAlarm(void);
static void m5_setup(void);
static void wifi_setup(void);
static void googlehome_setup(void);
static void ntp_setup(void);
static void cron_setup(void);
static void thread_setup(void);
static void printLocalTime(void);
static void printEnvData(void);
static void printDisplayTask(void *pvParameters);

//現時刻を文字列で取得
static void getTimeString(char *str)
{
  struct tm timeinfo;
  getLocalTime(&timeinfo);

  if (timeinfo.tm_hour >= 12)
  {
    sprintf(str, "午後%d時", timeinfo.tm_hour - 12);
  }
  else
  {
    sprintf(str, "午前%d時", timeinfo.tm_hour);
  }
  if (timeinfo.tm_min != 0)
  {
    sprintf(str, "%s%d分です。", str, timeinfo.tm_min);
  }
  else
  {
    //
    sprintf(str, "%sになりました。", str);
  }
}

//文字列をGoogleHomeへ通知する
static void ghnSendMessage(const char *str)
{
  if (googlehome_setup_flag == true)
    ghn.notify(str);
}

//通常の時刻通知
static void TimeSignalAlarm(void)
{
  char str[100];
  getTimeString(str);
  ghnSendMessage(str);
}

//朝の挨拶
static void MorningAlarm(void)
{
  char str[100];
  getTimeString(str);
  strcat(str, "起きる時間です。おはようございます");
  ghnSendMessage(str);
}

//夜の挨拶
static void EveningAlarm(void)
{
  char str[100];
  getTimeString(str);
  strcat(str, "そろそろ寝る準備をしましょう");
  ghnSendMessage(str);
}

// 時刻を画面に表示する
static void printLocalTime()
{
  struct tm timeinfo;
  getLocalTime(&timeinfo);

  int8_t hh = timeinfo.tm_hour;
  int8_t mm = timeinfo.tm_min;
  int8_t ss = timeinfo.tm_sec;
  static byte omm = 99, oss = 99;
  static byte xcolon = 0, xsecs = 0;

  // Update digital time
  int xpos = 0;
  // int ypos = 85; // Top left corner ot clock text, about half way down
  // int ysecs = ypos + 24;
  int ypos = 20; // Top left corner ot clock text, about half way down
  int ysecs = ypos + 30;

  if (omm != mm)
  { // Redraw hours and minutes time every minute
    omm = mm;

    // Draw hours and minutes
    if (hh < 10)
      xpos += M5.Lcd.drawChar('0', xpos, ypos, 8); // Add hours leading zero for 24 hr clock
    xpos += M5.Lcd.drawNumber(hh, xpos, ypos, 8);  // Draw hours
    xcolon = xpos;                                 // Save colon coord for later to flash on/off later
    xpos += M5.Lcd.drawChar(':', xpos, ypos - 8, 8);
    if (mm < 10)
      xpos += M5.Lcd.drawChar('0', xpos, ypos, 8); // Add minutes leading zero
    xpos += M5.Lcd.drawNumber(mm, xpos, ypos, 8);  // Draw minutes
    xsecs = xpos;                                  // Sae seconds 'x' position for later display updates
  }

  if (oss != ss)
  { // Redraw seconds time every second
    oss = ss;
    xpos = xsecs;

    if (ss % 2)
    {                                                // Flash the colons on/off
      M5.Lcd.setTextColor(0x39C4, TFT_BLACK);        // Set colour to grey to dim colon
      M5.Lcd.drawChar(':', xcolon, ypos - 8, 8);     // Hour:minute colon
      xpos += M5.Lcd.drawChar(':', xsecs, ysecs, 6); // Seconds colon
      M5.Lcd.setTextColor(TFT_YELLOW, TFT_BLACK);    // Set colour back to yellow
    }
    else
    {
      M5.Lcd.drawChar(':', xcolon, ypos - 8, 8);     // Hour:minute colon
      xpos += M5.Lcd.drawChar(':', xsecs, ysecs, 6); // Seconds colon
    }

    //Draw seconds
    if (ss < 10)
      xpos += M5.Lcd.drawChar('0', xpos, ysecs, 6); // Add leading zero
    M5.Lcd.drawNumber(ss, xpos, ysecs, 6);          // Draw seconds
  }
}

// 温湿度を画面に表示する
static void printEnvData(void)
{
  static DHT12 dht12; //Preset scale CELSIUS and ID 0x5c.
  int xpos = 0;
  int ypos = 160; // Top left corner ot clock text, about half way down

  //Read temperature with preset scale.
  xpos += M5.Lcd.drawFloat(dht12.readTemperature(), 1, xpos, ypos, 6);
  xpos += M5.Lcd.drawString("*C   ", xpos, ypos, 4);
  xpos += M5.Lcd.drawFloat(dht12.readHumidity(), 1, xpos, ypos, 6);
  xpos += M5.Lcd.drawString("%", xpos, ypos, 4);
}

//画面表示を管理するタスク
//GoogleHome通知中に画面表示が遅延するのを防ぐため、別タスクにする
static void printDisplayTask(void *pvParameters)
{
  int count = 0;
  while (1)
  {
    printLocalTime();
    if (count == 0)
    {
      printEnvData();
    }
    vTaskDelay(10);
    count = (count + 1) % 70;
  }
}

//M5Stack初期設定
static void m5_setup(void)
{
  if (m5_setup_flag == true)
    return;
  M5.begin();
  dacWrite(25, 0); // Speaker OFF
  Wire.begin();
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  m5_setup_flag = true;
}

//Wifi初期設定
//SSIDとパスワードは別ファイルにて管理
static void wifi_setup(void)
{
  if (wifi_setup_flag == true)
    return;

  m5_setup();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    M5.Lcd.print(".");
  }
  M5.Lcd.clear();
  wifi_setup_flag = true;
}

// GoogleHomeへの接続
// wifiが初期化されていることが条件
static void googlehome_setup(void)
{
  if (googlehome_setup_flag == true)
    return;

  const char displayName[] = "ダイニング ルーム";

  m5_setup();
  wifi_setup();
  M5.Lcd.printf("Connect to Google Home ...\n");
  if (ghn.device(displayName, "ja") != true)
  {
    M5.Lcd.println(ghn.getLastError());
  }
  else
  {
    M5.Lcd.printf("Success!!");
  }
  delay(500);
  M5.Lcd.clear();
  googlehome_setup_flag = true;
}

//ntpサーバから時刻を取得
static void ntp_setup(void)
{
  if (ntp_setup_flag == true)
    return;

  const char *ntpServer = "ntp.nict.jp";
  const long gmtOffset_sec = 9 * 3600;
  const int daylightOffset_sec = 0;

  wifi_setup();
  //init and get the time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  ntp_setup_flag = true;
}

// 時報機能をcronで起動する
static void cron_setup(void)
{
  if (cron_setup_flag == true)
    return;

  TimeSignalAlarm_CronID = Cron.create("0 0 * * * *", TimeSignalAlarm, false);          // every hour
  MorningAlarm_CronID = Cron.create("0 15 6 * * 1-5", MorningAlarm, false); // 6:15am every weekday
  EveningAlarm_CronID = Cron.create("0 45 20 * * *", EveningAlarm, false);  // 20:45 every day
  cron_setup_flag = true;
}

// 画面表示はスレッドで起動する
// GoogleHomeへのアクセス時に画面がフリーズするため
static void thread_setup(void)
{
  if (thread_setup_flag == true)
    return;

  // 画面はM5Stackのみ
  if(m5_setup_flag != true)
    return;

  //Core 1 thread
  xTaskCreatePinnedToCore(
      printDisplayTask,
      "printDisplay",
      8192,
      NULL,
      1,
      NULL,
      0);

  thread_setup_flag = true;
}

// セットアップ
// 各種機能の初期化を実施
void setup(void)
{
  setup_flag = false;

  m5_setup();
  wifi_setup();
  googlehome_setup();
  ntp_setup();
  thread_setup();
  cron_setup();

  setup_flag = true;
}

// メインループ
// ボタン検出が主
void loop()
{
  if(m5_setup_flag == true)
  {
    // if you want to use Releasefor("was released for"), use .wasReleasefor(int time) below
    if (M5.BtnA.wasReleased())
    {
      TimeSignalAlarm();
    }
    else if (M5.BtnB.wasReleased())
    {
      ghnSendMessage("Fが押されました");
    }
    else if (M5.BtnC.wasReleased())
    {
      ghnSendMessage("Gが押されました");
    }
    M5.update();
  }

  if(cron_setup_flag == true)
  {
    Cron.delay();
  }
}
