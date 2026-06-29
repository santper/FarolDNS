# FarolDNS 📡

Servidor DNS embarcado de baixo consumo de energia projetado para redes locais, com suporte a redundância física automática (failover Ethernet para Wi-Fi) e painel de controle web de fácil configuração.

O projeto roda nativamente no microcontrolador **ESP32-S3** usando o framework oficial **ESP-IDF v5.x** em C.

---

## 🛠️ Hardware Utilizado

- **Placa**: Waveshare ESP32-S3-ETH (com chip Ethernet W5500 e leitor MicroSD integrados de fábrica)
- **Módulo de Memória**: ESP32-S3-WROOM-1 N16R8 (16 MB Flash, 8 MB PSRAM)

### 📌 Pinagem Física (Waveshare ESP32-S3-ETH)

Para referência rápida, a pinagem utilizada pelos periféricos integrados é:

| Periférico | Sinal / Pino | GPIO (ESP32-S3) |
|------------|--------------|-----------------|
| **Ethernet (W5500)** | MOSI | GPIO 11 |
| | MISO | GPIO 12 |
| | CLK (SCLK) | GPIO 13 |
| | CS (SS) | GPIO 14 |
| | INT (Interrupt) | GPIO 10 |
| | RST (Reset) | GPIO 9 |
| **MicroSD** | MOSI | GPIO 6 |
| | MISO | GPIO 5 |
| | CLK (SCLK) | GPIO 7 |
| | CS (SS) | GPIO 4 |

---

## 🏗️ Arquitetura do Projeto

O firmware está estruturado de forma 100% modular dentro do diretório `components/`. O arquivo `main/main.c` atua apenas como um orquestrador de inicialização leve.

### Módulos (Componentes) Implementados:

1. **[storage_manager](components/storage_manager)**: Gerencia a partição NVS (Non-Volatile Storage) na Flash. Salva e recupera as configurações de rede (SSID, senha, modo DHCP/Estático) e o IP do servidor DNS público/upstream.
2. **[ethernet_w5500](components/ethernet_w5500)**: Driver para o chip Ethernet W5500 utilizando barramento SPI2. Configura a rede cabeada em modo Polling.
3. **[wifi_manager](components/wifi_manager)**: Gerencia a conexão Wi-Fi (modo Station) e cria um Access Point aberto (`FarolDNS_Setup`) de fallback caso o dispositivo precise ser configurado.
4. **[network_manager](components/network_manager)**: O coração da redundância. Monitora eventos das interfaces Ethernet e Wi-Fi:
   - Se o cabo Ethernet estiver conectado e obtiver IP, o Wi-Fi é desligado para economizar energia e evitar conflitos.
   - Se a Ethernet perder a conexão, o Wi-Fi é automaticamente ativado (se conecta à rede salva ou inicia em modo AP se não houver credenciais).
5. **[dns_server](components/dns_server)**: Servidor DNS forwarder UDP com cache em PSRAM. Escuta na porta 53, encaminha para upstream configurável (ex: `1.1.1.1`). Respostas são cacheadas com TTL para consultas repetidas instantâneas.
6. **[mdns_manager](components/mdns_manager)**: Registra o hostname local (`faroldns1`, `faroldns2`... via probe automático) facilitando o acesso pelo nome sem precisar do IP.
7. **[web_config](components/web_config)**: Servidor HTTP embarcado que serve uma interface web moderna (Dark Mode/Glassmorphism). Expõe as seguintes rotas:
   - `GET /` -> Painel administrativo com status em tempo real (`index.html` embutido).
   - `GET /api/config` -> Configurações atuais em JSON.
   - `GET /api/status` -> Status do sistema: interface ativa, IP, MAC, uptime, consultas DNS, bytes trafegados, versão do firmware.
   - `POST /save` -> Salva configurações em JSON, reinicia o ESP32.

---

## ✅ Requisitos Funcionais

| # | Requisito | Descrição |
|---|-----------|-----------|
| RF1 | DNS Forwarder com Cache | Escuta na porta UDP 53. Cache em PSRAM com TTL. Upstream configurável (ex: `1.1.1.1`) |
| RF2 | Failover automático | Prioridade Ethernet; se o cabo cair, Wi-Fi assume em segundos |
| RF3 | IP fixo único | IP compartilhado entre Ethernet e Wi-Fi. Captura automática do primeiro IP DHCP |
| RF4 | Painel web com status | Dark mode. Status em tempo real: interface, IP, consultas DNS, bytes, versão |
| RF5 | mDNS com probe | Hostname `faroldns1`/`faroldns2`... via sonda automática na rede |
| RF6 | Persistência | Configurações salvas em NVS flash com versionamento |
| RF7 | Modo AP inicial | Cria rede `FarolDNS_Setup` aberta se não houver Ethernet nem credenciais |
| RF8 | OTA | Dual partition para atualização over-the-air futura |

## 🎯 Casos de Uso

1. **Primeira inicialização**: Sem cabo Ethernet e sem credenciais salvas → AP `FarolDNS_Setup` aberto
2. **Configuração**: Conectar no AP, acessar `http://faroldns.local`, configurar rede e salvar
3. **Uso normal via Wi-Fi**: DNS forwarder ativo no IP do Wi-Fi
4. **Conexão do cabo Ethernet**: Wi-Fi desliga, DNS passa a responder no IP da Ethernet
5. **Queda da Ethernet**: Wi-Fi reativa automaticamente, DNS continua respondendo
6. **Reconfiguração**: Acessar web UI pelo IP vigente, alterar configurações e salvar

## 🏛️ Decisões de Arquitetura

- **Failover simples**: No máximo uma interface ativa por vez (seletor: `eth_only`, `wifi_only`, `auto`). Prioridade Ethernet quando em modo `auto`. Elimina conflitos ARP.
- **IP único**: Ethernet e Wi-Fi compartilham o mesmo IP configurado. Primeiro IP DHCP é capturado como fixo.
- **Modular**: Cada componente em `components/` com seu `CMakeLists.txt`. `main/main.c` é apenas orquestrador.
- **DNS forwarder com cache**: Encaminha para upstream (ex: `1.1.1.1`). Cache em PSRAM (até 256 entradas, TTL). Recursivo suspenso.
- **Configuração com versionamento**: Struct NVS com `config_version` para migração automática em caso de mudanças.
- **Flash 16MB com OTA**: Dual partition de 6MB cada + storage de 4MB para blocklists futuras.

## 🚀 Como Compilar e Rodar

### Pré-requisitos
- ESP-IDF v5.x instalado na máquina de desenvolvimento.

### 1. Exportar variáveis do ESP-IDF
```bash
. /home/isaac/esp/esp-idf/export.sh
```

### 2. Configurar o Target
```bash
idf.py set-target esp32s3
```

### 3. Compilar o Projeto
```bash
idf.py build
```

### 4. Gravar no ESP32-S3 e Monitorar
Selecione a porta serial correta (ex: `/dev/ttyACM0` ou `/dev/ttyUSB0`):
```bash
idf.py -p PORT flash monitor
```

---

## 📋 Como Testar o Dispositivo

1. **Primeira Inicialização**: Sem credenciais salvas, o dispositivo não conseguirá se conectar à rede Wi-Fi. Se o cabo de rede Ethernet não estiver conectado, ele criará a rede Wi-Fi aberta `FarolDNS_Setup`.
2. **Configuração**: Conecte-se à rede `FarolDNS_Setup` e acesse no navegador: [http://faroldns.local](http://faroldns.local) ou o IP do Access Point (normalmente `192.168.4.1`).
3. **Painel de Controle**: Insira as credenciais do seu Wi-Fi residencial/corporativo, configure se as interfaces usarão DHCP ou IPs estáticos, defina o DNS Upstream e salve. O ESP32 reiniciará automaticamente.
4. **Redundância**: Se você plugar o cabo Ethernet, o ESP32 desativará o Wi-Fi e passará a usar a rede cabeada. Se desconectar o cabo, a interface Wi-Fi assumirá a conexão de forma transparente em poucos segundos.

---

## 📚 Documentação

| Arquivo | Conteúdo |
|---------|----------|
| [`docs/historico_projeto.md`](docs/historico_projeto.md) | Histórico completo de desenvolvimento e decisões de arquitetura |
| [`docs/plano_de_acao.md`](docs/plano_de_acao.md) | Plano de tarefas priorizadas para as próximas sessões |
| [`docs/plano_de_testes.md`](docs/plano_de_testes.md) | Cenários de teste de bancada detalhados |
| [`docs/hardware/pinagem_esp32_s3_eth.md`](docs/hardware/pinagem_esp32_s3_eth.md) | Pinagem física da placa Waveshare ESP32-S3-ETH |
| [`.opencoderules.md`](.opencoderules.md) | Regras de codificação para sessões com IA |
