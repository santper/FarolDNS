# Pinagem da Placa Waveshare ESP32-S3-ETH

Este documento registra o mapeamento físico de pinos (GPIOs) da placa **Waveshare ESP32-S3-ETH**, integrando o microcontrolador ESP32-S3, o controlador Ethernet W5500 (via barramento SPI) e o leitor de cartão MicroSD (via barramento SPI).

---

## 1. Conexões do Controlador Ethernet (W5500)

O chip W5500 está conectado ao barramento SPI do ESP32-S3 utilizando os seguintes pinos:

| Sinal W5500 | GPIO do ESP32-S3 | Descrição |
| :--- | :---: | :--- |
| **ETH_MOSI** | **GPIO 11** | Master Out Slave In (SPI) |
| **ETH_MISO** | **GPIO 12** | Master In Slave Out (SPI) |
| **ETH_CLK**  | **GPIO 13** | Serial Clock (SCLK - SPI) |
| **ETH_CS**   | **GPIO 14** | Chip Select (Ativo em nível lógico baixo) |
| **ETH_INT**  | **GPIO 10** | Pino de Interrupção (Opcional, usado para modo interrupção) |
| **ETH_RST**  | **GPIO 9**  | Pino de Reset físico do chip |

---

## 2. Conexões do Leitor de Cartão MicroSD

O leitor de cartão SD integrado utiliza os seguintes pinos para a comunicação em modo SPI:

| Sinal SD Card | GPIO do ESP32-S3 | Descrição |
| :--- | :---: | :--- |
| **SD_MOSI** | **GPIO 6** | Master Out Slave In (SPI) |
| **SD_MISO** | **GPIO 5** | Master In Slave Out (SPI) |
| **SD_CLK**  | **GPIO 7** | Serial Clock (SCLK - SPI) |
| **SD_CS**   | **GPIO 4** | Chip Select do Cartão SD (Ativo em nível lógico baixo) |

---

## 3. Notas Importantes sobre o Hardware

*   **Barramentos SPI**:
    *   Tanto o W5500 quanto o leitor de SD usam pinos de barramento SPI diferentes. Isso significa que eles podem ser operados de forma independente em diferentes hosts SPI ou periféricos no ESP32-S3, evitando conflitos de compartilhamento de barramento na mesma frequência se necessário.
*   **Imagem de Referência**:
    *   A imagem com o diagrama físico da placa e detalhes de pinagem está salva em [ESP32-S3-ETH-details-11-1.jpg](file:///home/isaac/projetos/FarolDNS/docs/hardware/ESP32-S3-ETH-details-11-1.jpg).
