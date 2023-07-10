/* BSD Socket API Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <sys/param.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "protocol_examples_common.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include "driver/uart.h"

#include "driver/gpio.h"
#ifdef CONFIG_EXAMPLE_IPV4
#define HOST_IP_ADDR CONFIG_EXAMPLE_IPV4_ADDR
#else
#define HOST_IP_ADDR CONFIG_EXAMPLE_IPV6_ADDR
#endif

#define PORT CONFIG_EXAMPLE_PORT

static const char *TAG = "example";
static const char *payload = "Message from ESP32 ";
#define BUF_SIZE (128)
uint8_t name[BUF_SIZE];
uint8_t type[BUF_SIZE];
uint8_t position[BUF_SIZE];
uint8_t ulen[9];
uint8_t temp[17];
uint8_t humi[17];
uint8_t light[1];
uint8_t smoke[1];
char task_buf[512];
static void gpio2_init(void)
{
     gpio_config_t io_conf;
    //disable interrupt
    io_conf.intr_type = GPIO_INTR_DISABLE;
    //set as output mode
    io_conf.mode = GPIO_MODE_OUTPUT;
    //bit mask of the pins that you want to set,e.g.GPIO15/16
    io_conf.pin_bit_mask = 1ULL<<2;
    //disable pull-down mode
    io_conf.pull_down_en = 0;
    //disable pull-up mode
    io_conf.pull_up_en = 0;
    //configure GPIO with the given settings
    gpio_config(&io_conf);
    gpio_set_level(2,1);
}
static void uart_init(void)
{
    // Configure parameters of an UART driver,
    // communication pins and install the driver
    uart_config_t uart_config = {
        // .baud_rate = 74880,
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_NUM_0, &uart_config);
    uart_driver_install(UART_NUM_0, BUF_SIZE * 2, 0, 0, NULL, 0);
}
static void tcp_client_task(void *pvParameters)
{
    char addr_str[128];
    int addr_family;
    int ip_protocol;
    char box[200];
    while (1) {

#ifdef CONFIG_EXAMPLE_IPV4
        struct sockaddr_in destAddr;
        destAddr.sin_addr.s_addr = inet_addr(HOST_IP_ADDR);
        destAddr.sin_family = AF_INET;
        destAddr.sin_port = htons(PORT);
        addr_family = AF_INET;
        ip_protocol = IPPROTO_IP;
        inet_ntoa_r(destAddr.sin_addr, addr_str, sizeof(addr_str) - 1);
#else // IPV6
        struct sockaddr_in6 destAddr;
        inet6_aton(HOST_IP_ADDR, &destAddr.sin6_addr);
        destAddr.sin6_family = AF_INET6;
        destAddr.sin6_port = htons(PORT);
        destAddr.sin6_scope_id = tcpip_adapter_get_netif_index(TCPIP_ADAPTER_IF_STA);
        addr_family = AF_INET6;
        ip_protocol = IPPROTO_IPV6;
        inet6_ntoa_r(destAddr.sin6_addr, addr_str, sizeof(addr_str) - 1);
#endif

        int sock =  socket(addr_family, SOCK_STREAM, ip_protocol);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            break;
        }
        ESP_LOGI(TAG, "Socket created");

        int err = connect(sock, (struct sockaddr *)&destAddr, sizeof(destAddr));
        if (err != 0) {
            ESP_LOGE(TAG, "Socket unable to connect: errno %d", errno);
            close(sock);
            continue;
        }
        int len,rlen;
        len = strlen((const char*)name);
        sprintf(box,"len=%d\r\n",len);
        // uart_write_bytes(UART_NUM_0,&len,4); 
        rlen = htonl(len);
        err = send(sock,&rlen,4,0);
        if (err < 0) {
            ESP_LOGE(TAG, "(%d)Error occured during sending: errno %d",__LINE__,errno);
            close(sock);
            continue;
        }
        err = send(sock,name,len,0);
        sprintf(box,"name=%s\r\n",name);
        // uart_write_bytes(UART_NUM_0,box,100); 
        if (err < 0) {
            ESP_LOGE(TAG, "(%d)Error occured during sending: errno %d",__LINE__,errno);
            close(sock);
            continue;
        }
        len = strlen((const char*)type);
        rlen = htonl(len);
        err = send(sock,&rlen,4,0);
        if (err < 0) {
            ESP_LOGE(TAG, "(%d)Error occured during sending: errno %d",__LINE__,errno);
            close(sock);
            continue;
        }
        err = send(sock,type,len,0);
        if (err < 0) {
            ESP_LOGE(TAG, "(%d)Error occured during sending: errno %d",__LINE__,errno);
            close(sock);
            continue;
        }
        len = strlen((const char*)position);
        rlen = htonl(len);
        err = send(sock,&rlen,4,0);
        if (err < 0) {
            ESP_LOGE(TAG, "Error occured during sending: errno %d", errno);
            close(sock);
            continue;
        }
        err = send(sock,position,len,0);
        if (err < 0) {
            ESP_LOGE(TAG, "(%d)Error occured during sending: errno %d",__LINE__,errno);
            close(sock);
            continue;
        }
        ESP_LOGI(TAG, "Successfully connected");
        while (1) {
            gpio_set_level(2,0);
            uart_read_bytes(UART_NUM_0,temp, 17, 2000 / portTICK_RATE_MS);
            uart_read_bytes(UART_NUM_0,humi, 17, 2000 / portTICK_RATE_MS);
            uart_read_bytes(UART_NUM_0,light, 1, 2000 / portTICK_RATE_MS);
            uart_read_bytes(UART_NUM_0,smoke, 1, 2000 / portTICK_RATE_MS);
            gpio_set_level(2,1);
            len = 36;
            rlen = htonl(len);
            err = send(sock,&rlen,4,0);
            if (err < 0) {
                ESP_LOGE(TAG, "Error occured during sending: errno %d", errno);
                close(sock);
                break;
            }
            err = send(sock,temp,17, 0);
            if (err < 0) {
                ESP_LOGE(TAG, "(%d)Error occured during sending: errno %d",__LINE__,errno);
                close(sock);
                break;
            }
            err = send(sock,humi,17, 0);
            if (err < 0) {
                ESP_LOGE(TAG, "(%d)Error occured during sending: errno %d",__LINE__,errno);
                close(sock);
                break;
            }
            err = send(sock,light,1, 0);
            if (err < 0) {
                ESP_LOGE(TAG, "(%d)Error occured during sending: errno %d",__LINE__,errno);
                close(sock);
                break;
            }
            err = send(sock,smoke,1, 0);
            if (err < 0) {
                ESP_LOGE(TAG, "(%d)Error occured during sending: errno %d",__LINE__,errno);
                close(sock);
                break;
            }
            // len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
            // Error occured during receiving
            // if (len < 0) {
            //     ESP_LOGE(TAG, "recv failed: errno %d", errno);
            //     break;
            // }
            // // Data received
            // else {
            //     rx_buffer[len] = 0; // Null-terminate whatever we received and treat like a string
            //     ESP_LOGI(TAG, "Received %d bytes from %s:", len, addr_str);
            //     ESP_LOGI(TAG, "%s", rx_buffer);
            // }
            vTaskList(task_buf);
            printf("final task status:\r\n%s\r\n",task_buf);
            
            vTaskDelay(2000 / portTICK_PERIOD_MS);
        }

        if (sock != -1) {
            ESP_LOGE(TAG, "Shutting down socket and restarting...");
            shutdown(sock, 0);
            close(sock);
        }
    }
    vTaskDelete(NULL);
}

void app_main()
{
    uart_init();
    gpio2_init();
    char box[100];
    int len_tmp = uart_read_bytes(UART_NUM_0, ulen, 8, 20000 / portTICK_RATE_MS);
    ulen[len_tmp] = 0;
    int len_in = atoi((const char*)ulen);
        
    sprintf(box,"len_in=%d\r\n",len_in);
    // uart_write_bytes(UART_NUM_0,box,100);
    uart_read_bytes(UART_NUM_0, name, len_in, 20000 / portTICK_RATE_MS);
    len_tmp = uart_read_bytes(UART_NUM_0, ulen, 8, 20000 / portTICK_RATE_MS);
    ulen[len_tmp] = 0;
    len_in = atoi((const char*)ulen);   
        
    sprintf(box,"len_in=%d\r\n",len_in);
    // uart_write_bytes(UART_NUM_0,box,100); 
    uart_read_bytes(UART_NUM_0, type, len_in, 20000 / portTICK_RATE_MS);
    len_tmp = uart_read_bytes(UART_NUM_0, ulen, 8, 20000 / portTICK_RATE_MS);
    ulen[len_tmp] = 0;
    len_in = atoi((const char*)ulen);   
        
    sprintf(box,"len_in=%d\r\n",len_in);
    // uart_write_bytes(UART_NUM_0,box,100); 
    uart_read_bytes(UART_NUM_0, position, len_in, 20000 / portTICK_RATE_MS);
    vTaskList(task_buf);
    printf("initial task status:\r\n%s\r\n",task_buf);
    ESP_ERROR_CHECK(nvs_flash_init());
    vTaskList(task_buf);
    printf("after nvs_flash_init task status:\r\n%s\r\n",task_buf);
    ESP_ERROR_CHECK(esp_netif_init());
    vTaskList(task_buf);
    printf("after esp_netif_init task status:\r\n%s\r\n",task_buf);
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    printf("after esp_event_loop_create_default task status:\r\n%s\r\n",task_buf);
    ESP_ERROR_CHECK(example_connect());
    printf("after example connect task status:\r\n%s\r\n",task_buf);
    os_delay_us(2*1000*1000);
    printf("2 secondes after example connect task status:\r\n%s\r\n",task_buf);
    xTaskCreate(tcp_client_task, "tcp_client", 4096, NULL, 5, NULL);
}
