#include "esp_camera.h"
#include <WiFi.h>
#include "esp_timer.h"
#include "img_converters.h"
#include "Arduino.h"
#include "fb_gfx.h"
#include "soc/soc.h"             // disable brownout problems
#include "soc/rtc_cntl_reg.h"    // disable brownout problems
#include "esp_http_server.h"

// network credentials
const char* ssid = "PorQueFi";
const char* password = "idontknowjakedoestho";

// set static IP address
IPAddress local_IP(192, 168, 1, 184);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 0, 0);

#define PART_BOUNDARY "123456789000000000000987654321"

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

#define MOTOR_2_PIN_1    12
#define MOTOR_2_PIN_2    13
#define MOTOR_1_PIN_1    14
#define MOTOR_1_PIN_2    15
#define FLASH_PIN        4

static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t camera_httpd = NULL;
httpd_handle_t stream_httpd = NULL;

static const char PROGMEM INDEX_HTML[] = R"rawliteral(
<html>
  <head>
    <title>ESP32-CAM</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
      *,
      *:before,
      *:after {
        box-sizing: border-box;
      }
      
      body {
        background: #fff;
      }
      
      div {
        padding: 0;
        margin: 0;
        width: 4.5em;
        height: 4.5em;
        position: absolute;
        border-radius: 20%;
      }
      
      .wrapper {
        position: relative;
        height: 15em;
        margin: auto;
        top: 5em;
        bottom: 0;
        left: 0;
        right: 0;
      }
      
      .center {
        z-index: 2;
        top: 5em;
      }
      
      .center-circle {
        position: static;
        background: #fff;
        border-radius: 100%;
        margin: auto;
        width: 75%;
        height: 75%;
        z-index: 3;
      }
      
      .up,
      .right,
      .down,
      .left,
      .center {
        display: flex;
        background: #777;
        padding: 12px;
        border: 1px solid #fff;
      }
      
      .right {
        top: 5em;
        left: 5em;
      }
      
      .left {
        top: 5em;
        right: 5em;
      }
      
      .down {
        top: 10em;
      }
     
      img {  width: auto ;
        max-width: 100% ;
        height: auto ; 
      }
    </style>
  </head>
  <body>
    <img src="" id="photo" >
    <div class="wrapper">
      <div class="center" ontouchstart="toggleCheckbox('stop');">
        <div class="center-circle"></div>
      </div>
      <div class="up direction" ontouchstart="toggleCheckbox('forward');" ontouchend="toggleCheckbox('stop');"></div>
      <div class="right direction" ontouchstart="toggleCheckbox('right');" ontouchend="toggleCheckbox('stop');"></div>
      <div class="down direction" ontouchstart="toggleCheckbox('backward');" ontouchend="toggleCheckbox('stop');"></div>
      <div class="left direction" ontouchstart="toggleCheckbox('left');" ontouchend="toggleCheckbox('stop');"></div>
    </div>
    <script>
      function toggleCheckbox(x) {
        var xhr = new XMLHttpRequest();
        xhr.open("GET", "/action?go=" + x, true);
        xhr.send();
      }
      window.onload = document.getElementById("photo").src = window.location.href.slice(0, -1) + ":81/stream";
    </script>
  </body>
</html>
)rawliteral";

static esp_err_t index_handler(httpd_req_t *req){
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, (const char *)INDEX_HTML, strlen(INDEX_HTML));
}

static esp_err_t stream_handler(httpd_req_t *req){
  camera_fb_t * fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t * _jpg_buf = NULL;
  char * part_buf[64];

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if(res != ESP_OK){

    digitalWrite(FLASH_PIN, 1);
    delay(100);
    digitalWrite(FLASH_PIN, 0);
    delay(50);
    digitalWrite(FLASH_PIN, 1);
    delay(100);
    digitalWrite(FLASH_PIN, 0);
  
    return res;
  }

  while(true){
    fb = esp_camera_fb_get();
    
    if (!fb) {
      Serial.println("Camera capture failed");
      res = ESP_FAIL;
    } else {
      
      if(fb->width > 400){
        
        if(fb->format != PIXFORMAT_JPEG){
          bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
          esp_camera_fb_return(fb);
          fb = NULL;
          
          if(!jpeg_converted){
            Serial.println("JPEG compression failed");
            res = ESP_FAIL;
          }
        } else {
          _jpg_buf_len = fb->len;
          _jpg_buf = fb->buf;
        }
      }
    }
    
    if(res == ESP_OK){
      size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    }
    
    if(res == ESP_OK){
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }
    
    if(res == ESP_OK){
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }
    
    if(fb){
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    } else if(_jpg_buf){
      free(_jpg_buf);
      _jpg_buf = NULL;
    }
    
    if(res != ESP_OK){
      digitalWrite(FLASH_PIN, 1);
      delay(100);
      digitalWrite(FLASH_PIN, 0);
      delay(50);
      digitalWrite(FLASH_PIN, 1);
      delay(100);
      digitalWrite(FLASH_PIN, 0);
      
      break;
    }
  }
  
  return res;
}

static esp_err_t cmd_handler(httpd_req_t *req){
  char*  buf;
  size_t buf_len;
  char variable[32] = {0,};
  
  buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1) {
    buf = (char*)malloc(buf_len);
    
    if(!buf){
      httpd_resp_send_500(req);

      digitalWrite(FLASH_PIN, 1);
      delay(100);
      digitalWrite(FLASH_PIN, 0);
      delay(50);
      digitalWrite(FLASH_PIN, 1);
      delay(100);
      digitalWrite(FLASH_PIN, 0);
    
      return ESP_FAIL;
    }
    
    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
      
      if (!(httpd_query_key_value(buf, "go", variable, sizeof(variable)) == ESP_OK)) {
        free(buf);
        httpd_resp_send_404(req);

        digitalWrite(FLASH_PIN, 1);
        delay(100);
        digitalWrite(FLASH_PIN, 0);
        delay(50);
        digitalWrite(FLASH_PIN, 1);
        delay(100);
        digitalWrite(FLASH_PIN, 0);
        
        return ESP_FAIL;
      }
    } else {
      free(buf);
      httpd_resp_send_404(req);

      digitalWrite(FLASH_PIN, 1);
      delay(100);
      digitalWrite(FLASH_PIN, 0);
      delay(50);
      digitalWrite(FLASH_PIN, 1);
      delay(100);
      digitalWrite(FLASH_PIN, 0);
      
      return ESP_FAIL;
    }
    
    free(buf);
  } else {
    httpd_resp_send_404(req);

    digitalWrite(FLASH_PIN, 1);
    delay(100);
    digitalWrite(FLASH_PIN, 0);
    delay(50);
    digitalWrite(FLASH_PIN, 1);
    delay(100);
    digitalWrite(FLASH_PIN, 0);
    
    return ESP_FAIL;
  }

  sensor_t * s = esp_camera_sensor_get();
  int res = 0;
  
  if(!strcmp(variable, "forward")) {
    Serial.println("FORWARD");
    digitalWrite(MOTOR_1_PIN_1, 1);
    digitalWrite(MOTOR_1_PIN_2, 0);
    digitalWrite(MOTOR_2_PIN_1, 1);
    digitalWrite(MOTOR_2_PIN_2, 0);
  }
  else if(!strcmp(variable, "left")) {
    Serial.println("LEFT");
    digitalWrite(MOTOR_1_PIN_1, 0);
    digitalWrite(MOTOR_1_PIN_2, 1);
    digitalWrite(MOTOR_2_PIN_1, 1);
    digitalWrite(MOTOR_2_PIN_2, 0);
  }
  else if(!strcmp(variable, "right")) {
    Serial.println("RIGHT");
    digitalWrite(MOTOR_1_PIN_1, 1);
    digitalWrite(MOTOR_1_PIN_2, 0);
    digitalWrite(MOTOR_2_PIN_1, 0);
    digitalWrite(MOTOR_2_PIN_2, 1);
  }
  else if(!strcmp(variable, "backward")) {
    Serial.println("BACKWARD");
    digitalWrite(MOTOR_1_PIN_1, 0);
    digitalWrite(MOTOR_1_PIN_2, 1);
    digitalWrite(MOTOR_2_PIN_1, 0);
    digitalWrite(MOTOR_2_PIN_2, 1);
  }
  else if(!strcmp(variable, "stop")) {
    Serial.println("STOP");
    digitalWrite(MOTOR_1_PIN_1, 0);
    digitalWrite(MOTOR_1_PIN_2, 0);
    digitalWrite(MOTOR_2_PIN_1, 0);
    digitalWrite(MOTOR_2_PIN_2, 0);
  }
  else {
    res = -1;
  }

  if(res){
    return httpd_resp_send_500(req);

    digitalWrite(FLASH_PIN, 1);
    delay(100);
    digitalWrite(FLASH_PIN, 0);
    delay(50);
    digitalWrite(FLASH_PIN, 1);
    delay(100);
    digitalWrite(FLASH_PIN, 0);
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

void startCameraServer(){
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  
  httpd_uri_t index_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = index_handler,
    .user_ctx  = NULL
  };

  httpd_uri_t cmd_uri = {
    .uri       = "/action",
    .method    = HTTP_GET,
    .handler   = cmd_handler,
    .user_ctx  = NULL
  };
  
  httpd_uri_t stream_uri = {
    .uri       = "/stream",
    .method    = HTTP_GET,
    .handler   = stream_handler,
    .user_ctx  = NULL
  };
  
  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &index_uri);
    httpd_register_uri_handler(camera_httpd, &cmd_uri);
  }
  
  config.server_port += 1;
  config.ctrl_port += 1;
  
  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
  }
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); //disable brownout detector
  
  pinMode(MOTOR_1_PIN_1, OUTPUT);
  pinMode(MOTOR_1_PIN_2, OUTPUT);
  pinMode(MOTOR_2_PIN_1, OUTPUT);
  pinMode(MOTOR_2_PIN_2, OUTPUT);
  pinMode(FLASH_PIN, OUTPUT);
  
  Serial.begin(115200);
  Serial.setDebugOutput(false);
  
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
  
  if(psramFound()){
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }
  
  // Camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    
    Serial.printf("Camera init failed with error 0x%x", err);
    digitalWrite(FLASH_PIN, 1);
    delay(100);
    digitalWrite(FLASH_PIN, 0);
    delay(50);
    digitalWrite(FLASH_PIN, 1);
    delay(100);
    digitalWrite(FLASH_PIN, 0);
    
    return;
  }
  
  // Wi-Fi connection
  if(!WiFi.config(local_IP, gateway, subnet)) {
    Serial.println("STA Failed to configure");
  }
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("Camera Stream Ready! Go to: http://");
  Serial.println(WiFi.localIP());

  digitalWrite(FLASH_PIN, 1);
  delay(100);
  digitalWrite(FLASH_PIN, 0);
  
  // start streaming web server
  startCameraServer();
}

void loop() {
  
}
