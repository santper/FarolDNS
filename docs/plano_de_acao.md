# Plano de Ação — FarolDNS

> Gerado em 20/06/2026 após análise completa do código (Antigravity CLI).
> Baseado nos arquivos: arquivos fonte em `main/` e `components/`, `docs/historico_projeto.md`,
> `docs/plano_de_testes.md`, `docs/dicas.txt`, `.opencoderules.md`.

## Contexto Atual

- **7 componentes modulares** implementados e compilando no ESP-IDF v5.3
- Sistema funcional: failover Ethernet↔Wi-Fi, DNS forwarder, mDNS, web UI
- ~1500 linhas de C, web UI de 330 linhas (HTML+CSS+JS embarcado)
- 3 issues conhecidas (boot race, mDNS por interface, subrede IP)
- Placa alvo: Waveshare ESP32-S3-ETH (W5500 + 8MB PSRAM)

## Estrutura do Projeto

```
FarolDNS/
├── main/main.c                          # Orquestrador (app_main)
├── components/
│   ├── storage_manager/                 # NVS Flash persistence
│   ├── ethernet_w5500/                  # W5500 SPI Ethernet driver
│   ├── wifi_manager/                    # Wi-Fi STA + AP
│   ├── network_manager/                 # Failover state machine (FreeRTOS)
│   ├── dns_server/                      # DNS forwarder UDP:53
│   ├── mdns_manager/                    # mDNS (<hostname>.local)
│   └── web_config/                      # HTTP server + dashboard UI
└── docs/
    ├── plano_de_acao.md                 # ← Este arquivo
    ├── plano_de_testes.md               # Cenários de teste de bancada
    ├── historico_projeto.md             # Histórico de desenvolvimento
    └── dicas.txt                        # Notas do usuário
```

---

## Fase 0 — Testar Firmware Corrigido (Imediato)

O firmware com as correções da sessão 19/06 foi gravado mas **nunca testado**. Antes de qualquer alteração, validar:

| # | Tarefa | Descrição |
|---|--------|-----------|
| 0.1 | **Testar Cenário 4 (Ethernet)** | Conectar cabo RJ45, verificar se Ethernet sobe e Wi-Fi desliga. `dig @<IP_ETH> google.com` |
| 0.2 | **Testar Cenário 5 (Failover)** | Remover cabo Ethernet, verificar fallback para Wi-Fi. `dig @<IP_WIFI> google.com` |
| 0.3 | **Testar IP estático** | Configurar IP estático via web UI, reboot, verificar se IP fixo é mantido |

### Resultados dos Testes (espmon.log — 19/06 22:37-22:51)

O log capturou **3 boots** do firmware corrigido (`b2a78ae-dirty`). Resultados:

| Teste | Resultado |
|-------|-----------|
| PSRAM 8MB | ✅ OK nos 3 boots |
| MAC eFuse (`28:84:85:54:b7:6f`) | ✅ Lido corretamente |
| Wi-Fi Station ("santper") | ✅ Conectou, IP `192.168.3.168` |
| Ethernet DHCP | ✅ IP `192.168.3.169` |
| Failover Ethernet→Wi-Fi (cabo removido) | ✅ Transição em ~1s |
| Failover Wi-Fi→Ethernet (cabo conectado) | ✅ Transição imediata |
| DNS Forwarding (`1.1.1.1`) | ✅ Consultas resolvidas |
| Web UI | ✅ Acessível |
| mDNS | ❌ Sem consultas nos logs |
| **Boot race condition** | ⚠️ Confirmada: Boot 1 e 2 dispararam Wi-Fi antes do Ethernet Link Up (delay de 4s insuficiente). Boot 3 funcionou (link em 3s). |

**Conclusão**: O firmware corrigido está funcional, mas a **race condition no boot** (item 1.2) precisa ser resolvida para eliminar o fallback Wi-Fi desnecessário ao ligar com cabo Ethernet conectado.

---

## Fase 1 — Correções e Estabilização ✅

| # | Tarefa | Status |
|---|--------|--------|
| 1.1 | mDNS gerenciado automaticamente pelo ESP-IDF | ✅ |
| 1.2 | `vTaskDelay(4000)` → `ulTaskNotifyTake(8s)` | ✅ |
| 1.3 | Debounce 500ms pós-timeout | ✅ |
| 1.4 | ISR duplicado: `ESP_ERR_INVALID_STATE` como OK | ✅ |

## Fase 2 — IP Único + Dashboard com Status ✅

As tarefas 2.1, 3.1 e 3.2 foram fundidas e implementadas em uma única rodada:

| # | Tarefa | Arquivos | Descrição |
|---|--------|----------|-----------|
| 2.a | **Struct IP único** | `storage_manager.h/c` | Substituído `wifi_ip/eth_ip` + `wifi_dhcp/eth_dhcp` por conjunto único: `dhcp_enabled`, `ip`, `netmask`, `gw`. Adicionado `config_version` para migração automática de NVS. |
| 2.b | **Seletor de modo de rede** | `network_manager.c`, `index.html` | Campo `net_mode` (auto/eth_only/wifi_only) no config + seletor na UI. Oculta seção conforme modo. |
| 2.c | **Captura automática de IP DHCP** | `ethernet_w5500.c`, `wifi_manager.c` | No primeiro `GOT_IP`, salva o IP obtido como fixo e desliga DHCP. Próximos boots usam esse IP. |
| 2.d | **`GET /api/status`** | `web_config.c`, `dns_server.h/c` | Retorna: interface ativa, IP, MAC, uptime, consultas DNS, bytes rx/tx. Contadores com `volatile` para leitura cross-task. |
| 2.e | **Dashboard com status** | `index.html` | Card animado no topo com indicador visual, IP, MAC, consultas DNS, KB trafegados. Polling a cada 5s. |
| 2.f | **Hostname único via probe mDNS** | `mdns_manager.c/h` | Sonda `faroldns1`, `faroldns2`... via `mdns_query_a` até achar um disponível. Default: `faroldns1`. Salva na NVS. |
| 2.g | **Compatibilidade mDNS com Ethernet** | `ethernet_w5500.c` | `if_key` alterado de `"ETH_SPI_5500"` para `"ETH_DEF"` (o mDNS busca por `ETH_DEF`). |

---

## Fase 3 — Pré-requisito: Flash 16MB + OTA ✅

| # | Tarefa | Descrição | Status |
|---|--------|-----------|--------|
| 3.a | **Flash 2MB → 16MB** | `sdkconfig.defaults`: `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y` | ✅ |
| 3.b | **Partição customizada (OTA)** | `partitions.csv`: ota_0 6MB, ota_1 6MB, storage 4MB | ✅ |
| 3.c | **SPIFFS habilitado** | Config no `sdkconfig.defaults` para future uso com blocklists | ✅ |

---

## Fase 4 — Resolver DNS Recursivo ⏸️ (Suspenso)

| # | Tarefa | Status | Observação |
|---|--------|--------|------------|
| 4.1 | **Root hints embarcados** | ✅ | Criado `dns_root_hints.h` |
| 4.2 | **Resolução recursiva iterativa** | ❌ | Implementado mas com bugs na delegação NS/glue. Substituído temporariamente por forwarder com cache. |
| 4.3 | **Cache DNS em PSRAM** | ✅ | Implementado e funcionando no forwarder |
| 4.4 | **EDNS0** | ✅ | Buffer de 1500 bytes habilitado |

> **Nota**: O recursivo está suspenso. O forwarder com cache (atual) resolve rapidamente consultas repetidas via cache e encaminha as demais para o upstream (1.1.1.1). O recursivo será retomado em sessão futura.

---

## Fase 5 — MicroSD + Bloqueio de Anúncios (Prioridade Média)

| # | Tarefa | Arquivos | Descrição |
|---|--------|----------|-----------|
| 5.1 | **Driver MicroSD SPI** | `components/sd_card/` (novo) | Inicializar SD card GPIO 4-7, montar FATFS |
| 5.2 | **Carregar blocklists** | `dns_server.c` | Ler listas estilo Pi-Hole do SD para PSRAM |
| 5.3 | **Bloqueio DNS** | `dns_server.c` | Responder NXDOMAIN para domínios bloqueados |

---

## Fase 6 — Baixa Prioridade

| # | Tarefa | Arquivos | Descrição |
|---|--------|----------|-----------|
| 6.1 | **Kconfig.projbuild** | Cada componente | Expor configurações no `idf.py menuconfig` |
| 6.2 | **Senha no painel** | `web_config.c` | Autenticação básica no `POST /save` |

---

## Referências

- `docs/historico_projeto.md` (seções E, F, G) — histórico completo e últimas sessões.
- `espmon.log` — logs seriais capturados durante os testes de bancada.

---

## Como retomar o desenvolvimento

Na próxima sessão, peça para a IA ler:

1. `docs/plano_de_acao.md` — este arquivo (prioridades e pendências)
2. `docs/historico_projeto.md` — histórico completo, decisões de arquitetura e últimas sessões
3. `docs/plano_de_testes.md` — cenários de teste
4. `docs/dicas.txt` — observações do usuário
5. `.opencoderules.md` — regras de codificação do projeto

E também os arquivos fonte relevantes para a tarefa específica que for iniciar.
