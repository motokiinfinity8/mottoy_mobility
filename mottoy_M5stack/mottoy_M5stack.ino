#include <WiFi.h>
#include <WiFiUdp.h>
#include <M5Stack.h>
#include "esp_system.h"
#include "AudioFileSourceSD.h"
#include "AudioFileSourceID3.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"
#include "AudioGeneratorWAV.h"

#define SerialPort Serial

#define CMD_SIZE  12  


/** MOPPY モータードライバ制御
 *  [MOPPY仕様]
 *   Motor(L) 前方向 [緑] ---  停止 : 3V(Hi-Z)   / 駆動 : 0V  (GND結合) 
 *   Motor(L) 後方向 [黄] ---  停止 : 0.5V(Hi-Z) / 駆動 : 0V  (GND結合) ★ 
 *   Motor(R) 前方向 [青] ---  停止 : 3V(Hi-Z) / 駆動 : 0V  (GND結合) 
 *   Motor(R) 後方向 [橙] ---  停止 : 3V(Hi-Z) / 駆動 : 0V  (GND結合) 
 *　　★Motor(L) 後ろ方向のみモータードライバ制御信号の電圧が異なるため、
 *　　　制御信号も変更する 
 *
 * [本モジュール仕様]
 *   Motor(L) 前方向 (LF_MOTER_PIN)  ---- H : 駆動(TR ON)、 L : 停止 (TR OFF)
 *   Motor(L) 後方向 (LB_MOTER_PIN)  ----H : 駆動(TR ON)、 L : 停止 (TR OFF)
 *   Motor(R) 前方向 (RF_MOTER_PIN)  ---- H : 駆動(TR ON)、 L : 停止 (TR OFF)
 *   Motor(R) 後方向 (RB_MOTER_PIN)  ---- H : 駆動(TR ON)、 L : 停止 (TR OFF)
 *
  */

#define LF_MOTER_PIN 2
#define LB_MOTER_PIN 5
#define RF_MOTER_PIN 21
#define RB_MOTER_PIN 22

const char ssid[] = "ESP32_MOBILITY"; // SSID
const char password[] = "esp32_con";  // password
const int WifiPort = 8000;      // ポート番号

const IPAddress HostIP(192, 168, 11, 1);       // IPアドレス
const IPAddress ClientIP(192, 168, 11, 2);       // IPアドレス
const IPAddress subnet(255, 255, 255, 0); // サブネットマスク
const IPAddress gateway(192,168, 11, 0);
const IPAddress dns(192, 168, 11, 0);
bool wifi_connect;

WiFiUDP Udp;
uint8_t WiFibuff[CMD_SIZE];
int motor_inst, motor_state ;
String cmd_msg, vel_msg;
int vel_linear=0, vel_argular=0;
int running_mode; //0: 事前通知モード、1:事前予告なし(通常)モード

// initial value for audio
AudioGeneratorWAV *wav;
AudioGeneratorMP3 *mp3;
AudioFileSourceSD *file;
AudioOutputI2S *out;
AudioFileSourceID3 *id3;
bool g_audio_play;


void setup() 
{
  //PIN設定
  pinMode(LF_MOTER_PIN, OUTPUT);
  pinMode(LB_MOTER_PIN, OUTPUT);
  pinMode(RF_MOTER_PIN, OUTPUT);
  pinMode(RB_MOTER_PIN, OUTPUT);
  digitalWrite(LF_MOTER_PIN, LOW);
  digitalWrite(LB_MOTER_PIN, LOW);
  digitalWrite(RF_MOTER_PIN, LOW);
  digitalWrite(RB_MOTER_PIN, LOW); 

  // Caution : M5stack内でI2C信号をセットアップする影響で、該当信号がHighに上がるため
  //           Libraryファイルを改良 (I2CEnableの無効設定を追加)
  M5.begin(true, true, false);  //I2C Enableは無効とする (該当端子をGPIOとして使用しているため）


  Serial.begin(115200);
  motor_inst = 0;
  running_mode = 0;
  
  // Wifi設定 (SSID&パス設定)
  wifi_connect = false;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);
  delay(100);
  WiFi.softAPConfig(HostIP, HostIP, subnet); // IPアドレス設定

  Serial.print("AP IP address: ");
  IPAddress myAddress = WiFi.softAPIP();
  Serial.println(myAddress);

  Udp.begin(WifiPort);  // UDP通信開始
  Serial.println("Starting UDP");
  
  Serial.print("Local port: ");
  Serial.println(WifiPort);

  //WAV再生設定
  out = new AudioOutputI2S(0,1); 
  out->SetOutputModeMono(true);
  out->SetGain(0.5); 
  wav = new AudioGeneratorWAV(); 



  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextColor(WHITE ,BLACK); // Set pixel color; 1 on the monochrome screen
  M5.Lcd.setTextSize(3);
  M5.Lcd.setCursor(0,0);
  M5.Lcd.print("Running Mode : "); 
  M5.Lcd.setCursor(248,0);
  M5.Lcd.print("0");   
  
}

void loop() {

loop_start:

   M5.update();
   //if(M5.BtnA.wasPressed()){
   //   running_mode = 0;   
   //   M5.Lcd.setCursor(248,0);
   //   M5.Lcd.print("0");  
   //}   
   //if(M5.BtnB.wasPressed()){
   //   running_mode = 1;   
   //   M5.Lcd.setCursor(248,0);
   //   M5.Lcd.print("1");
   //}
  int packet_size = Udp.parsePacket();

  if (packet_size > 0) {
    //Serial.print(packet_size);
    
    if(wifi_connect == false){
       wifi_connect = true;
       file = new AudioFileSourceSD("/joycon.wav"); 
       wav->begin(file, out);
       Serial.println("WAV Playing! (Connecting)"); 
       while(wav->isRunning()){
          if (!wav->loop()) wav->stop();
       }  
    }
     
    //Serial.println("Serial Received");   
    rcvWiFi();    
    int i;
    //for(i=0; i<packet_size; i++){
    //  Serial.print(WiFibuff[i]);
    //  Serial.print(" ");
   // }
      
    if(WiFibuff[0] == 0x2){
          String s_buf = String((char*)WiFibuff);
          Serial.println(s_buf.substring(1,11));
          cmd_msg = (s_buf.substring(1,4));
          vel_msg = (s_buf.substring(4.8));
          String mode_msg =  (s_buf.substring(8.9));
          running_mode = (s_buf.substring(8,9)).toInt();
          Serial.print(cmd_msg);
          Serial.print(" ");
          Serial.print(vel_msg);
          Serial.print(" ");
          Serial.print(mode_msg);
          Serial.print(" ");
          Serial.println(running_mode);

          // MVC (Mobility Direction Control)の場合
          int motor_inst_tmp = motor_inst;
          
          if(cmd_msg.equals("MDC")){
            if((vel_msg.substring(0,2)).equals("-9")) motor_inst = -4;
            if((vel_msg.substring(0,2)).equals("+0")) motor_inst =  0;
            if((vel_msg.substring(0,2)).equals("+9")) motor_inst =  4;

            if((vel_msg.substring(2,4)).equals("-9")) motor_inst -= 1;
            if((vel_msg.substring(2,4)).equals("+9")) motor_inst += 1;
          }

          // レバー操作のチャタ防止対策 2回連続で同じコマンドの場合のみ有効にする
          if(motor_inst_tmp != motor_inst && motor_inst != 0 && running_mode == 0 ){
             delay(100);
             goto loop_start;
          }
          
          //wifi_rcv++;
          //Serial.write('Wifi RCV: '); 
          //Serial.write(wifi_rcv); 
          Serial.print("Motor Instructiont:"); 
          Serial.println(motor_inst);      
     }
  }

  // 無線停止時は、強制OFF
  //if(WiFi.status() != WL_CONNECTED){
  //  Serial.println("WLAN Disconnected!"); 
  //  motor_inst = 0;
  //}

  // Motor制御命令に変更があった場合に停止 & 音源再生
  if((motor_inst != motor_state) && motor_inst != 0 && running_mode == 0){
    Serial.println("Motor Instruction input!"); 
    // Motor停止
    digitalWrite(LF_MOTER_PIN, LOW);
    digitalWrite(LB_MOTER_PIN, LOW);
    digitalWrite(RF_MOTER_PIN, LOW);
    digitalWrite(RB_MOTER_PIN, LOW); 
     switch(motor_inst){
          case  -5: //右後ろ
            file = new AudioFileSourceSD("/back_r.wav");
            break;
          case -4:  //後ろ
            file = new AudioFileSourceSD("/back.wav");   
            break;
          case  -3: //左後ろ
            file = new AudioFileSourceSD("/back_l.wav"); 
            break;
          case  -1: //右
            file = new AudioFileSourceSD("/right.wav"); 
            break;
          case  1:  //左
            file = new AudioFileSourceSD("/left.wav"); 
            break;
          case  3:  //右前
            file = new AudioFileSourceSD("/front_r.wav"); 
            break;
          case  4:  //前
            file = new AudioFileSourceSD("/front.wav"); 
            break;
          case  5:  //左前
            file = new AudioFileSourceSD("/front_l.wav"); 
            break;
        }
      wav = new AudioGeneratorWAV(); 
      wav->begin(file, out);
      Serial.println("WAV Playing!"); 
      while(wav->isRunning()){
       if (!wav->loop()) wav->stop();
      }
      //貯まったUDPバッファを一旦破棄する
      Udp.flush();
  }


  // Motor制御
        switch(motor_inst){
          case  -5: //右後ろ
            digitalWrite(LF_MOTER_PIN, LOW);
            digitalWrite(LB_MOTER_PIN, HIGH);
            digitalWrite(RF_MOTER_PIN, LOW);
            digitalWrite(RB_MOTER_PIN, LOW);            
            break;
          case -4:  //後ろ
            digitalWrite(LF_MOTER_PIN, LOW);
            digitalWrite(LB_MOTER_PIN, HIGH);
            digitalWrite(RF_MOTER_PIN, LOW);
            digitalWrite(RB_MOTER_PIN, HIGH);     
            break;
          case  -3: //左後ろ
            digitalWrite(LF_MOTER_PIN, LOW);
            digitalWrite(LB_MOTER_PIN, LOW);
            digitalWrite(RF_MOTER_PIN, LOW);
            digitalWrite(RB_MOTER_PIN, HIGH);  
            break;
          case  -1: //右
            digitalWrite(LF_MOTER_PIN, HIGH);
            digitalWrite(LB_MOTER_PIN, LOW);
            digitalWrite(RF_MOTER_PIN, LOW);
            digitalWrite(RB_MOTER_PIN, HIGH); 
            break;
          case  1:  //左
            digitalWrite(LF_MOTER_PIN, LOW);
            digitalWrite(LB_MOTER_PIN, HIGH);
            digitalWrite(RF_MOTER_PIN, HIGH);
            digitalWrite(RB_MOTER_PIN, LOW);   
            break;
          case  3:  //右前
            digitalWrite(LF_MOTER_PIN, HIGH);
            digitalWrite(LB_MOTER_PIN, LOW);
            digitalWrite(RF_MOTER_PIN, LOW);
            digitalWrite(RB_MOTER_PIN, LOW); 
            break;
          case  4:  //前
            digitalWrite(LF_MOTER_PIN, HIGH);
            digitalWrite(LB_MOTER_PIN, LOW);
            digitalWrite(RF_MOTER_PIN, HIGH);
            digitalWrite(RB_MOTER_PIN, LOW); 
            break;
          case  5:  //左前
            digitalWrite(LF_MOTER_PIN, LOW);
            digitalWrite(LB_MOTER_PIN, LOW);
            digitalWrite(RF_MOTER_PIN, HIGH);
            digitalWrite(RB_MOTER_PIN, LOW); 
            break;
          case  0:  //なし
            digitalWrite(LF_MOTER_PIN, LOW);
            digitalWrite(LB_MOTER_PIN, LOW);
            digitalWrite(RF_MOTER_PIN, LOW);
            digitalWrite(RB_MOTER_PIN, LOW); 
            break;
        }
  delay(10);
  motor_state = motor_inst;
}

void rcvWiFi() {
  Udp.read(WiFibuff, CMD_SIZE);
  //Serial.print(WiFibuff);
  Udp.flush();
}
