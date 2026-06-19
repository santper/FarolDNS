#ifndef ETHERNET_W5500_H
#define ETHERNET_W5500_H

#include "esp_err.h"
#include "esp_netif.h"

// --- CONFIGURAÇÃO DE PINOS DO W5500 (Waveshare ESP32-S3-ETH) ---
#define ETH_PIN_MOSI 11
#define ETH_PIN_MISO 12
#define ETH_PIN_SCLK 13
#define ETH_PIN_CS   14
#define ETH_PIN_INT  10
#define ETH_PIN_RST  9

#define ETH_SPI_HOST SPI2_HOST

/**
 * @brief Inicializa o driver Ethernet para o W5500 e a interface de rede.
 * @return esp_err_t ESP_OK em caso de sucesso, código de erro caso contrário.
 */
esp_err_t eth_w5500_init(void);

/**
 * @brief Verifica se a conexão Ethernet está ativa com um IP válido.
 * @return true se conectado e com IP, false caso contrário.
 */
bool eth_w5500_is_connected(void);

/**
 * @brief Obtém o ponteiro da netif para a interface Ethernet.
 * @return esp_netif_t* ponteiro para a interface.
 */
esp_netif_t* eth_w5500_get_netif(void);

#endif // ETHERNET_W5500_H
