/*	codigoFinal.cpp
 *
 *  Nomes: 	Eduardo Guilherme	185698
 *			Gustavo Gouveia		186766
 *			Luiz Ricardo		185675
 *			Manoela Alvares		190565
 *			Nichollas Farias	182348
 */

// Inclusão das bibliotecas necessárias para configuração do ESP32-CAM
#include "esp_camera.h"
#include <WiFi.h>
#include "esp_timer.h"
#include "img_converters.h"
#include "Arduino.h"
#include "fb_gfx.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_http_server.h"

// Definição do SSID e Password para fazer a conexão WiFi
const char *ssid = "HUAWEI Mate 20 Pro"; // Configura o nome da rede que será acessado para controlar o robo através do celular
const char *password = "d32e5f843426";   // Senha para a rede

#define PART_BOUNDARY "123456789000000000000987654321"

// Definição dos todos os pinos do ESP32-CAM disponíveis para utilização e dos pinos à serem utilizados para os motores e sensor
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27

#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

// Pinos à serem utilizados pelos motores
#define MOTOR_1_PIN_1 14
#define MOTOR_1_PIN_2 15
#define MOTOR_2_PIN_1 13
#define MOTOR_2_PIN_2 12

#define MQ2 4 // Definição do pino para o sensor de gás (GPIO4)

float sensorGas; // Definição da variável que será responsável por armazenar o valor analógico do sensor

static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t camera_httpd = NULL;
httpd_handle_t stream_httpd = NULL;

// Configuração da interface que será realizado os comandos de movimentação dos motores e da vizualização da câmera
static const char PROGMEM INDEX_HTML[] = R"rawliteral(                 
<html>
  <head>
    <title>RDMA - GP03</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
      body { font-family: Arial; text-align: center; margin:0px auto; padding-top: 30px;}
      table { margin-left: auto; margin-right: auto; }
      td { padding: 8 px; }
      .button {
        background-color: #2f4468;
        border: none;
        color: white;
        padding: 10px 20px;
        text-align: center;
        text-decoration: none;
        display: inline-block;
        font-size: 18px;
        margin: 6px 3px;
        cursor: pointer;
        -webkit-touch-callout: none;
        -webkit-user-select: none;
        -khtml-user-select: none;
        -moz-user-select: none;
        -ms-user-select: none;
        user-select: none;
        -webkit-tap-highlight-color: rgba(0,0,0,0);
      }
      img {  width: auto ;
        max-width: 100% ;
        height: auto ; 
      }
    </style>
  </head>
  <body>
    <h1>RDMA - GP03</h1>
    <img src="" id="photo" >
    <table>
      <tr><td colspan="3" align="center"><button class="button" onmousedown="toggleCheckbox('forward');" ontouchstart="toggleCheckbox('forward');" onmouseup="toggleCheckbox('stop');" ontouchend="toggleCheckbox('stop');">Forward</button></td></tr>
      <tr><td align="center"><button class="button" onmousedown="toggleCheckbox('left');" ontouchstart="toggleCheckbox('left');" onmouseup="toggleCheckbox('stop');" ontouchend="toggleCheckbox('stop');">Left</button></td><td align="center"><button class="button" onmousedown="toggleCheckbox('stop');" ontouchstart="toggleCheckbox('stop');">Stop</button></td><td align="center"><button class="button" onmousedown="toggleCheckbox('right');" ontouchstart="toggleCheckbox('right');" onmouseup="toggleCheckbox('stop');" ontouchend="toggleCheckbox('stop');">Right</button></td></tr>
      <tr><td colspan="3" align="center"><button class="button" onmousedown="toggleCheckbox('backward');" ontouchstart="toggleCheckbox('backward');" onmouseup="toggleCheckbox('stop');" ontouchend="toggleCheckbox('stop');">Backward</button></td></tr>                   
    </table>
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

/*
 * As funções de "handler" são responsáveis por configurarem uma resposta como um arquivo HTML
 * e envia o conteúdo deste arquivo para o cliente que fez a solitação GET na raiz do servidor.
 *
 * Por exemplo:
 *
 * No caso do movimento dos motores, quando um botão é clicado, ele fornece o conteúdo gerado
 * à este código e a partir disso, as ações de movimentação dos motores são feitas.
 */

static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, (const char *)INDEX_HTML, strlen(INDEX_HTML));
}

static esp_err_t stream_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len = 0;
    uint8_t *_jpg_buf = NULL;
    char *part_buf[64];

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK)
    {
        return res;
    }

    while (true)
    {
        fb = esp_camera_fb_get();
        if (!fb)
        {
            Serial.println("Camera capture failed");
            res = ESP_FAIL;
        }
        else
        {
            if (fb->width > 400)
            {
                if (fb->format != PIXFORMAT_JPEG)
                {
                    bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
                    esp_camera_fb_return(fb);
                    fb = NULL;
                    if (!jpeg_converted)
                    {
                        Serial.println("JPEG compression failed");
                        res = ESP_FAIL;
                    }
                }
                else
                {
                    _jpg_buf_len = fb->len;
                    _jpg_buf = fb->buf;
                }
            }
        }
        if (res == ESP_OK)
        {
            size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);
            res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
        }
        if (res == ESP_OK)
        {
            res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
        }
        if (res == ESP_OK)
        {
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        }
        if (fb)
        {
            esp_camera_fb_return(fb);
            fb = NULL;
            _jpg_buf = NULL;
        }
        else if (_jpg_buf)
        {
            free(_jpg_buf);
            _jpg_buf = NULL;
        }
        if (res != ESP_OK)
        {
            break;
        }
    }
    return res;
}

static esp_err_t cmd_handler(httpd_req_t *req)
{
    char *buf;
    size_t buf_len;
    char variable[32] = {
        0,
    };

    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1)
    {
        buf = (char *)malloc(buf_len);
        if (!buf)
        {
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK)
        {
            if (httpd_query_key_value(buf, "go", variable, sizeof(variable)) == ESP_OK)
            {
            }
            else
            {
                free(buf);
                httpd_resp_send_404(req);
                return ESP_FAIL;
            }
        }
        else
        {
            free(buf);
            httpd_resp_send_404(req);
            return ESP_FAIL;
        }
        free(buf);
    }
    else
    {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    sensor_t *s = esp_camera_sensor_get();
    int res = 0;

    // Verifica o parâmetro recebido e realiza a ação correspondente
    if (!strcmp(variable, "forward"))
    { // Comandos ao clicar o botão "Forward", o robô ira para frente
        Serial.println("Forward");
        digitalWrite(MOTOR_1_PIN_1, 1);
        digitalWrite(MOTOR_1_PIN_2, 0);
        digitalWrite(MOTOR_2_PIN_1, 0);
        digitalWrite(MOTOR_2_PIN_2, 1);
    }
    else if (!strcmp(variable, "left"))
    { // Comandos ao clicar o botão "Left", o robô ira para esquerda
        Serial.println("Left");
        digitalWrite(MOTOR_1_PIN_1, 0);
        digitalWrite(MOTOR_1_PIN_2, 1);
        digitalWrite(MOTOR_2_PIN_1, 0);
        digitalWrite(MOTOR_2_PIN_2, 1);
    }
    else if (!strcmp(variable, "right"))
    { // Comandos ao clicar o botão "Right", o robô ira para direita
        Serial.println("Right");
        digitalWrite(MOTOR_1_PIN_1, 1);
        digitalWrite(MOTOR_1_PIN_2, 0);
        digitalWrite(MOTOR_2_PIN_1, 1);
        digitalWrite(MOTOR_2_PIN_2, 0);
    }
    else if (!strcmp(variable, "backward"))
    { // Comandos ao clicar o botão "Backward", o robô ira para trás
        Serial.println("Backward");
        digitalWrite(MOTOR_1_PIN_1, 0);
        digitalWrite(MOTOR_1_PIN_2, 1);
        digitalWrite(MOTOR_2_PIN_1, 1);
        digitalWrite(MOTOR_2_PIN_2, 0);
    }
    else if (!strcmp(variable, "stop"))
    {
        Serial.println("Stop");
        digitalWrite(MOTOR_1_PIN_1, 0);
        digitalWrite(MOTOR_1_PIN_2, 0);
        digitalWrite(MOTOR_2_PIN_1, 0);
        digitalWrite(MOTOR_2_PIN_2, 0);
    }
    else
    {
        res = -1;
    }

    if (res)
    {
        return httpd_resp_send_500(req);
    }

    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, NULL, 0);
}

//  startCameraServer() - Função responsável por iniciar o servidor HTTP para fornecer acesso à camera e outras rotas definidas
void startCameraServer()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG(); // Inicialização do servidor com valores padrão de configuração
    config.server_port = 80;                        // Definição da porta do servidor como 80

    /*
     * Definição de estruturas "httpd_uri_t" para a rota ".uri".
     * O manipulador da rota ".handler" é chamado quando uma solitação GET for feita para a rota.
     */

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
        .user_ctx = NULL};

    httpd_uri_t cmd_uri = {
        .uri = "/action",
        .method = HTTP_GET,
        .handler = cmd_handler,
        .user_ctx = NULL};
    httpd_uri_t stream_uri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = stream_handler,
        .user_ctx = NULL};
    if (httpd_start(&camera_httpd, &config) == ESP_OK)
    {
        httpd_register_uri_handler(camera_httpd, &index_uri);
        httpd_register_uri_handler(camera_httpd, &cmd_uri);
    }
    config.server_port += 1;
    config.ctrl_port += 1;
    if (httpd_start(&stream_httpd, &config) == ESP_OK)
    {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
    }
}

void setup()
{
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

    // Definição dos pinos como OUTPUT ou INPUT

    pinMode(MOTOR_1_PIN_1, OUTPUT);
    pinMode(MOTOR_1_PIN_2, OUTPUT);
    pinMode(MOTOR_2_PIN_1, OUTPUT);
    pinMode(MOTOR_2_PIN_2, OUTPUT);
    pinMode(MQ2, INPUT);

    Serial.begin(9600);
    Serial2.begin(9600, SERIAL_8N1, 3, 1); // Inicialização do serial para que o ESP envie as informações ao LoRa, e ao TTN - (RX0 - 3 | TX0 - 1)
    Serial2.println("AT+JOIN");            // Antes do loop(), o esp32 tentará fazer a conexão com o TTN a partir do AT+JOIN

    Serial.setDebugOutput(false);

    // Configuração de parãmetros principais para inicialização da camera do ESP32-CAM
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

    if (psramFound())
    {
        config.frame_size = FRAMESIZE_VGA;
        config.jpeg_quality = 10;
        config.fb_count = 2;
    }
    else
    {
        config.frame_size = FRAMESIZE_SVGA;
        config.jpeg_quality = 12;
        config.fb_count = 1;
    }

    // Inicialização da câmera
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK)
    {
        Serial.printf("Camera init failed with error 0x%x", err);
        return;
    }

    // Utiliza as varíaveis definidas de SSID e Password para definir o Ip local para a inicialização do server
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }
    Serial.println("");
    Serial.println("WiFi connected");

    Serial.print("Camera Stream Ready! Go to: http://");
    Serial.println(WiFi.localIP());

    // Inicialização do server para realizar a movimentação do motores, ver as imagens do ESP32 e os valores do sensor MQ2
    startCameraServer();
}

void loop()
{
    sensorGas = analogRead(MQ2);   // Variável do sensor armazena o valor analógico do sensor MQ2
    enviaDados(String(sensorGas)); // Envia os dados para o server TTN (Parâmetro: String())
    delay(5000);                   // Delay para que a cada 5 segundos, o loop execute a função novamente para os valores do sensor ao TTN
}

void enviaDados(String dado) // Função que recebe como parâmetro em String(), a variável do valor do sensor e envia para o server TTN
{
    Serial2.println("AT+SEND=2:" + dado);
}