# Histórico e Documentação de Desenvolvimento do FarolDNS

Este documento serve como o "módulo de memória" do projeto **FarolDNS**. Ele deve ser atualizado ao final de cada sessão de desenvolvimento para que desenvolvedores (humanos ou IAs) que assumirem o projeto no futuro possam entender o contexto, decisões de arquitetura e o estado atual imediatamente.

---

## 1. Visão Geral do Projeto
O **FarolDNS** é um servidor DNS embarcado de baixo consumo de energia projetado para rodar em rede local. O objetivo é fornecer resolução de DNS extremamente rápida, estável (cabeada) e com capacidade de failover (redundância) automático para Wi-Fi caso o cabo de rede seja desconectado.

*   **Microcontrolador**: ESP32-S3-WROOM-1 N16R8 (16MB Flash / 8MB Octal PSRAM)
*   **Placa de Desenvolvimento**: Waveshare ESP32-S3-ETH (Ethernet W5500 e leitor de MicroSD integrados de fábrica)
*   **Framework**: ESP-IDF v5.3 (C nativo)

---

## 2. Linha do Tempo e Decisões de Arquitetura

### A. Junho/2026 - Fase 1: Protótipo em Arduino
*   Iniciou-se com um código simples usando a biblioteca `WiFiUDP` na IDE Arduino ([FarolDNS.ino_V0.1](file:///home/isaac/projetos/FarolDNS/FarolDNS.ino_V0.1)).
*   A primeira versão travava devido a loops infinitos de bloqueio e leituras duplicadas do buffer UDP.
*   Corrigiu-se a POC ([FarolDNS.ino](file:///home/isaac/projetos/FarolDNS/FarolDNS.ino)) implementando uma lógica não-bloqueante de escuta e encaminhamento (Forwarder) para o DNS público `1.1.1.1`.

### B. Junho/2026 - Fase 2: Migração para o ESP-IDF
*   O projeto foi migrado para o **ESP-IDF v5.3** para possibilitar o uso robusto do chip Ethernet W5500, multithreading nativo (FreeRTOS) e gerenciamento otimizado de memória (usando os 8MB de PSRAM externa para futuras tabelas de cache recursivo).
*   A primeira compilação nativa no container docker foi homologada com sucesso.

### C. 11 de Junho/2026 - Fase 3: Estrutura Modular e Nova Placa (Waveshare)
*   Com a chegada da placa física **Waveshare ESP32-S3-ETH**, o mapeamento de pinos do W5500 e do leitor de SD mudou em relação ao protótipo anterior.
*   **Decisão de Engenharia**: O código do projeto foi completamente reestruturado em **Componentes Modulares** independentes dentro do diretório `components/`. Isso removeu a lógica de rede e sockets de dentro do `main.c`, deixando-o puramente como um orquestrador leve.
*   **Decisão de Interface Única (Redundância)**: Alinhou-se a regra de negócio do arquivo de dicas ([dicas.txt](file:///home/isaac/projetos/FarolDNS/docs/dicas.txt)):
    *   **Ethernet Conectada (Prioridade)**: O Wi-Fi permanece 100% desligado para economizar energia e evitar conflitos de IP (ARP) na rede local.
    *   **Ethernet Desconectada (Fallback)**: O rádio Wi-Fi é ligado no modo Station (para se conectar ao roteador) ou no modo Access Point (`FarolDNS_Setup` sem senha) caso não haja credenciais armazenadas na Flash, permitindo que o usuário configure o dispositivo via Web Dashboard.

---

## 3. Arquitetura Modular Atual do Projeto

Os seguintes componentes foram criados e estão compilando no ESP-IDF:

1.  **[storage_manager](file:///home/isaac/projetos/FarolDNS/components/storage_manager)**:
    *   Inicializa a partição NVS (Non-Volatile Storage) da Flash.
    *   Salva e carrega a struct de configurações `faroldns_config_t` (SSID, senha do Wi-Fi, modo DHCP ou IP Estático para ambas as interfaces e o IP do DNS upstream).
2.  **[ethernet_w5500](file:///home/isaac/projetos/FarolDNS/components/ethernet_w5500)**:
    *   Inicializa o barramento SPI2_HOST na pinagem específica da Waveshare.
    *   Instala o driver nativo do W5500 em modo **Polling** (sem interrupções de hardware físicas ativas por software por enquanto).
    *   Atualiza o status de link e aquisição de IP.
3.  **[wifi_manager](file:///home/isaac/projetos/FarolDNS/components/wifi_manager)**:
    *   Gerencia a inicialização do Wi-Fi.
    *   Suporta modo Station e modo Access Point (`FarolDNS_Setup` aberto).
4.  **[network_manager](file:///home/isaac/projetos/FarolDNS/components/network_manager)**:
    *   A máquina de estado central do projeto.
    *   Escuta eventos de rede (`ETH_EVENT`, `IP_EVENT`, `WIFI_EVENT`) e notifica uma tarefa FreeRTOS monitora.
    *   Aplica a regra de desligar Wi-Fi quando a Ethernet estiver ativa e ligar Wi-Fi quando a Ethernet cair.
5.  **[dns_server](file:///home/isaac/projetos/FarolDNS/components/dns_server)**:
    *   Escuta queries DNS UDP na porta padrão 53 em qualquer interface ativa (`0.0.0.0`).
    *   Encaminha as consultas para o DNS público salvo nas configurações (ex: `1.1.1.1`), retornando as respostas aos clientes com timeout de segurança.

*O ponto de entrada do firmware está em [main.c](file:///home/isaac/projetos/FarolDNS/main/main.c).*

---

## 4. Pinagem Física da Placa

O mapeamento de pinagem física da placa Waveshare está documentado e detalhado em:
*   [pinagem_esp32_s3_eth.md](file:///home/isaac/projetos/FarolDNS/docs/hardware/pinagem_esp32_s3_eth.md) (Diagrama e Notas Técnicas)
*   [ESP32-S3-ETH-details-11-1.jpg](file:///home/isaac/projetos/FarolDNS/docs/hardware/ESP32-S3-ETH-details-11-1.jpg) (Foto esquemática)

---

### D. 19 de Junho/2026 - Fase 4: Correções de Hardware e Testes
*   **Objetivo**: Gravar firmware na placa física Waveshare ESP32-S3-ETH e validar funcionamento.
*   **Problemas corrigidos**:
    1.  **PSRAM Boot Loop**: A placa reiniciava infinitamente. Corrigido adicionando `CONFIG_SPIRAM_MODE_OCT=y` e `CONFIG_SPIRAM_SPEED_80M=y` no `sdkconfig.defaults`.
    2.  **MAC Address zerado (`00:00:00:00:00:00`)** no W5500: Corrigido lendo o MAC do eFuse com `esp_read_mac()` e aplicando via `esp_eth_ioctl(ETH_CMD_S_MAC_ADDR)`.
    3.  **SPI frame format incorreto**: `command_bits` alterado de `0` para `16` no `ethernet_w5500.c` (formato exigido pelo W5500).
    4.  **GPIO ISR service não instalado**: Adicionado `gpio_install_isr_service(0)` antes de iniciar o W5500.
    5.  **Detecção de link lenta**: `s_connected` passou a ser setado em `ETHERNET_EVENT_CONNECTED` (antes era apenas no `GOT_IP`).
*   **Compilação**: ✅ Bem-sucedida. Firmware gravado na placa.
*   **Testes realizados** (com firmware anterior às correções):
    *   Cenário 1 (AP Setup): ✅ Funcionou
    *   Cenário 2 (Wi-Fi Station): ✅ Funcionou
    *   Cenário 3 (DNS via Wi-Fi): ✅ Funcionou
    *   Cenário 4 (Ethernet failover): ❌ Falhou (MAC zerado impedia DHCP)
    *   Cenário 5 (Fallback Ethernet→Wi-Fi): ❌ Falhou (MAC zerado)
*   **Testes realizados (22:37 às 22:51)** com o firmware corrigido (`b2a78ae-dirty`, compilado 22:17):
    *   **Boot/Pós-RAM**: ✅ PSRAM 8MB detectado nos 3 boots
    *   **MAC eFuse**: ✅ Lido corretamente (`28:84:85:54:b7:6f`)
    *   **Wi-Fi Station**: ✅ Conectou na rede "santper", IP `192.168.3.168`
    *   **Ethernet DHCP**: ✅ IP `192.168.3.169`
    *   **Failover Ethernet→Wi-Fi** (cabo removido): ✅ Transição em ~1s
    *   **Failover Wi-Fi→Ethernet** (cabo conectado): ✅ Transição imediata
    *   **DNS Forwarding**: ✅ Consultas de `192.168.3.11` → `1.1.1.1` → resposta
    *   **Web UI**: ✅ Acessível (favicon.ico requests recebidos)
    *   **mDNS**: ❌ Não testado / não funcional (sem consultas nos logs)
    *   **Boot race condition**: ⚠️ Boot 1 e 2 dispararam Wi-Fi antes do Ethernet Link Up (4s insuficiente). Boot 3 funcionou (link em 3s).
*   **Issues conhecidas**:
    *   IP diferente entre Wi-Fi (`.168`) e Ethernet (`.169`) — usuário optou por IP estático.
    *   mDNS (`faroldns.local`) só funciona no Wi-Fi — precisa associar `netif` correta ao comutar interface.
    *   **Boot race condition confirmada**: `vTaskDelay(4000)` não sincroniza com `ETHERNET_EVENT_CONNECTED`. Link Ethernet varia entre 3s e 5s+.
*   **Nova arquitetura solicitada**: Usuário quer controle granular de interfaces (Wi-Fi ON/OFF, ETH ON/OFF, suporte a ambas ativas com IPs diferentes em sub-redes distintas).

---

## 5. Próximos Passos (Planejamento)

1.  **`web_config` (Próximo Módulo)**: Criar o servidor HTTP embarcado para o painel de configurações.
2.  **`mdns_manager`**: Sobe o serviço mDNS para responder ao nome local `faroldns.local`.
3.  **Cache DNS em PSRAM**: Implementar armazenamento em tabela hash na PSRAM para acelerar as consultas recorrentes.
4.  **Resolução DNS Recursiva Completa**: Remover a dependência de resolvedores públicos e implementar a árvore de consultas raiz (Root Hints).
