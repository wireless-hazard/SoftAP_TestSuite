/* Console example — various system commands

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include "esp_log.h"
#include "esp_console.h"
#include "esp_system.h"
#include "esp_sleep.h"
#include "esp_spi_flash.h"
#include "driver/rtc_io.h"
#include "driver/uart.h"
#include "linenoise/linenoise.h"
#include "argtable3/argtable3.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "cmd_testsuite.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
/* The examples use WiFi configuration that you can set via project configuration menu.

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_WIFI_CHANNEL   CONFIG_ESP_WIFI_CHANNEL
#define EXAMPLE_MAX_STA_CONN       CONFIG_ESP_MAX_STA_CONN

static const char *TAG = "cmd_testsuite";
static const char system_info_command[] = "{\"command\": \"system_info\"}";

static bool soft_ap_on = false;
static int sockfd = -1;

static void register_startap(void);
static void register_clear(void);
static void register_turn_wifi_off(void);
static void register_socket(void);
static void register_socket_close(void);
static void register_connect_socket(void);
static void register_send_system_info(void);

void register_testsuite(void){
	register_startap();
	register_clear();
	register_turn_wifi_off();
	register_socket();
	register_socket_close();
    register_connect_socket();
    register_send_system_info();
}


static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "station "MACSTR" join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGW(TAG, "station "MACSTR" leave, AID=%d",
                 MAC2STR(event->mac), event->aid);
        if (sockfd != -1){
            sockfd = -1;
            ESP_LOGW(TAG, "Shutting down socket");
            shutdown(sockfd, 0);
            close(sockfd);
        }
    }
}

static void wifi_init_softap(void){

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .ssid_len = strlen(EXAMPLE_ESP_WIFI_SSID),
            .channel = EXAMPLE_ESP_WIFI_CHANNEL,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .max_connection = EXAMPLE_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        },
    };
    if (strlen(EXAMPLE_ESP_WIFI_PASS) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    
}

static int startap(int argc, char **argv){
	if(soft_ap_on){
		ESP_LOGW(TAG,"Access Point already on!!");
		return ESP_OK;
	}
	ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_softap finished. SSID:%s password:%s channel:%d",
             EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS, EXAMPLE_ESP_WIFI_CHANNEL);
    soft_ap_on = true;
    return ESP_OK;

}

static void register_startap(void){
	const esp_console_cmd_t cmd = {
        .command = "softap_start",
        .help = "Start the Access Point",
        .hint = NULL,
        .func = &startap,
    };
    wifi_init_softap();
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}

static int clear_screen(int argc, char **argv){
	linenoiseClearScreen();
    return ESP_OK;
}

static void register_clear(void){
	const esp_console_cmd_t cmd = {
		.command = "clear",
		.help = "clear terminal screen",
		.hint = NULL,
		.func = &clear_screen,
	};
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}

static int turn_wifi_off(int argc, char **argv){
	if(!soft_ap_on){
		ESP_LOGW(TAG,"Access Point already off!!");
		return ESP_OK;
	}
	soft_ap_on = false;
	esp_wifi_deauth_sta(0);
    esp_wifi_stop();
    return ESP_OK;
}

static void register_turn_wifi_off(void){
	const esp_console_cmd_t cmd = {
		.command = "softap_stop",
		.help = "Stop the Access Point",
		.hint = NULL,
		.func = &turn_wifi_off,
	};
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}

static int open_socket(int argc, char **argv){
	if (!soft_ap_on){
		ESP_LOGE(TAG,"Access Point turned off!!");
		if(sockfd != -1){
			ESP_LOGE(TAG, "Shutting down socket");
            shutdown(sockfd, 0);
            close(sockfd);
            sockfd = -1;
        }
		return ESP_OK;
	}else if(sockfd != -1){
		ESP_LOGW(TAG,"Socket already open!");
		return ESP_OK;
	}
    // socket create and varification
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        ESP_LOGE(TAG,"socket creation failed...\n");
		return ESP_OK;
    }
    else{
        ESP_LOGI(TAG,"Socket successfully created..\n");

    }
    return ESP_OK;
}

static void register_socket(void){
	const esp_console_cmd_t cmd = {
		.command = "socket_open",
		.help = "open a client socket",
		.hint = NULL,
		.func = &open_socket,
	};
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd));
}

static int close_socket(int argc, char **argv){
	if(sockfd != -1){
		ESP_LOGI(TAG, "Shutting down socket");
        shutdown(sockfd, 0);
        close(sockfd);
        sockfd = -1;
		return ESP_OK;

    }else{ 
    	ESP_LOGE(TAG,"Socket already closed!");
		return ESP_OK;
	}
}

static void register_socket_close(void){
	const esp_console_cmd_t cmd = {
		.command = "socket_close",
		.help = "close a client socket",
		.hint = NULL,
		.func = &close_socket,
	};
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd));	
}

static struct {
    struct arg_str *ip;
    struct arg_end *end;
} connect_args;

static int connect_socket(int argc, char **argv){

    connect_args.ip->sval[0] = "";

    if (sockfd == -1) {
        ESP_LOGE(TAG,"socket isn't open!!\n");
        return ESP_OK;
    }

    int nerrors = arg_parse(argc, argv, (void **) &connect_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, connect_args.end, argv[0]);
        return ESP_OK;
    }
    struct sockaddr_in dest_addr;
    bzero(&dest_addr, sizeof(dest_addr));
  
    // assign IP, PORT
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_addr.s_addr = inet_addr(connect_args.ip->sval[0]);
    dest_addr.sin_port = htons(8001);

    
    if (connect(sockfd, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) != 0) {
        ESP_LOGE(TAG,"Connection with the server failed...\n");
        return ESP_OK;
    }
    else
        ESP_LOGI(TAG,"Connected to the server: %s\n",connect_args.ip->sval[0]);

    return ESP_OK;
}

static void register_connect_socket(void){
    connect_args.ip = arg_str1(NULL, NULL, "<ip>", "ip of the socket server to connect to");
    connect_args.end = arg_end(0);
    const esp_console_cmd_t cmd = {
        .command = "socket_connect",
        .help = "connect to a server socket",
        .hint = NULL,
        .func = &connect_socket,
        .argtable = &connect_args
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd));
}

static struct {
    struct arg_int *message;
    struct arg_end *end;
} system_info_args;

static int send_system_info(int argc, char **argv){

    char packet[1024] = {0,};

    if (sockfd == -1) {
        ESP_LOGE(TAG,"socket isn't open!!\n");
        return ESP_OK;
    }

    int nerrors = arg_parse(argc, argv, (void **) &system_info_args);

    if (nerrors != 0) {
        arg_print_errors(stderr, system_info_args.end, argv[0]);
        return ESP_OK;
    }

    int err;
    switch (system_info_args.message->ival[0]){
        case 0:
            err = send(sockfd,&system_info_command,sizeof(system_info_command),0);
            if (err < 0){
                ESP_LOGE(TAG,"System info JSON not sent!! Error: %s",strerror(errno));
                return ESP_OK;
            }else{
                ESP_LOGI(TAG,"System info JSON sent!!");
            }
        break;
        default:
            ESP_LOGE(TAG,"INVALID CHOICE!!!");
            return ESP_OK;
    }
    err = recv(sockfd,packet,sizeof(packet),0);
    if (err < 0){
        ESP_LOGE(TAG,"Nothing received back from the socket server!!");
        return ESP_OK;
    }
    ESP_LOGW(TAG,"\n%s\n",packet);
    return ESP_OK;
}

static void register_send_system_info(void){
    system_info_args.message = arg_int1("c", "command", "<int>", "which command to use");
    system_info_args.end = arg_end(0);
    const esp_console_cmd_t cmd = {
        .command = "socket_send",
        .help = "send command to socket",
        .hint = NULL,
        .func = &send_system_info,
        .argtable = &system_info_args
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd));
}