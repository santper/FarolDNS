# Plano de Ação — FarolDNS

> Gerado em 20/06/2026 após análise completa do código (Antigravity CLI).
> Baseado nos arquivos: arquivos fonte em `main/` e `components/`, `docs/historico_projeto.md`,
> `docs/plano_de_testes.md`, `docs/dicas.txt`, `Resumo da Sessão 2026-06-19.md`, `.opencoderules.md`.

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

## Fase 1 — Correções e Estabilização (Prioridade Alta)

| # | Tarefa | Arquivos | Descrição |
|---|--------|----------|-----------|
| 1.1 | **Corrigir mDNS p/ interface ativa** | `mdns_manager.c`, `network_manager.c` | Chamar `mdns_register_netif()` na interface ativa (Ethernet ou Wi-Fi) ao trocar estado. Atualmente o mDNS só funciona no Wi-Fi porque a netif padrão não é atualizada. |
| 1.2 | **Atraso de boot (4s)** | `network_manager.c:63` | Substituir `vTaskDelay(4000)` por espera orientada a evento: aguardar `ETHERNET_EVENT_CONNECTED` com timeout de ~8s, ou usar `ulTaskNotifyTake` com timeout. Testes mostraram link Ethernet entre 3s e 5s+. |
| 1.3 | **Race condition boot** | `network_manager.c` | Após o timeout, verificar `eth_w5500_is_connected()` **uma vez mais** antes de decidir fallback. O evento `ETHERNET_EVENT_CONNECTED` pode chegar durante o processamento do timeout. |
| 1.4 | **Remover `gpio_install_isr_service` duplicado** | `ethernet_w5500.c:140` | Pode crashar se já instalado por outro componente. Tratar retorno `ESP_ERR_INVALID_STATE` como OK. |

---

## Fase 2 — Expansão da Configuração (Média Prioridade)

| # | Tarefa | Arquivos | Descrição |
|---|--------|----------|-----------|
| 2.1 | **Campos `wifi_enabled` / `eth_enabled`** | `storage_manager.h`, `web_config.c`, `index.html` | Adicionar flags booleanas ao `faroldns_config_t` para habilitar/desabilitar cada interface independentemente. |
| 2.2 | **Suporte a dual-interface simultânea** | `network_manager.c`, `wifi_manager.c` | Se ambas interfaces habilitadas com IPs diferentes, manter ambas ativas (sub-redes diferentes). Se mesmo IP, prioridade Ethernet (como hoje). |
| 2.3 | **Migrar config p/ NVS em campo individual** | `storage_manager.c` | Em vez de blob único, salvar campos individuais na NVS para permitir versionamento futuro e evitar corrupção por mudança de struct. |

---

## Fase 3 — Melhorias na Web UI (Média Prioridade)

| # | Tarefa | Arquivos | Descrição |
|---|--------|----------|-----------|
| 3.1 | **Informações do sistema na dashboard** | `web_config.c`, `index.html` | Nova rota `GET /api/status` retornando: interface ativa, IPs, estado da rede, contadores de pacotes DNS. |
| 3.2 | **Exibir IP atual na UI** | `index.html`, `web_config.c` | Mostrar IP(s) vigentes, interface ativa, estado do sistema no canto superior. |
| 3.3 | **Proteger painel com senha** | `web_config.c` | Adicionar autenticação básica ou token no `POST /save` para evitar que qualquer pessoa na rede mude a configuração. |

---

## Fase 4 — DNS Cache e Performance (Baixa Prioridade)

| # | Tarefa | Arquivos | Descrição |
|---|--------|----------|-----------|
| 4.1 | **Cache DNS em PSRAM** | `dns_server.c`, novo `components/dns_cache/` | Tabela hash simples com TTL, usando heap PSRAM (8MB disponível). Consultas repetidas resolvidas sem upstream. |
| 4.2 | **Aumentar buffer DNS** | `dns_server.c:16` | Suportar EDNS0 (>512 bytes) para DNSSEC, IPv6 e respostas maiores. |
| 4.3 | **Timeout adaptativo** | `dns_server.c:99-102` | Ajustar timeout baseado em latência histórica do upstream em vez de 2s fixo. |

---

## Fase 5 — Qualidade e Testes (Contínuo)

| # | Tarefa | Arquivos | Descrição |
|---|--------|----------|-----------|
| 5.1 | **Adicionar `Kconfig.projbuild`** | Cada componente | Permitir configuração via `idf.py menuconfig` (ex: pinos, timeouts, stack sizes, pool size). |
| 5.2 | **Testes de unidade (Unity)** | `test/` | Testes para `storage_manager` (save/load) e `dns_server` (parse/serialização). |
| 5.3 | **Logs estruturados** | Todos | Adicionar `ESP_LOGV` para debug verbose em cada componente. |

---

## Referências

- `Resumo da Sessão 2026-06-19.md` — resumo detalhado da última sessão com Antigravity, incluindo correções aplicadas, resultados de testes e pendências.
- `espmon.log` — logs seriais capturados durante os testes de bancada.

---

## Como retomar o desenvolvimento

Na próxima sessão, peça para a IA ler:

1. `docs/plano_de_acao.md` — este arquivo (prioridades e pendências)
2. `docs/historico_projeto.md` — histórico completo e decisões de arquitetura
3. `docs/plano_de_testes.md` — cenários de teste
4. `docs/dicas.txt` — observações do usuário
5. `.opencoderules.md` — regras de codificação do projeto
6. `Resumo da Sessão 2026-06-19.md` — última sessão com Antigravity

E também os arquivos fonte relevantes para a tarefa específica que for iniciar.
