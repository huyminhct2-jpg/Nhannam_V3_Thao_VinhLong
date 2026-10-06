// =====================================================
// ============= Code nhà nấm V3 =======================
// =====================================================
#include <ESP8266httpUpdate.h>
#include <WiFiClientSecure.h>
#include "ESP8266_SHT3X.h"           // Thư viện đọc cảm biến SHT30 (nhiệt độ & độ ẩm)
#include <ESP8266WiFi.h>             // Thư viện WiFi cho ESP8266
#include <WiFiClient.h>              // Thư viện TCP client
#include <ESP8266WebServer.h>        // WebServer đơn giản (dùng cho OTA)
#include <ESP8266mDNS.h>             // mDNS (ví dụ truy cập esp.local)
#include <ESP8266HTTPUpdateServer.h> // Server hỗ trợ OTA update qua web
#include <BlynkSimpleEsp8266.h>      // Thư viện Blynk cho ESP8266
#include <LiquidCrystal_I2C.h>       // Thư viện LCD I2C 16x2
#include <WiFiManager.h>             // WiFiManager (khai báo nhưng không dùng trong logic hiện tại)
#include <time.h>
#define BLYNK_PRINT Serial          // Cho phép Blynk in log ra Serial để debug

// =====================================================
// ====== KHAI BÁO CẢM BIẾN & CHÂN RELAY ==============

// =====================================================
SHT3X sht30(0x44); // Khởi tạo cảm biến SHT30 với địa chỉ I2C 0x44
#define relay1 0    // Chân GPIO0 -> relay1 (quạt) (ghi nhớ module relay có thể active LOW)
#define relay2 14   // Chân GPIO14 -> relay2 (tạo ẩm)
#define relay3 12   // Chân GPIO12 -> relay3 (dự phòng)
#define relay4 13   // Chân GPIO13 -> relay4 (dự phòng)

// =====================================================
// ================= BIẾN TOÀN CỤC =====================
// =====================================================
int Modeactive, Reft1, Reft2, Refh1, Refh2, RefML;  // Modeactive: chế độ; Reft1/Reft2: ngưỡng nhiệt; Refh1/Refh2: ngưỡng ẩm / ngưỡng máy lạnh
int button1, button2, button3, button4;      // Các biến ghi trạng thái nút từ Blynk (manual)
bool stopAuto = false;                        // Cờ dừng chế độ tự động (khi chuyển sang thủ công)
bool autoRunning = false;                    // Cờ báo chuỗi auto đang chạy (để tránh xung với hẹn giờ)
bool prevRelay1State = LOW;                  // Lưu trạng thái trước đó của relay1 (dùng cho log hẹn giờ)
bool firstRun = true;                        // Flasg lần chạy đầu để tránh log sai ở lần đầu
bool lastWiFiState = false;                  // false = OFF, true = ON

unsigned long entryTime = 0;        // Thời điểm nhiệt độ bắt đầu vào vùng lý tưởng
bool isTiming = false;              // Cờ báo đang đếm ngược 15 phút
bool isVentilating = false;         // Cờ báo quạt đang chạy

const unsigned long WAIT_TIME = 15 * 60 * 1000UL; // 15 phút chờ đợi
bool triggerOTA = false; // Thêm cờ báo hiệu cập nhật OTA

bool thresholdChanged = false; // Cờ báo hiệu có sự thay đổi ngưỡng từ App

String firmwareURL = "https://raw.githubusercontent.com/huyminhct2-jpg/Nhanam_V3_Khoi/main/Nhanam_V3/build/esp8266.esp8266.nodemcuv2/Nhanam_V3.ino.bin";

bool kichAmRunning = false; // khai báo nút kích ẩm 

bool startKichAm = false;  
// =====================================================
// ================== LCD & BLYNK ======================
// =====================================================
LiquidCrystal_I2C lcd(0x27, 16, 2);           // Khởi tạo LCD I2C (địa chỉ 0x27), 16x2 ký tự
WidgetLED led(V0);                            // LED ảo trên Blynk (V0) dùng báo trạng thái hệ thống
WidgetLED led1(V12);                          // LED ảo hiển thị trạng thái relay1 trên app (V12)
WidgetLED led2(V13);                          // LED ảo relay2 (V13)
WidgetLED led3(V14);                          // LED ảo relay3 (V14)
WidgetLED led4(V15);                          // LED ảo relay4 (V15)

BlynkTimer timer;                             // Timer nội bộ của Blynk (tương tự SimpleTimer)
unsigned long lastTime1 = 0;                  // Thời điểm cập nhật hiển thị lần cuối (ms)
unsigned long lastLCDResetTime = 0;           // Thời điểm reset LCD lần gần nhất (ms)
const unsigned long LCDResetInterval = 1800000UL; // 30 phút = 1,800,000 ms -> dùng reset LCD để tránh lỗi hiển thị

// =====================================================
// ====== WIFI & OTA (CẤU HÌNH) =======================
// =====================================================
char auth[] = "Du6Pr1bGzApF0znkVvBPmMwy6H3F41mV"; // Token Blynk 
const char* ssid = "NHA NAM";                    // SSID WiFi
const char* pass = "123456789@@";                // Mật khẩu WiFi

// Thông tin cho OTA (web update)
const char* host = "ESP";                        // Tên mDNS
const char* updatePath = "/update";              
const char* updateUsername = "admin";            
const char* updatePassword = "admin";            

// Cấu hình IP tĩnh (nếu muốn dùng IP tĩnh)
IPAddress staticIP(192,168,1,251);               
IPAddress gateway(192,168,1,1);                  
IPAddress subnet(255,255,255,0);                 
IPAddress dns1(8,8,8,8);                         
IPAddress dns2(8,8,4,4);                         

ESP8266WebServer webServer(81);                   // Web server chạy ở cổng 81 để phục vụ OTA UI
ESP8266HTTPUpdateServer httpUpdater;              // Đối tượng hỗ trợ OTA update

// =====================================================
// ================= GIAO DIỆN OTA (HTML) ==============
// =====================================================
// Trang chính của giao diện OTA (lưu trong PROGMEM để tiết kiệm RAM)
const char MainPage[] PROGMEM = R"=====( 
<!DOCTYPE html>
<html>
<head><title>HUY MINH PRO</title>
<style>body{text-align:center;}</style>
<meta name="viewport" content="width=device-width,user-scalable=0" charset="UTF-8">
</head>
<body>
<div>
<img src='https://huyminhcantho.com/wp-content/uploads/2022/05/cropped-275263160_458673896042314_8099340625003815361_n.jpg.webp' height='300px' width='330px'>
</div>
<div>
<button onclick="window.location.href='/update'">UPLOAD FIRMWARE</button><br><br>
<a href='https://huyminhcantho.com/'>XEM THÊM</a>
</div>
</body>
</html>
)=====";

// =====================================================
// ======================= SETUP =======================
// =====================================================
void setup() {
  Serial.begin(115200);                       // Khởi động Serial Monitor ở 115200 baud
  Serial.println("KHOI DONG HE THONG...");    // In log khởi động

  // ---------- KẾT NỐI WIFI ----------
  WiFi.mode(WIFI_STA);                        // Thiết lập ESP ở chế độ Station (kết nối router)
  WiFi.config(staticIP, gateway, subnet, dns1, dns2); // Áp IP tĩnh (nếu router cho phép)
  WiFi.begin(ssid, pass);   
  while (WiFi.waitForConnectResult() != WL_CONNECTED) { // Chờ kết nối, nếu lỗi sẽ retry
    Serial.println("WiFi failed, retrying...");         // In thông báo lỗi
    delay(2000);                                        // Chờ 2 giây rồi thử lại
  }
  Serial.print("Địa chỉ IP: ");              // In thông báo
  Serial.println(WiFi.localIP());            // In IP hiện tại của ESP

  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov"); //  ĐỂ CÀI MÚI GIỜ VIỆT NAM (+7)

  // ---------- KHỞI ĐỘNG OTA (mDNS + Web OTA) ----------
  MDNS.begin(host);                          // Khởi tạo mDNS (ví dụ truy cập http://ESP.local)
  MDNS.addService("http", "tcp", 81);        // Đăng ký service http ở cổng 81
  httpUpdater.setup(&webServer, updatePath, updateUsername, updatePassword); // Cấu hình OTA
  webServer.on("/", [] {                     // Nếu truy cập root "/" trả về MainPage
    webServer.send(200, "text/html", MainPage);
  });
  webServer.begin();                         // Bắt đầu chạy web server
  Serial.println("OTA SERVER STARTED");      // In log

  // ---------- KẾT NỐI BLYNK ----------
  Blynk.begin(auth, ssid, pass, "blynkserver.ddns.net", 8080); // Kết nối tới server Blynk (tùy chỉnh domain/cổng)

  // Blynk.virtualWrite(V31, "Da khoi dong lai");
  Blynk.run();
  delay(500);

  // ---------- LED NHẤP NHÁY TRÊN BLYNK (KIỂM TRA HOẠT ĐỘNG) ----------
  timer.setInterval(1000, []() {             // Mỗi 1000 ms (1s) chạy lambda
    if (led.getValue()) led.off(); else led.on(); // Toggle LED ảo V0 để báo hoạt động
  });
  // BẠN THÊM DÒNG NÀY VÀO ĐÂY NHÉ (Giúp mạch tự kiểm tra WiFi mỗi 3 giây)
  timer.setInterval(3000L, checkWiFiStatus);

  // ---------- CẤU HÌNH CHÂN RELAY ----------
  pinMode(relay1, OUTPUT);  // Đặt relay1 là OUTPUT
  pinMode(relay2, OUTPUT);  // Đặt relay2 là OUTPUT
  pinMode(relay3, OUTPUT);  // Đặt relay3 là OUTPUT
  pinMode(relay4, OUTPUT);  // Đặt relay4 là OUTPUT
  allOff();                 // Tắt toàn bộ relay lúc khởi động (an toàn)

  Blynk.syncAll();          // Đồng bộ các giá trị ảo từ server (để lấy trạng thái, ngưỡng,...)

  // ---------- KHỞI ĐỘNG LCD ----------
  lcd.begin(16, 2);         // Khởi tạo LCD 16x2
  lcd.clear();              // Xóa màn hình
  lcd.backlight();          // Bật đèn nền LCD
  lcd.setCursor(0, 0);      // Con trỏ cột 0, hàng 0
  lcd.print("NAM SACH CAN THO");  // In dòng tiêu đề lên LCD
  lcd.setCursor(0, 1);      // Dòng 2, cột 0
  lcd.print("IP:");         // In "IP:"
  lcd.setCursor(3, 1);      // Cột 3 (để in IP kế bên)
  lcd.print(WiFi.localIP()); // In địa chỉ IP thực tế lên LCD
  lastLCDResetTime = millis();// Lưu thời điểm hiện tại (dùng cho reset LCD sau interval)
  delay(1000);              // Dừng 1 giây để đọc thông tin
}


// =====================================================
// ================== CHUYỂN CHẾ ĐỘ (AUTO <-> MANUAL) ===
// =====================================================
// Lưu ý: BLYNK_WRITE(V3) nhận giá trị khi người dùng bật/tắt widget gắn V3
BLYNK_WRITE(V3)
{
    Modeactive = param.asInt();

    switch (Modeactive)
    {
        case 1:   // AUTO
            Serial.println("CHE DO AUTO");
            stopAuto = false;
            kichAmRunning = false;
            ShowMode("AUTO");
            break;

        case 2:
            Serial.println("CHE DO THU CONG");
            stopAuto = true;
            kichAmRunning = false;
            allOff();
            ShowMode("THU CONG");
            break;

        case 3:
            if (!kichAmRunning)
            {
                stopAuto = true;
                ShowMode("KICH AM");
                startKichAm = true;
            }
            break;
      }
}
// =====================================================
// ====== BLYNK NHẬN NGƯỠNG & NÚT (THỦ CÔNG) ===========
// =====================================================
// Những BLYNK_WRITE này chỉ nhận giá trị vào các biến tương ứng
BLYNK_WRITE(V4) { Reft1 = param.asInt(); thresholdChanged = true;}  // Ngưỡng nhiệt độ trên
BLYNK_WRITE(V5) { Reft2 = param.asInt(); thresholdChanged = true;}  // Ngưỡng nhiệt độ dưới 
BLYNK_WRITE(V6) { Refh1 = param.asInt(); thresholdChanged = true;}  // Ngưỡng độ ẩm 1
BLYNK_WRITE(V7) { Refh2 = param.asInt(); thresholdChanged = true;}  // Ngưỡng độ ẩm 2

// Nút điều khiển thủ công: đọc trạng thái và điều khiển relay tương ứng
BLYNK_WRITE(V8) { button1 = param.asInt(); }  // Lưu trạng thái nút relay1 (manual)
BLYNK_WRITE(V9) { button2 = param.asInt(); }  // Lưu trạng thái nút relay2
BLYNK_WRITE(V10){ button3 = param.asInt(); }  // Lưu trạng thái nút relay3
BLYNK_WRITE(V11){ button4 = param.asInt(); }  // Lưu trạng thái nút relay4
BLYNK_WRITE(V32){ RefML   = param.asInt(); thresholdChanged = true;}  // Lưu ngưỡng máy lạnh
// =====================================================
// ================= HẸN GIỜ TRÊN BLYNK (TIMER) =========
// =====================================================
// Mảng timerStates lưu 12 trạng thái hẹn giờ (có thể mapping đến V16..V27)
int timerStates[12] = {0}; // Khởi tạo 12 timer trạng thái = 0

// BLYNK_WRITE_DEFAULT bắt tất cả các pin ảo không được khai báo riêng
BLYNK_WRITE_DEFAULT() {
  int pin = request.pin;                    // Lấy pin ảo vừa gửi
  if (pin >= V16 && pin <= V27) {           // Nếu pin nằm trong dải V16..V27
    int index = pin - V16;                  // Tính chỉ số mảng tương ứng
    timerStates[index] = param.asInt();    // Lưu trạng thái vào mảng
    updateRelay1();                         // Cập nhật trạng thái relay1 dựa trên timer
  }
}

// =====================================================
// ================== HẸN GIỜ RELAY 1 ===================
// =====================================================
void updateRelay1() {
  if (Modeactive == 1 && autoRunning) return; // Nếu đang auto và autoRunning true => không để timer override

  bool anyOn = false;                         // Kiểm tra có timer nào bật không
  for (int i = 0; i < 12; i++) {
    if (timerStates[i]) { anyOn = true; break; } // Nếu bất kỳ timer nào bằng 1 => anyOn true
  }

  digitalWrite(relay1, anyOn ? HIGH : LOW);   // Nếu có bất kỳ timer on -> ghi HIGH (tắt hoặc bật tùy module relay)
  led1.setValue(anyOn * 255);                 // Cập nhật LED ảo (255=on, 0=off)

  // Ghi log để biết trạng thái thay đổi
  if (anyOn && prevRelay1State == LOW) Serial.println("⏰ Hẹn giờ: BẬT relay1");
  else if (!anyOn && prevRelay1State == HIGH && !firstRun) Serial.println("⏰ Hẹn giờ: TẮT relay1");

  prevRelay1State = anyOn;                    // Lưu trạng thái hiện tại
  firstRun = false;                           // Đã qua lần đầu
}

// =====================================================
// ================== LCD VÀ HIỂN THỊ DỮ LIỆU ===========
// =====================================================
void resetLCD() {
  Serial.println(F("==> RESET LCD SAU 30P")); // In log reset LCD
  lcd.clear();                                // Xóa LCD
  lcd.begin(16, 2);                            // Khởi tạo lại LCD (đảm bảo)
  lcd.backlight();                             // Bật đèn nền
  lcd.setCursor(0, 0); lcd.print("NAM SACH CAN THO"); // In tiêu đề
  lcd.setCursor(0, 1); lcd.print("IP:"); lcd.setCursor(3, 1);
  lcd.print(WiFi.localIP());                   // In IP
}
void ShowMode(const char* mode)
{
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("CHE DO: ");
    lcd.print(mode);
    delay(1000);
}
void Display() {
  if (millis() - lastTime1 > 2000) {  
    sht30.get(); // 🔥 BẮT BUỘC PHẢI CÓ       
    float t = sht30.cTemp;                     
    float h = sht30.humidity;                  
    
    // CHỈ VẼ LÊN LCD KHI CÓ WIFI, MẤT WIFI THÌ NHƯỜNG MÀN HÌNH CHO CẢNH BÁO
    if (WiFi.status() == WL_CONNECTED) {
        Blynk.virtualWrite(V1, t);                 
        Blynk.virtualWrite(V2, h);  

        lcd.clear();                               
        lcd.setCursor(0, 0); lcd.print("Nhiet do:"); 
        lcd.setCursor(9, 0); lcd.print(t);          
        lcd.setCursor(14, 0); lcd.write(223);       
        lcd.setCursor(15, 0); lcd.print("C");       
        lcd.setCursor(0, 1); lcd.print("Do am   :"); 
        lcd.setCursor(9, 1); lcd.print(h);          
        lcd.setCursor(14, 1); lcd.print("%");       
    }
    lastTime1 = millis();                       
  }
}

// =====================================================
// ============ PROGRESS OTA % (V40) ====================
// =====================================================
void otaProgress(int percent) {
  Blynk.virtualWrite(V40, percent);
  Blynk.run();
  lcd.setCursor(0, 1);
  lcd.print("Dang tai: ");
  lcd.print(percent);
  lcd.print("%   ");
}
void fakeProgressBar() {
  for (int i = 0; i <= 90; i += 10) {
    otaProgress(i);
    delay(300);
  }
}

// =====================================================
// ============ OTA UPDATE FIRMWARE GITHUB ==============
// =====================================================
void updateFirmwareGitHub() {
  Serial.println("🚀 Dang update firmware tu GitHub...");

  Blynk.virtualWrite(V31, "Bat dau tai...");
  Blynk.run();   
  delay(100);    

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Update code...");

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15000); // Tăng thời gian chờ mạng lên 15s cho an toàn

  // Hàm tính %
  ESPhttpUpdate.onProgress([](int cur, int total) {
      static int last_p = -1;
      int p = (cur * 100) / total;
      
      ESP.wdtFeed(); // Vuốt ve Watchdog chống treo chip

      if (p != last_p) {
          // Luôn cập nhật lên LCD từng phần trăm
          lcd.setCursor(0, 1);
          lcd.print("Dang tai: ");
          lcd.print(p);
          lcd.print("%   ");

          // Báo lên App Blynk từng phần trăm
          if (p == 100) {
              String timeStr = getUpdateTimestamp();
              // Nếu lấy được giờ thì ghi "update code : ngày giờ", nếu mạng lag chưa kịp lấy thì ghi chữ "OK"
              if (timeStr != "") {
                  Blynk.virtualWrite(V31, "update code : " + timeStr);
              } else {
                  Blynk.virtualWrite(V31, "update code : OK (Restarting)");
              }
          } else {
              Blynk.virtualWrite(V31, "Dang tai: " + String(p) + "%");
          }
          Blynk.virtualWrite(V40, p);   
          
          // Ép mạch gửi dữ liệu lên Blynk ngay lập tức cho mỗi 1%
          Blynk.run(); 
          
          last_p = p;
      }
  });

  // BẬT LẠI chế độ tự động Reset phần cứng của thư viện (rất an toàn)
  ESPhttpUpdate.rebootOnUpdate(true);

  // Bắt đầu tải
  t_httpUpdate_return ret = ESPhttpUpdate.update(client, firmwareURL);

  // Phía dưới này CHỈ chạy nếu việc tải file bị LỖI (do mạng rớt hoặc link sai)
  switch (ret) {
    case HTTP_UPDATE_FAILED: {
      String err = ESPhttpUpdate.getLastErrorString();
      Serial.println("❌ Update FAIL: " + err);
      lcd.clear(); lcd.setCursor(0, 0); lcd.print("UPDATE FAIL!");
      Blynk.virtualWrite(V31, "Loi mang! Thu lai"); 
      Blynk.run();       
      delay(3000);
      break;
    }
    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("⚠️ Khong co firmware moi!");
      lcd.clear(); lcd.setCursor(0, 0); lcd.print("NO UPDATE");
      Blynk.virtualWrite(V31, "Khong co ban moi"); 
      Blynk.run();
      delay(2000);
      break;
    case HTTP_UPDATE_OK:
      // Không cần viết gì ở đây vì thư viện đã tự ngắt điện reset phần cứng từ trước rồi
      break;
  }
}

// =====================================================
// ================= Nút trên blynk ====================
// =====================================================
BLYNK_WRITE(V30) {
  if (param.asInt() == 1) {
    triggerOTA = true; // Chỉ bật cờ, không tải trực tiếp ở đây
  }
}

// =====================================================
// ======================= LOOP CHÍNH ==================
// =====================================================
void loop() {
  MDNS.update();               // Cập nhật mDNS (giữ mDNS alive)
  webServer.handleClient();    // Xử lý request từ webServer (OTA)
  Blynk.run();                 // Chạy Blynk (xử lý các sự kiện, kết nối)
  timer.run();                 // Chạy timer của Blynk (các hàm setInterval)
  if (startKichAm)
    {
        startKichAm = false;
        KichAm36Phut();
    }

  if (triggerOTA) {
    triggerOTA = false;
    updateFirmwareGitHub();
  }

  // Đọc cảm biến SHT30: sht30.get() trả về 0 nếu đọc thành công
  if (sht30.get() == 0) {
    float t = sht30.cTemp;     // Nhiệt độ hiện tại
    float h = sht30.humidity;  // Độ ẩm hiện tại

    // Nếu Modeactive == 0 (AUTO theo logic gốc) và stopAuto == false -> chạy chế độ tự động
    if (Modeactive == 1 && !stopAuto && !kichAmRunning){
      autoRunning = true;
      Display();

      // ===== 1. NHIỆT ĐỘ CỰC CAO (CHẠY MÁY LẠNH - RELAY 3) =====
      if (t >= RefML){
          Serial.println("NHIET DO CUC CAO -> BAT MAY LANH");
          resetVentilator();
          
          TCucCao(); if (stopAuto || thresholdChanged) { autoRunning = false; allOff(); thresholdChanged = false; return;}
          
          allOff(); // Tắt toàn bộ relay (bao gồm máy lạnh)
          lcd.clear();
          lcd.setCursor(0,0);
          lcd.print("Ktra Nhiet Do");
          
          // Cho hệ thống nghỉ 2 phút (60000ms) để khí lạnh tỏa đều và cảm biến đo lại
          delayWithCheck(2*60000); 
      }

      // ===== 2. NHIỆT ĐỘ CAO (QUẠT + ẨM) =====
      else if (t > Reft1 && t < RefML){
          Serial.println("NHIET DO CAO");
          resetVentilator();
          THigh1(); if (stopAuto || thresholdChanged) { autoRunning = false; allOff(); thresholdChanged = false; return;}
          THigh2(); if (stopAuto || thresholdChanged) { autoRunning = false; allOff(); thresholdChanged = false; return;}
          THigh3(); if (stopAuto || thresholdChanged) { autoRunning = false; allOff(); thresholdChanged = false; return;}
          THigh4(); if (stopAuto || thresholdChanged) { autoRunning = false; allOff(); thresholdChanged = false; return;}
      }

      // ===== ĐỘ ẨM THẤP =====
      else if (h < Refh2){
          Serial.println("DO AM THAP");
          resetVentilator();
          HLow1(); if (stopAuto || thresholdChanged) { autoRunning = false; allOff(); thresholdChanged = false; return;}
          HLow2(); if (stopAuto || thresholdChanged) { autoRunning = false; allOff(); thresholdChanged = false; return;}
          HLow3(); if (stopAuto || thresholdChanged) { autoRunning = false; allOff(); thresholdChanged = false; return;}

          allOff();
          lcd.clear();
          lcd.setCursor(0,0);
          lcd.print("Ktra Do Am");

          delayWithCheck(60000);
      }

      // ===== ĐỘ ẨM CAO =====
      else if (h > Refh1){
          Serial.println("DO AM CAO");
          resetVentilator();
          HHigh(); if (stopAuto || thresholdChanged) { autoRunning = false; allOff(); thresholdChanged = false; return;}

          allOff();
          lcd.clear();
          lcd.setCursor(0,0);
          lcd.print("Ktra Do Am");
          delayWithCheck(60000);
      }
      else {
          // Kiểm tra vùng nhiệt độ lý tưởng (Reft1 đến Reft2)
          if (t <= Reft1 && t >= Reft2) {
              
              // Nếu quạt CHƯA bật, tiến hành đếm giờ
              if (!isVentilating) {
                  // Mới vào vùng lý tưởng lần đầu
                  if (!isTiming) { 
                      entryTime = millis();
                      isTiming = true;
                      Serial.println("Vùng lý tưởng, bắt đầu đếm 15 phút...");
                  }
                  // Đang trong quá trình đếm ngược 15 phút
                  else {
                      if (millis() - entryTime >= WAIT_TIME) {
                          isTiming = false;
                          isVentilating = true;       // Kích hoạt cờ báo quạt đang chạy
                          digitalWrite(relay4, HIGH); // Bật quạt thông gió
                          led4.on();
                          Serial.println("Đủ 15 phút, bật quạt thông gió!");
                      }
                  }
              } 
              // Nếu quạt ĐÃ bật (isVentilating == true) -> Giữ nguyên trạng thái BẬT
              else {
                  digitalWrite(relay4, HIGH);
                  led4.on();
              }
          }
          else {
              // Nhiệt độ KHÔNG CÒN lý tưởng -> Tắt quạt và reset toàn bộ cờ đếm
              if (isVentilating || isTiming) {
                  Serial.println("Thoát vùng lý tưởng -> Tắt quạt thông gió & Reset bộ đếm");
              }
              
              digitalWrite(relay4, LOW);
              led4.off();
              isTiming = false;
              isVentilating = false;
              
              allOff(); 
              lcd.clear();
              lcd.setCursor(0, 0); lcd.print("Nhiet do ko OK");
              Display();
              delayWithCheck(5000);
          }
      }
          autoRunning = false;
      }
    else {
    Display();

    if (!kichAmRunning)
    {
        digitalWrite(relay1, button1 || timerStates[0]);
        digitalWrite(relay2, button2);
        digitalWrite(relay3, button3);
        digitalWrite(relay4, button4);
    }

    led1.setValue(digitalRead(relay1)*255);
    led2.setValue(digitalRead(relay2)*255);
    led3.setValue(digitalRead(relay3)*255);
    led4.setValue(digitalRead(relay4)*255);
}
  } 
  else {
    // Nếu sht30.get() != 0 => sensor không đọc được
    Serial.println("SENSOR NOT FOUND");
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("SENSOR NOT FOUND"); // Hiển thị lỗi sensor trên LCD
  }

  // Nếu đã vượt quá thời gian reset LCD, gọi resetLCD()
  if (millis() - lastLCDResetTime > LCDResetInterval) {
    resetLCD();                   // Reset lại LCD (clear + re-init)
    lastLCDResetTime = millis();  // Cập nhật thời điểm reset
  }
}

// =====================================================
// ============== CÁC HÀM CHẾ ĐỘ TỰ ĐỘNG ===============
// =====================================================
// Các hàm tự động (THigh / HLow / HHigh) đều dùng chung một hàm xử lý autoAction()
// -> Giúp giảm trùng lặp code, dễ đọc và dễ bảo trì

// Hàm xử lý chung cho các hành động tự động
void autoAction(const char* line1, const char* line2, 
                int r1State, int r2State, int r3State,  // Thêm r3State vào đây
                unsigned long waitMs) { 
  lcd.clear(); 
  lcd.setCursor(0, 0); lcd.print(line1); 
  lcd.setCursor(0, 1); lcd.print(line2); 
  digitalWrite(relay1, r1State); led1.setValue(r1State * 255);
  digitalWrite(relay2, r2State); led2.setValue(r2State * 255);
  digitalWrite(relay3, r3State); led3.setValue(r3State * 255); 
  delayWithCheck(waitMs); 
}

// =======================================================
// =================hàm các trường hợp ===================
// =======================================================

void TCucCao() { autoAction("Nhiet do CUC CAO", "May Lanh: 30p", LOW, LOW, HIGH, 30 * 60000); }

void THigh1()  { autoAction("Nhiet do cao!", "Quat_3P", HIGH, LOW, LOW, 3 * 60000); }
void THigh2()  { autoAction("Nhiet do cao!", "Am_2P", LOW, HIGH, LOW, 2 * 60000); }
void THigh3()  { autoAction("Nhiet do cao!", "Quat_1P", HIGH, LOW, LOW, 1 * 60000); }
void THigh4()  { autoAction("Kiem tra lai", "Nhiet do...", LOW, LOW, LOW, 1 * 60000); }


void HLow1() { autoAction("Do am thap", "Bat: tao am", LOW, HIGH, LOW, 3 * 60000); }    
void HLow2() { autoAction("Do am thap!", "Bat quat", HIGH, HIGH, LOW, 5000); }       
void HLow3() { autoAction("Do am thap!", "Bat tao lan 2", LOW, HIGH, LOW, 3 * 60000); } 

void HHigh() { autoAction("Do am cao", "", HIGH, LOW, LOW, 3 * 60000); }

// =======================================================
// ================= // hàm kích ẩm 36ph =================
// =======================================================
void KichAm36Phut()
{
    thresholdChanged = false;
    kichAmRunning = true;

    for (int i = 0; i < 3 && kichAmRunning; i++)
    {
        autoAction("KICH AM", "Tao am 10P", LOW, HIGH, LOW, 10 * 60000UL);
        if (!kichAmRunning) break;

        autoAction("KICH AM", "Quat 2P", HIGH, LOW, LOW, 2 * 60000UL);
    }

   kichAmRunning = false;
   thresholdChanged = false;
   allOff();

    Blynk.virtualWrite(V29, 0);

    // Chỉ tự về AUTO nếu người dùng vẫn đang ở chế độ KÍCH ẨM
    if (Modeactive == 3)
    {
        Modeactive = 1;
        stopAuto = false;
        Blynk.virtualWrite(V3, 1);
        ShowMode("AUTO");
    }
}
// =====================================================
// ================= // Tắt tất cả relay =================
// =====================================================
void allOff() {
  // Tắt tất cả relay
  digitalWrite(relay1, LOW); led1.off();
  digitalWrite(relay2, LOW); led2.off();
  digitalWrite(relay3, LOW); led3.off();
  digitalWrite(relay4, LOW); led4.off();
}
void resetVentilator() {
  if (isVentilating || isTiming) {
      Serial.println("Thoát vùng lý tưởng -> Tắt quạt thông gió & Reset bộ đếm");
      digitalWrite(relay4, LOW);
      led4.off();
      isTiming = false;
      isVentilating = false;
  }
}
// =====================================================
// ================= KIỂM TRA WIFI =====================
// =====================================================
void checkWiFiStatus() {
  bool currentState = (WiFi.status() == WL_CONNECTED);

  if (!currentState) { 
    // Nếu mất mạng: Luôn hiện cảnh báo
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print(">> WIFI OFF <<");
    lcd.setCursor(0, 1); lcd.print("Dang ket noi... ");
  } 
  else if (currentState && !lastWiFiState) {
    // Nếu vừa có mạng lại: Báo trên màn hình 2 giây
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("WIFI CONNECTED! ");
    delay(2000); 
    lcd.clear();
  }

  lastWiFiState = currentState;
}
void delayWithCheck(unsigned long duration) {
    unsigned long start = millis();
    while (millis() - start < duration) {
        
        // ================= THOÁT KHẨN =================
        if (triggerOTA || (!kichAmRunning && thresholdChanged) || (stopAuto && !kichAmRunning)) break;
        
        // ================= CẬP NHẬT HỆ THỐNG =================
        sht30.get();
        if (!autoRunning) Display();
        
        Blynk.run();
        
        // Lệnh timer.run() này sẽ tự động gọi hàm checkWiFiStatus ngầm để báo lỗi mất mạng!
        timer.run(); 
        
        webServer.handleClient();
        yield();
    }
}
// Hàm lấy và làm đẹp chuỗi thời gian
String getUpdateTimestamp() {
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  
  // Nếu chưa lấy được giờ mạng, trả về chữ trống
  if (timeinfo->tm_year < 120) return ""; 
  
  char buffer[30];
  // Định dạng: ngày/tháng/năm giờ:phút
  sprintf(buffer, "%02d/%02d/%04d %02d:%02d", 
          timeinfo->tm_mday, timeinfo->tm_mon + 1, timeinfo->tm_year + 1900,
          timeinfo->tm_hour, timeinfo->tm_min);
          
  return String(buffer);
}
