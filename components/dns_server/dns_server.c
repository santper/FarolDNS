#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "storage_manager.h"
#include "network_manager.h"
#include "dns_server.h"

static const char *TAG = "DNSServer";
#define LOCAL_DNS_PORT 53
#define MAX_DNS_PACKET_SIZE 512

static TaskHandle_t s_dns_task_handle = NULL;
static bool s_running = false;

static void dns_server_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Aguardando inicializacao da rede para iniciar DNS...");
    while (network_manager_get_state() == NET_STATE_INIT) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    ESP_LOGI(TAG, "Carregando configuracao do DNS Upstream...");
    faroldns_config_t config;
    if (storage_load_config(&config) != ESP_OK) {
        storage_get_default_config(&config);
    }
    
    char upstream_ip[16];
    strncpy(upstream_ip, config.upstream_dns, sizeof(upstream_ip) - 1);
    upstream_ip[sizeof(upstream_ip) - 1] = '\0';
    ESP_LOGI(TAG, "DNS Upstream configurado para: %s", upstream_ip);

    // Cria o socket UDP do servidor
    int server_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (server_sock < 0) {
        ESP_LOGE(TAG, "Falha ao criar socket do servidor DNS: errno %d", errno);
        s_running = false;
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(LOCAL_DNS_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Falha ao fazer bind na porta %d: errno %d", LOCAL_DNS_PORT, errno);
        close(server_sock);
        s_running = false;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Servidor DNS escutando na porta %d", LOCAL_DNS_PORT);

    uint8_t rx_buffer[MAX_DNS_PACKET_SIZE];
    uint8_t tx_buffer[MAX_DNS_PACKET_SIZE];

    while (s_running) {
        struct sockaddr_in client_addr;
        socklen_t socklen = sizeof(client_addr);
        
        // Recebe query DNS de um cliente
        int len = recvfrom(server_sock, rx_buffer, sizeof(rx_buffer), 0,
                           (struct sockaddr *)&client_addr, &socklen);
        if (len < 0) {
            if (s_running) {
                ESP_LOGE(TAG, "recvfrom falhou: errno %d", errno);
            }
            break;
        }

        char client_ip_str[32];
        inet_ntoa_r(client_addr.sin_addr, client_ip_str, sizeof(client_ip_str));
        ESP_LOGI(TAG, "Consulta DNS recebida de %s:%d (%d bytes)", 
                 client_ip_str, ntohs(client_addr.sin_port), len);

        // Se estamos no modo Wi-Fi AP de fallback, nao ha internet para encaminhar
        if (network_manager_get_state() == NET_STATE_WIFI_AP) {
            ESP_LOGW(TAG, "Modo AP de Fallback ativo. Nao ha conexao com internet para resolver.");
            continue;
        }

        // Cria socket UDP cliente dinâmico para falar com o DNS Upstream
        int client_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (client_sock < 0) {
            ESP_LOGE(TAG, "Falha ao criar socket para upstream: errno %d", errno);
            continue;
        }

        // Configura timeout de 2 segundos para o recebimento da resposta
        struct timeval tv;
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        struct sockaddr_in upstream_addr;
        upstream_addr.sin_family = AF_INET;
        upstream_addr.sin_port = htons(53);
        upstream_addr.sin_addr.s_addr = inet_addr(upstream_ip);

        // Encaminha a query DNS para o servidor público
        ESP_LOGI(TAG, "Encaminhando para o DNS publico %s...", upstream_ip);
        int sent = sendto(client_sock, rx_buffer, len, 0,
                          (struct sockaddr *)&upstream_addr, sizeof(upstream_addr));
        if (sent < 0) {
            ESP_LOGE(TAG, "Falha ao enviar para o DNS publico: errno %d", errno);
            close(client_sock);
            continue;
        }

        // Aguarda e recebe a resposta do DNS publico
        struct sockaddr_in reply_addr;
        socklen_t reply_socklen = sizeof(reply_addr);
        int reply_len = recvfrom(client_sock, tx_buffer, sizeof(tx_buffer), 0,
                                 (struct sockaddr *)&reply_addr, &reply_socklen);

        if (reply_len < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                ESP_LOGW(TAG, "Timeout: O DNS publico nao respondeu a tempo.");
            } else {
                ESP_LOGE(TAG, "Erro ao receber dados do DNS publico: errno %d", errno);
            }
        } else {
            ESP_LOGI(TAG, "Resposta recebida do DNS publico (%d bytes). Enviando ao cliente...", reply_len);
            
            // Envia a resposta de volta ao cliente original
            int sent_back = sendto(server_sock, tx_buffer, reply_len, 0,
                                  (struct sockaddr *)&client_addr, socklen);
            if (sent_back < 0) {
                ESP_LOGE(TAG, "Falha ao responder ao cliente: errno %d", errno);
            }
        }

        close(client_sock);
    }

    close(server_sock);
    s_running = false;
    vTaskDelete(NULL);
}

esp_err_t dns_server_start(void)
{
    if (s_running) {
        return ESP_OK;
    }

    s_running = true;
    BaseType_t ret = xTaskCreate(dns_server_task, "dns_server", 4096, NULL, 5, &s_dns_task_handle);
    if (ret != pdPASS) {
        s_running = false;
        ESP_LOGE(TAG, "Falha ao criar tarefa dns_server");
        return ESP_FAIL;
    }

    return ESP_OK;
}

void dns_server_stop(void)
{
    s_running = false;
}
