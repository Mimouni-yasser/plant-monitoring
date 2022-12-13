#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "Arduino.h"
#include "fb_gfx.h"
#include <ArduinoJson.h>

typedef struct {
        httpd_req_t *req;
        size_t len;
} jpg_chunking_t;

httpd_handle_t camera_httpd = NULL;
bool enable = false;


static size_t jpg_encode_stream(void * arg, size_t index, const void* data, size_t len){
    jpg_chunking_t *j = (jpg_chunking_t *)arg;
    if(!index){
        j->len = 0;
    }
    if(httpd_resp_send_chunk(j->req, (const char *)data, len) != ESP_OK){
        return 0;
    }
    j->len += len;
    return len;
}


static esp_err_t capture_handler(httpd_req_t *req){
    if(enable == false)
    {
        Serial.println("system not ready");
        httpd_resp_send(req, "system not ready", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    else {
    digitalWrite(4, HIGH);
    delay(200);
    camera_fb_t * fb = NULL;
    esp_err_t res = ESP_OK;
    int64_t fr_start = esp_timer_get_time();


    fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    size_t out_len, out_width, out_height;
    uint8_t * out_buf;
    bool s;
    size_t fb_len = 0;
    if(fb->format == PIXFORMAT_JPEG){
        fb_len = fb->len;
        res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
    } else {
         jpg_chunking_t jchunk = {req, 0};
         res = frame2jpg_cb(fb, 80, jpg_encode_stream, &jchunk)?ESP_OK:ESP_FAIL;
         httpd_resp_send_chunk(req, NULL, 0);
         fb_len = jchunk.len;
    }
    esp_camera_fb_return(fb);
    int64_t fr_end = esp_timer_get_time();
    Serial.printf("JPG: %uB %ums\n", (uint32_t)(fb_len), (uint32_t)((fr_end - fr_start)/1000));
    delay(200);
    digitalWrite(4, LOW);
    return res;
    }
}


static esp_err_t post_handler(httpd_req_t *req){
    char *buf = (char*)malloc(req->content_len+1);
    
    // Read the data for the request
    if(httpd_req_recv(req, buf, req->content_len)<=0)
         return ESP_FAIL;
    Serial.println(buf);
    DynamicJsonDocument doc(20);
    deserializeJson(doc, buf);
    if(doc["enable"] == "true"){

        enable = true;
    }else{
        enable = false;
    }
    // Send response
    const char* resp_str = enable ? "system enabled":"system disbabled";
    httpd_resp_send(req, resp_str, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}


void startCameraServer(){
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    httpd_uri_t index_get_uri = {
        .uri       = "/",
        .method    = HTTP_GET,
        .handler   = capture_handler,
        .user_ctx  = NULL
    };
     httpd_uri_t index_post_uri = {
        .uri       = "/",
        .method    = HTTP_POST,
        .handler   = post_handler,
        .user_ctx  = NULL
    };
    
    
    Serial.printf("Starting web server on port: '%d'\n", config.server_port);
    if (httpd_start(&camera_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(camera_httpd, &index_get_uri);
        httpd_register_uri_handler(camera_httpd, &index_post_uri);
    }

    config.server_port += 1;
    config.ctrl_port += 1;
}
