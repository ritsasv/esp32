#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Modelo AI Thinker ESP32-CAM
#define CAMERA_MODEL_AI_THINKER

#if defined(CAMERA_MODEL_AI_THINKER)
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22
#endif

#define FLASH_LED_PIN 4

// OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_SDA 14
#define OLED_SCL 15

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// WiFi
const char* ssid = "Ritsa";
const char* password = "12345678";

IPAddress local_IP(192, 168, 0, 128);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);

WebServer server(80);
bool flashOn = false;
bool streamOn = true;

// Scroll
String scrollText = "  RITSA ELECTRONICA ESP32 CAM  ";
int scrollPos = SCREEN_WIDTH;

// Tarea OLED (texto en una sola línea)
void scrollOLEDTask(void * parameter) {
  for (;;) {
    display.clearDisplay();
    display.setTextSize(1);  // Tamaño pequeño para que entre
    display.setTextColor(SSD1306_WHITE);

    int textWidth = scrollText.length() * 6; // 6px por carácter
    display.setCursor(scrollPos, 26); // vertical centrado aprox.

    display.print(scrollText);
    display.display();

    scrollPos -= 1;
    if (scrollPos < -textWidth) {
      scrollPos = SCREEN_WIDTH;
    }

    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}

// Cámara
void startCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if (psramFound()) {
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_QQVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Error al iniciar cámara: 0x%x\n", err);
    return;
  }
}

// Página Web
void handleRoot() {
  String html = R"rawliteral(
    <html><head><title>ESP32-CAM</title></head><body>
    <h2>Stream en vivo</h2>
    <div id="videoContainer">
      <img id="cam" src="/stream" width="320"/>
    </div><br><br>
    <button onclick="toggleFlash()">Encender/Apagar Flash</button>
    <button onclick="toggleStream()" id="streamBtn">Apagar Stream</button>

    <script>
      let controller = null;
      let streamOn = true;

      const delay = (ms) => new Promise(res => setTimeout(res, ms));

      const toggleFlash = async () => {
        await toggleStream();
        await delay(500);
        await fetch('/flash');
        await delay(500);
        await toggleStream();
      };

      const toggleStream = async () => {
        const container = document.getElementById('videoContainer');
        const btn = document.getElementById('streamBtn');
        const cam = document.getElementById('cam');
        if (streamOn) {
          if (cam) cam.src = "";
          btn.textContent = "Encender Stream";
        } else {
          if (cam) cam.src = "/stream";
          btn.textContent = "Apagar Stream";
        }
        streamOn = !streamOn;
      };
    </script>
    </body></html>
  )rawliteral";

  server.send(200, "text/html", html);
}

void handleFlash() {
  flashOn = !flashOn;
  digitalWrite(FLASH_LED_PIN, flashOn ? HIGH : LOW);
  Serial.println(flashOn ? "Flash encendido" : "Flash apagado");
  server.send(200, "text/plain", flashOn ? "Flash encendido" : "Flash apagado");
}

void handleStream() {
  WiFiClient client = server.client();
  String response = "HTTP/1.1 200 OK\r\n";
  response += "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";
  server.sendContent(response);

  while (client.connected()) {
    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) continue;

    response = "--frame\r\n";
    response += "Content-Type: image/jpeg\r\n";
    response += "Content-Length: " + String(fb->len) + "\r\n\r\n";
    server.sendContent(response);
    server.sendContent((const char *)fb->buf, fb->len);
    server.sendContent("\r\n");
    esp_camera_fb_return(fb);

    delay(100);
  }
}

void handleJPG() {
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Error al capturar imagen");
    server.send(500, "text/plain", "Error al capturar imagen");
    return;
  }

  server.sendHeader("Content-Type", "image/jpeg");
  server.send_P(200, "image/jpeg", (char *)fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

void setup() {
  Serial.begin(115200);
  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW);

  Wire.begin(OLED_SDA, OLED_SCL);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Conectando WiFi...");
  display.display();

  WiFi.config(local_IP, gateway, subnet);
  WiFi.begin(ssid, password);
  Serial.print("Conectando a WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n¡Conectado!");
  Serial.println(WiFi.localIP());

  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.println("WiFi conectado!");
  display.print("IP: ");
  display.println(WiFi.localIP());
  display.display();
  delay(3000);

  scrollPos = SCREEN_WIDTH;

  xTaskCreatePinnedToCore(
    scrollOLEDTask,
    "ScrollTask",
    2048,
    NULL,
    1,
    NULL,
    0
  );

  startCamera();

  server.on("/", handleRoot);
  server.on("/flash", HTTP_GET, handleFlash);
  server.on("/jpg", HTTP_GET, handleJPG);
  server.on("/stream", HTTP_GET, handleStream);
  server.begin();
  Serial.println("Servidor iniciado");
}

void loop() {
  server.handleClient();
}
