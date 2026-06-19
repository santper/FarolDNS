# Plano de Testes e Lista de Funcionalidades - FarolDNS 📡

Este documento descreve as funcionalidades implementadas no firmware **FarolDNS** rodando no **ESP32-S3** com o driver Ethernet **W5500** (Waveshare) e detalha o roteiro de testes em bancada.

---

## 📋 Lista de Funcionalidades por Módulo

### 1. Storage Manager (`storage_manager`)
- **NVS Flash**: Inicialização do subsistema de armazenamento não volátil.
- **Configurações Persistentes**: Salva e carrega credenciais Wi-Fi (SSID/Senha), modo de obtenção de IP (DHCP ou Estático) para ambas as interfaces (Wi-Fi e Ethernet), endereços IP estáticos configurados e o IP do servidor DNS Upstream.
- **Valores Padrão**: Define fallback padrão na primeira inicialização (ex: DNS Upstream `1.1.1.1` e Wi-Fi em modo AP).

### 2. Ethernet W5500 (`ethernet_w5500`)
- **Driver SPI2**: Configuração do barramento SPI2 na pinagem nativa da placa Waveshare.
- **Modo Polling**: Inicialização e monitoramento da interface cabeada W5500 de forma periódica por software.
- **Interface de Rede (`esp_netif`)**: Atribuição automática de IP via DHCP ou IP fixo configurado.

### 3. Wi-Fi Manager (`wifi_manager`)
- **Modo Station (STA)**: Conecta-se a um ponto de acesso existente utilizando as credenciais salvas.
- **Modo Access Point (AP)**: Levanta rede Wi-Fi aberta chamada `FarolDNS_Setup` para configuração inicial.

### 4. Network Manager (`network_manager`)
- **Orquestrador de Redundância (Failover)**:
  - **Prioridade Ethernet**: Se o cabo Ethernet for conectado e negociar link, o Wi-Fi é desligado para poupar bateria e evitar conflito de rede/ARP local.
  - **Fallback Wi-Fi**: Se o link Ethernet cair, o Wi-Fi é ativado em modo STA (para conectar na rede configurada) ou modo AP (se não houver rede salva).

### 5. DNS Server (`dns_server`)
- **UDP Socket (Porta 53)**: Escuta requisições em todas as interfaces ativas.
- **Forwarder**: Repassa queries de clientes locais para o DNS Upstream configurado com um timeout dinâmico de 2 segundos.
- **Retorno**: Devolve a resposta dos servidores globais para o cliente de rede local de forma transparente.

### 6. mDNS Manager (`mdns_manager`)
- **Hostname Local**: Resolve o nome `faroldns.local` (ou o hostname customizado) na rede local.
- **Publicação de Serviços**: Registra o serviço HTTP na porta 80.

### 7. Web Config (`web_config`)
- **Servidor HTTP**: Escuta na porta 80 de qualquer interface ativa.
- **Interface Dashboard**: Serve uma página web moderna com design responsivo (Dark Mode e Glassmorphism).
- **REST API**:
  - `GET /api/config` -> Retorna as configurações da NVS em formato JSON.
  - `POST /save` -> Recebe dados em JSON, salva na NVS e agenda um reinício físico do chip após 2 segundos.

---

## 🧪 Plano de Testes de Bancada

### Pré-requisitos
- Um cabo serial (USB-C) conectado à placa na porta USB principal (reconhecida como `/dev/ttyACM0`).
- Um cabo de rede RJ45 conectado a um roteador/switch local com acesso à internet (para testar Ethernet).
- Um roteador Wi-Fi local para conectar o ESP32 em modo Station.
- Um computador na mesma rede (Wi-Fi ou cabeada) com `dig` ou `nslookup` instalado.

---

### Cenário 1: Primeira Inicialização (Sem Configuração e Sem Ethernet)
**Objetivo**: Validar a criação automática da rede de setup (`FarolDNS_Setup`).

1. Garanta que o cabo Ethernet **não** esteja conectado ao ESP32.
2. Limpe as configurações anteriores da NVS gravando uma imagem limpa ou resetando via comando serial (se implementado) ou simplesmente rodando o firmware pela primeira vez.
3. Ligue o ESP32 e monitore a saída serial:
   ```bash
   . /home/isaac/esp/esp-idf/export.sh && idf.py -p /dev/ttyACM0 monitor
   ```
4. **Resultado Esperado no Console**:
   - `Ethernet inativa ou cabo desconectado. Iniciando fallback via Wi-Fi...`
   - O Wi-Fi inicia no modo Access Point.
5. **Teste de Rede**:
   - No celular ou notebook, procure redes Wi-Fi e encontre a rede aberta `FarolDNS_Setup`.
   - Conecte-se nela. Você deve receber um IP na faixa `192.168.4.x`.
   - Acesse [http://192.168.4.1](http://192.168.4.1) ou [http://faroldns.local](http://faroldns.local) no navegador. A interface web Glassmorphism deve carregar.

---

### Cenário 2: Configuração e Salvamento via Interface Web
**Objetivo**: Validar gravação na NVS e reinício automático.

1. Estando conectado no painel web (do Cenário 1):
   - Altere o **Hostname** para `faroldns-teste`.
   - Preencha o SSID e a Senha da sua rede Wi-Fi local.
   - Configure o **DNS Upstream** para `8.8.8.8` (Google DNS).
   - Clique em **Salvar Configurações**.
2. **Resultado Esperado**:
   - A tela deve mostrar uma mensagem de salvamento com sucesso.
   - No console serial do ESP32, você verá logs de parse JSON, salvamento na NVS e:
     `Reiniciando o sistema...`
   - O dispositivo reiniciará (soft reset).

---

### Cenário 3: Funcionamento e Conectividade via Wi-Fi Station (Sem Ethernet)
**Objetivo**: Validar a conexão Wi-Fi como cliente e a resolução DNS básica.

1. Mantenha o cabo Ethernet desconectado. Deixe o dispositivo ligar após o reset do Cenário 2.
2. **Resultado Esperado no Console**:
   - O dispositivo tentará carregar as configurações do Wi-Fi salvo na NVS e iniciará o Wi-Fi no modo Station (`NET_STATE_WIFI_STA`).
   - Você verá os logs de conexão com a sua rede Wi-Fi e aquisição de IP (ex: `192.168.1.150`).
3. **Teste de Rede**:
   - Conecte o seu computador de teste na mesma rede Wi-Fi local.
   - Abra o terminal do computador e teste o mDNS:
     ```bash
     ping faroldns-teste.local
     ```
     (Deve responder com o IP que o ESP32 obteve no Wi-Fi).
   - Acesse o painel pelo navegador em [http://faroldns-teste.local](http://faroldns-teste.local).
4. **Teste de DNS**:
   - No terminal do computador de teste, envie uma consulta DNS apontando para o IP do ESP32:
     ```bash
     nslookup google.com <IP_DO_ESP32_WIFI>
     ```
     ou
     ```bash
     dig @<IP_DO_ESP32_WIFI> google.com
     ```
   - O DNS deve responder com o IP público do Google (resolvido via Upstream `8.8.8.8`).

---

### Cenário 4: Conexão Ethernet Física (Cabo conectado)
**Objetivo**: Validar a comutação automática de interface (Prioridade Ethernet).

1. Com o ESP32 rodando no Wi-Fi (Cenário 3), conecte o cabo de rede Ethernet RJ45 na placa.
2. **Resultado Esperado**:
   - O driver W5500 detecta o link ativo.
   - O console serial exibirá:
     - `Ethernet conectada e ativa. Desativando Wi-Fi...`
     - O Wi-Fi do ESP32 é desligado.
     - O estado do sistema é atualizado para `NET_STATE_ETHERNET`.
     - O ESP32 obtém um IP na interface Ethernet (ex: `192.168.1.200`).
3. **Teste de Rede**:
   - Acesse o painel pelo IP da Ethernet ou por [http://faroldns-teste.local](http://faroldns-teste.local).
   - Faça uma query DNS apontando para o IP da Ethernet do ESP32:
     ```bash
     nslookup github.com <IP_DO_ESP32_ETHERNET>
     ```
     (A query deve ser resolvida com sucesso).

---

### Cenário 5: Perda Física de Conexão Ethernet (Failover)
**Objetivo**: Validar a transição automática de volta para o Wi-Fi.

1. Com o ESP32 rodando normalmente na Ethernet (Cenário 4), remova o cabo de rede RJ45.
2. **Resultado Esperado**:
   - O driver detecta a queda do link.
   - O console do monitor serial exibirá:
     - `Ethernet inativa ou cabo desconectado. Iniciando fallback via Wi-Fi...`
     - O rádio Wi-Fi é reativado no modo Station.
     - O ESP32 conecta de novo na rede Wi-Fi salva e obtém o IP Wi-Fi.
     - O estado atualiza para `NET_STATE_WIFI_STA`.
3. **Teste de Rede**:
   - Verifique se o dispositivo continua respondendo em [http://faroldns-teste.local](http://faroldns-teste.local) e respondendo a consultas DNS no IP do Wi-Fi.
