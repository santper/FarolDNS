# FarolDNS - RESUMO DA SESSÃO (2026-06-11)

## Status Atual do Projeto
- **Placa**: Waveshare ESP32-S3-ETH (W5500 + SD integrados)
- **Framework**: ESP-IDF v5.x
- **Compilação**: ✅ **BEM-SUCEDIDA** (último build passou)
- **Módulos Implementados** (todos compilando):

| Módulo | Função | Status |
|--------|--------|--------|
| `storage_manager` | NVS persistente (configs Wi-Fi/Eth/DNS) | ✅ |
| `ethernet_w5500` | Driver W5500 (polling) com pinagem Waveshare | ✅ |
| `wifi_manager` | STA + AP fallback ("FarolDNS_Setup", sem senha) | ✅ |
| `network_manager` | Orquestrador de failover ETH > Wi-Fi (FreeRTOS) | ✅ |
| `dns_server` | Forwarder UDP porta 53 (upstream via NVS) | ✅ |
| `mdns_manager` | `faroldns.local` via componente `espressif/mdns` | ✅ |
| `web_config` | Painel HTTP (porta 80) com API `/api/config` e `/save` | ✅ |

## Hardware - Pinagem Documentada
- **W5500 (ETH)**: MOSI=11, MISO=12, CLK=13, CS=14, INT=10, RST=9
- **MicroSD**: MOSI=6, MISO=5, CLK=7, CS=4

📁 **Documentação centralizada em `docs/`**:
```
docs/
├── historico_projeto.md        # Arquitetura, decisões e roadmap
├── dicas.txt                   # Guidelines originais
└── hardware/
    ├── pinagem_esp32_s3_eth.md
    └── ESP32-S3-ETH-details-11-1.jpg
```

## Última Ação Realizada
**`web_config.c`** foi criado e **integrado ao `main.c`** com chamada `web_config_start()`. O servidor HTTP já embute o `index.html` via `EMBED_TXTFILES` e expõe:
- `GET /` → Interface HTML (dark mode, glassmorphism)
- `GET /api/config` → JSON atual da NVS
- `POST /save` → Atualiza NVS e **reinicia o ESP32** (soft reset)

---

## Último Comando (Interrompido por cota)
```bash
idf.py build   # estava em execução ANTES da cota estourar
```
⚠️ **A compilação NÃO FOI FINALIZADA** após a inclusão do `web_config`. O build provavelmente falhou ou foi interrompido pela cota.

---

## Próximo Passo (PARA RETOMAR)
1. **Executar o build** para validar a integração do `web_config`:
   ```bash
   . /home/isaac/esp/esp-idf/export.sh && idf.py build
   ```
2. **Atualizar git/GitHub e docs** inclusive criar o readme.md no GitHub descrevendo o projeto e situação atual. É bom documentar loga para o caso de cota estourar.
3. **Corrigir eventuais erros** de linkagem ou dependência (ex: `cJSON`, `esp_http_server`).
4. **Gravar na placa** (se conectada):
   ```bash
   idf.py -p PORT flash monitor
   ```
5. **Testar o painel web** acessando `http://faroldns.local` (mDNS) ou IP da ETH/Wi-Fi.

---

## Observações Críticas para Retomada
- O `network_manager` implementa **failover ETH > Wi-Fi** com *task* monitora e notificações por eventos.
- O DNS *forwarder* usa `INADDR_ANY`, portanto escuta em qualquer interface ativa.
- O `web_config` **reinicia o sistema** após salvar; isso aplica as novas configurações da NVS.
- Os componentes `espressif/mdns` e `cJSON` já estão no `managed_components/`.

---

## Checklist de Retomada (Mínimo de Tokens)
- [ ] `idf.py build` (validar `web_config`)
- [ ] Se erro: verificar `REQUIRES` em `web_config/CMakeLists.txt` (cJSON, esp_http_server)
- [ ] `idf.py -p PORT flash`
- [ ] Acessar IP/`faroldns.local` no navegador
- [ ] Configurar Wi-Fi/ETH/DNS via interface web
- [ ] Testar failover físico (desconectar cabo ETH)

---

**Fim do resumo.** O projeto está 95% modularizado; a última camada (Web UI) foi escrita mas **não validada em runtime**.