/* Console example — various system commands

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <math.h>
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

static const char restart_command[] = "{\"command\": \"restart\"}";
static const char reset_command[] = "{\"command\": \"reset\"}";
static const char stop_command[] = "{\"command\": \"stop_transmission\"}";
static const char system_info_command[] = "{\"command\": \"system_info\"}";
static const char init_transmissionBEGIN[] = "{\"command\": \"start_sensor\",\"freq\": ";
static const char init_transmissionEND[] = ",\"GYRO\": 1,\"ACCEL\": 1}";
static const char stop_transmission[] = "{\"command\": \"stop_transmission\"}";

static bool soft_ap_on = false;
static bool streaming = false;
static bool sending_on = false;
static bool generic_buffer = false;
static int sockfd = -1;

static void register_startap(void);
static void register_clear(void);
static void register_turn_wifi_off(void);
static void register_socket(void);
static void register_socket_close(void);
static void register_connect_socket(void);
static void register_send_system_info(void);
static void register_receive_stream_pckt(void);
static void register_generic_receiver(void);
static void register_stations_list(void);
static void register_print_packets(void);

void register_testsuite(void){
	register_startap();
	register_clear();
	register_turn_wifi_off();
	register_socket();
	register_socket_close();
    register_connect_socket();
    register_send_system_info();
    register_receive_stream_pckt();
    register_generic_receiver();
	register_stations_list();
    register_print_packets();
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
        .command = "ap_start",
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
    
    sockfd = -1;
    streaming = false;
    generic_buffer = false;
    ESP_LOGW(TAG, "Shutting down socket");
    shutdown(sockfd, 0);
    close(sockfd);
    
    return ESP_OK;
}

static void register_turn_wifi_off(void){
	const esp_console_cmd_t cmd = {
		.command = "ap_stop",
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
        ESP_LOGE(TAG,"socket creation failed: %s\n",strerror(errno));
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
        ESP_LOGE(TAG,"Connection with the server failed: error = %s\n",strerror(errno));
        if (errno == 128){
            sockfd = -1;    
        }
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
        .command = "connect_to",
        .help = "connect to a server socket",
        .hint = NULL,
        .func = &connect_socket,
        .argtable = &connect_args
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd));
}

enum which_message{info,stop,restart,reset};

static struct {
    struct arg_int *message;
    struct arg_end *end;
} system_info_args;

static void task_send_system_info(void *pvParameters){

    int err;
    switch (system_info_args.message->ival[0]){
        case info:
            err = send(sockfd,&system_info_command,sizeof(system_info_command),0);
            if (err < 0){
                ESP_LOGE(TAG,"System info JSON not sent!! Error: %s",strerror(errno));
                sending_on = false;
                vTaskDelete(NULL);
            }else{
                ESP_LOGI(TAG,"System info JSON sent!!");
            }
        break;
        case restart:
            err = send(sockfd,&restart_command,sizeof(restart_command),0);
            if (err < 0){
                ESP_LOGE(TAG,"Restart command JSON not sent!! Error: %s",strerror(errno));
                sending_on = false;
                vTaskDelete(NULL);
            }else{
                ESP_LOGI(TAG,"Restart command JSON sent!!");
            }
        break;
        case reset:
            err = send(sockfd,&reset_command,sizeof(reset_command),0);
            if (err < 0){
                ESP_LOGE(TAG,"Reset command JSON not sent!! Error: %s",strerror(errno));
                sending_on = false;
                vTaskDelete(NULL);
            }else{
                ESP_LOGI(TAG,"Reset command JSON sent!!");
            }
        break;
        case stop:
            err = send(sockfd,&stop_command,sizeof(stop_command),0);
            if (err < 0){
                ESP_LOGE(TAG,"Stop command JSON not sent!! Error: %s",strerror(errno));
                sending_on = false;
                vTaskDelete(NULL);
            }else{
                ESP_LOGI(TAG,"Stop command JSON sent!!");
                streaming = false;
            }
        break;
        default:
            ESP_LOGE(TAG,"INVALID CHOICE!!!");
            sending_on = false;
            vTaskDelete(NULL);
    }
    sending_on = false;
    vTaskDelete(NULL);

}

static int send_system_info(int argc, char **argv){

    if (sockfd == -1) {
        ESP_LOGE(TAG,"socket isn't open!!\n");
        return ESP_OK;
    }

    int nerrors = arg_parse(argc, argv, (void **) &system_info_args);

    if (nerrors != 0) {
        arg_print_errors(stderr, system_info_args.end, argv[0]);
        return ESP_OK;
    }

    if(!sending_on){
        xTaskCreatePinnedToCore(task_send_system_info, "socket send system info", 8096,NULL, 6, NULL,1);
        sending_on = true;
    }

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

typedef struct{
    uint8_t ID0;
    int64_t time;
    int16_t accelX;
    int16_t accelY;
    int16_t accelZ;
    int16_t gyroX;
    int16_t gyroY;
    int16_t gyroZ;
    uint16_t battery;
    uint8_t IDfinal;
}__attribute__((__packed__)) battery_packet;

static struct {
    struct arg_int *sensor_frequency;
    struct arg_int *number_of_pckts;
    struct arg_int *rounds;
    struct arg_end *end;
} packet_stream_args;

/*static void task_stream_pckts(void *pvParameters){
    uint16_t limit_of_packets = (uint16_t)packet_stream_args.number_of_pckts->ival[0];
    int sensor_frequency = packet_stream_args.sensor_frequency->ival[0];
    int rounds = packet_stream_args.rounds->ival[0];

    int16_t tempo_anterior = 0;
    int16_t tempo_atual = 0;
    unsigned long long int corrompido = 0;
    unsigned long long int total_pacotes = 0;
    float frequencia = 0;

    char init_transmission[100] = {0,};
    sprintf(init_transmission,"%s%d%s",init_transmissionBEGIN,sensor_frequency,init_transmissionEND);
    uint8_t packet[24];
    battery_packet data = {0,};

    int err;

    float media_freq = 0;
    
    for(int j = 0; j < rounds;j++){

        tempo_atual = 0;
        tempo_anterior = 0;
        corrompido = 0;
        total_pacotes = 0;
        frequencia = 0;
        media_freq = 0;

        err = send(sockfd,&init_transmission,sizeof(init_transmission),0);//MSG_DONTWAIT);
        if (err < 0){
            ESP_LOGE(TAG,"NAO ENVIADO\n");
        }
        for(int i = 0; i < limit_of_packets;i++){
        
            total_pacotes++;
            if(!streaming){
                ESP_LOGI(TAG,"(%d/%d)Frequencia Media(%lld pacotes) = %f Hz (esperado: %dHz)\n",j+1,rounds,total_pacotes,media_freq/total_pacotes,sensor_frequency);
                ESP_LOGW(TAG,"Number of corrupted packets: %llu\n",corrompido);
                vTaskDelete(NULL);
            }
            err = recv(sockfd,packet,sizeof(packet),0);
            if (err < 0){
                ESP_LOGE(TAG,"error no socket\n");
                break;
            }
            memcpy(&data.ID0,packet,sizeof(uint8_t));
            memcpy(&data.IDfinal,packet+23,sizeof(uint8_t));
        
            if ((data.ID0 != 0) || (data.IDfinal != 255)){
                corrompido++;
                continue;
            }
            memcpy(&data.time,packet+1,sizeof(data.time));
            memcpy(&data.accelX,packet+9,sizeof(data.accelX));
            memcpy(&data.accelY,packet+11,sizeof(data.accelY));
            memcpy(&data.accelZ,packet+13,sizeof(data.accelZ));
            memcpy(&data.gyroX,packet+15,sizeof(data.gyroX));
            memcpy(&data.gyroY,packet+17,sizeof(data.gyroY));
            memcpy(&data.gyroZ,packet+19,sizeof(data.gyroZ));
            memcpy(&data.battery,packet+21,sizeof(uint16_t));
        

            tempo_atual = data.time - tempo_anterior;
            frequencia = (1/(float)tempo_atual)*pow(10,6);

            if ((frequencia<=0) || (frequencia>4100)){
                corrompido++;
                tempo_anterior = data.time;
                continue;    
            }

            tempo_anterior = data.time;

            media_freq += frequencia;
        }
        ESP_LOGI(TAG,"(%d/%d)Frequencia Media(%lld pacotes) = %f Hz (esperado: %dHz)\n",j+1,rounds,total_pacotes,media_freq/total_pacotes,sensor_frequency);
        ESP_LOGW(TAG,"Number of corrupted packets: %llu\n",corrompido);
        err = send(sockfd,&stop_transmission,sizeof(stop_transmission),0);//MSG_DONTWAIT);
        if (err < 0){ 
            ESP_LOGE(TAG,"NAO ENVIADO\n");
        }
        vTaskDelay(1000/portTICK_PERIOD_MS);
    }
    streaming = false;
    vTaskDelete(NULL);
}*/

static battery_packet last_iteration[150] = {0,};

static void task_stream_pckts(void *pvParameters){
    uint16_t limit_of_packets = (uint16_t)packet_stream_args.number_of_pckts->ival[0];
    int sensor_frequency = packet_stream_args.sensor_frequency->ival[0];
    int rounds = packet_stream_args.rounds->ival[0];

    int16_t tempo_anterior = 0;
    int16_t tempo_atual = 0;
    unsigned long long int corrompido = 0;
    unsigned long long int total_pacotes = 0;
    float frequencia = 0;

    char init_transmission[100] = {0,};
    sprintf(init_transmission,"%s%d%s",init_transmissionBEGIN,sensor_frequency,init_transmissionEND);
    uint8_t packet[75*sizeof(battery_packet)];
    battery_packet data[150] = {0,};

    int err;

    float media_freq = 0;
    
    for(int j = 0; j < rounds;j++){

        tempo_atual = 0;
        tempo_anterior = 0;
        corrompido = 0;
        total_pacotes = 0;
        frequencia = 0;
        media_freq = 0;

        err = send(sockfd,&init_transmission,sizeof(init_transmission),0);//MSG_DONTWAIT);
        if (err < 0){
            ESP_LOGE(TAG,"NAO ENVIADO\n");
        }
        for(int i = 0; i < limit_of_packets;i = i + 150){
        
            total_pacotes += 150;
            if(!streaming){
                ESP_LOGI(TAG,"(%d/%d)Frequencia Media(%lld pacotes) = %f Hz (esperado: %dHz)\n",j+1,rounds,total_pacotes,media_freq/total_pacotes,sensor_frequency);
                ESP_LOGW(TAG,"Number of corrupted packets: %llu\n",corrompido);
                vTaskDelete(NULL);
            }
            for(int metade = 0; metade <=75; metade = metade + 75){
                err = recv(sockfd,packet,sizeof(packet),0);
                memcpy(data+metade,packet,sizeof(packet));
            }
            if (err < 0){
                ESP_LOGE(TAG,"error no socket\n");
                break;
            }

            for (int k = 0; k < (sizeof(data)/sizeof(battery_packet)); k++){
                
                // ESP_LOGI(TAG,"time[%d] %lld",k,data[k].time);

                if ((data[k].ID0 != 0) || (data[k].IDfinal != 255)){
                    corrompido++;
                    continue;
                }

                tempo_atual = data[k].time - tempo_anterior;
                frequencia = (1/(float)tempo_atual)*pow(10,6);
                tempo_anterior = data[k].time;
                media_freq += frequencia;
            }
        }
        err = send(sockfd,&stop_transmission,sizeof(stop_transmission),0);//MSG_DONTWAIT);
        if (err < 0){ 
            ESP_LOGE(TAG,"NAO ENVIADO\n");
        }
        ESP_LOGI(TAG,"(%d/%d)Frequencia Media(%lld pacotes) = %f Hz (esperado: %dHz)\n",j+1,rounds,total_pacotes,media_freq/total_pacotes,sensor_frequency);
        ESP_LOGW(TAG,"Number of corrupted packets: %llu\n",corrompido);
        vTaskDelay(1000/portTICK_PERIOD_MS);
    }
    memcpy(last_iteration,data,sizeof(data));
    streaming = false;
    vTaskDelete(NULL);
}


static int receive_stream_pckt(int argc, char **argv){
    if ((sockfd > 0) && (!streaming) && (!generic_buffer)){
        int nerrors = arg_parse(argc, argv, (void **) &packet_stream_args);

        if (nerrors != 0) {
            arg_print_errors(stderr, packet_stream_args.end, argv[0]);
            return ESP_OK;
        }
        if ((packet_stream_args.sensor_frequency->ival[0] < 1) || (packet_stream_args.sensor_frequency->ival[0] > 4000)){
            ESP_LOGE(TAG,"Invalid frequency!!");
            return ESP_OK;
        }
        if ((packet_stream_args.number_of_pckts->ival[0] <= 0) || (packet_stream_args.number_of_pckts->ival[0] > 60000)){
            ESP_LOGE(TAG,"Invalid number of packets!!");
            return ESP_OK;
        }
        if ((packet_stream_args.rounds->ival[0] <= 0) || (packet_stream_args.rounds->ival[0] > 10)){
            ESP_LOGE(TAG,"\"%d\" is an Invalid number of rounds!!",packet_stream_args.rounds->ival[0]);
            return ESP_OK;
        }
        ESP_LOGI(TAG,"Starting receiving stream of packets\n");
        xTaskCreatePinnedToCore(task_stream_pckts, "socket_streaming_recv", 10240,NULL, 6, NULL,1);
        streaming = true;
        return ESP_OK;
    }
    if ((streaming) || (generic_buffer)){
        ESP_LOGW(TAG,"Stream still ongoing!!");
    }else{
        ESP_LOGE(TAG,"Socket is not open!!");
    }
    return ESP_OK;
}

static void register_receive_stream_pckt(void){
    packet_stream_args.sensor_frequency = arg_int1("f","frequency","<int>","sensor's transmission frequency");
    packet_stream_args.number_of_pckts = arg_int1("c", "count", "<int>", "number of packets to receive");
    packet_stream_args.rounds = arg_int1("r","rounds","<int>","number of sequential transmissions of \'c\' packets");
    packet_stream_args.end = arg_end(0);
    const esp_console_cmd_t cmd = {
        .command = "recv_sensor",
        .help = "receive fixed amount of packets from sensor",
        .hint = NULL,
        .func = &receive_stream_pckt,
        .argtable = &packet_stream_args
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd));
}

static void task_generic_receiver(void *pvParameters){
    uint8_t packet[1024] = {0,};
    int err;
    while(true){
        err = recv(sockfd,packet,sizeof(packet),0);
        if (err < 0){
            ESP_LOGE(TAG,"error no socket: %s",strerror(errno));
            generic_buffer = false;
            break;
        }
        if(!generic_buffer){
            ESP_LOGW(TAG,"generic_receiver is being closed");
            vTaskDelete(NULL);
        }
        ESP_LOGW(TAG,"%s\n",packet);
    }
    ESP_LOGE(TAG,"Receiver is being closed!!");
    vTaskDelete(NULL);
}
static int generic_receiver(int argc, char **argv){
    if ((sockfd > 0) && (!streaming) && (!generic_buffer)){
        xTaskCreatePinnedToCore(task_generic_receiver, "generic_receiver_task", 8096,NULL, 6, NULL,1);
        generic_buffer = true;
    }else if((streaming) || (generic_buffer)){
        ESP_LOGW(TAG,"Stream still ongoing!!");
        return ESP_OK;
    }else{
        ESP_LOGE(TAG,"Socket is not open!!");
    }
    return ESP_OK;


}

static void register_generic_receiver(void){
    const esp_console_cmd_t cmd = {
        .command = "generic_recv_on",
        .help = "turn on the receving socket",
        .hint = NULL,
        .func = &generic_receiver,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd));
}

static int stations_list(int argc, char **argv){
	wifi_sta_list_t list;
	if (!soft_ap_on){
		ESP_LOGE(TAG,"Wireless Interface off");
		return ESP_OK;
	}
	ESP_ERROR_CHECK(esp_wifi_ap_get_sta_list(&list));
	int list_iterator = list.num - 1;
	tcpip_adapter_sta_list_t tcpip_sta_list;

	tcpip_adapter_get_sta_list(&list, &tcpip_sta_list);
	// tcpip_adapter_sta_list.sta[i].ip

	for(int i = 0; i <= list_iterator; i++){
		ESP_LOGI(TAG,"\nMAC: %2x:%2x:%2x:%2x:%2x:%2x\t\tIP:"IPSTR"\t\tRSSI:%d\n",list.sta[i].mac[0],list.sta[i].mac[1],list.sta[i].mac[2],
			list.sta[i].mac[3],list.sta[i].mac[4],list.sta[i].mac[5],IP2STR(&tcpip_sta_list.sta[i].ip),list.sta[i].rssi);
	}
	return ESP_OK;
}

static void register_stations_list(void){
	const esp_console_cmd_t cmd = {
		.command = "list_stations",
		.help = "list the stations associated",
		.hint = NULL,
		.func = &stations_list,
	};
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd));
}

static int print_packets(int argc, char **argv){
    if ((streaming) || (generic_buffer)){
        ESP_LOGE(TAG,"Can't print while the trasmission is on");
        return ESP_OK;
    }

/*  uint16_t battery;
    uint8_t IDfinal;*/
    for(int i = 0; i < 150; i++){
        ESP_LOGI(TAG,"{\n\n\tID0: %d, time: %lld, accelX: %d, accelY: %d, accelZ: %d, gyroX: %d, gyroY: %d, gyroZ: %d, battery: %u, IDfinal: %d\n",
            last_iteration[i].ID0,last_iteration[i].time,last_iteration[i].accelX,last_iteration[i].accelY,last_iteration[i].accelZ,
            last_iteration[i].gyroX,last_iteration[i].gyroY,last_iteration[i].gyroZ,last_iteration[i].battery,last_iteration[i].IDfinal);
    }
    return ESP_OK;
}

static void register_print_packets(void){
    const esp_console_cmd_t cmd = {
        .command = "print_packets",
        .help = "print all 150 last received packets",
        .hint = NULL,
        .func = &print_packets,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd));
}