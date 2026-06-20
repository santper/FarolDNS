# FarolDNS - RESUMO DA SESSÃO (2026-06-19)

## Status Atual do Projeto
- **Placa**: Waveshare ESP32-S3-ETH (W5500 + SD integrados)
- **Framework**: ESP-IDF v5.x
- **Compilação**: ✅ **BEM-SUCEDIDA** (último build passou)
- **Firmware gravado**: ✅ **SIM** (em `/dev/ttyACM0`)

---

## O Que Foi Feito Nesta Sessão

### 1. Correções de Baixo Nível (Aplicadas e Gravadas)

| Problema | Solução | Status |
|----------|---------|--------|
| **PSRAM Boot Loop** (placa reiniciava infinitamente) | Adicionado `CONFIG_SPIRAM_MODE_OCT=y` e `CONFIG_SPIRAM_SPEED_80M=y` no `sdkconfig.defaults` | ✅ |
| **MAC Address zerado (`00:00:00:00:00:00`)** no W5500 | Lido MAC do eFuse com `esp_read_mac()` e aplicado com `esp_eth_ioctl(ETH_CMD_S_MAC_ADDR)` | ✅ |
| **SPI frame format incorreto** | Alterado `command_bits = 0` para `command_bits = 16` no `ethernet_w5500.c` | ✅ |
| **GPIO ISR warning** | Adicionado `gpio_install_isr_service(0)` antes de iniciar o W5500 | ✅ |
| **Detecção de cabo lenta** | `s_connected = true` agora é setado em `ETHERNET_EVENT_CONNECTED` (antes só no `GOT_IP`) | ✅ |
| **Race condition no boot** | Ainda em análise, mas parcialmente mitigado | ⚠️ |

### 2. Testes Realizados pelo Usuário

| Cenário | Resultado |
|---------|-----------|
| **Cenário 1**: Configuração inicial via Wi-Fi AP (`FarolDNS_Setup`) | ✅ Funcionou |
| **Cenário 2**: Wi-Fi Station conectando ao roteador | ✅ Funcionou |
| **Cenário 3**: `nslookup` e `dig` via IP do Wi-Fi | ✅ Funcionou |
| **Cenário 4**: Ethernet física ativando e desativando Wi-Fi | ❌ **FALHOU** (versão antiga do firmware) |
| **Cenário 5**: Ethernet com IP diferente do Wi-Fi | ❌ **FALHOU** (MAC zerado impedia DHCP) |

**⚠️ Importante**: Os testes foram feitos com firmware compilado em `00:45:28` (versão **anterior** às correções). O firmware corrigido foi gravado às `22:18` e aguarda testes.

---

## Problemas Identificados e Pendentes

### 1. IP Diferente entre Wi-Fi (`.168`) e Ethernet (`.169`)
- **Causa**: DHCP do roteador atribui IPs diferentes para MACs diferentes (Wi-Fi e Ethernet têm MACs distintos).
- **Decisão do usuário**: Servidor DNS deve ter IP **estático**.
- **Solução proposta**:
  - Opção A: Configurar IP estático na Ethernet via UI web (firmware).
  - Opção B: Criar reserva de DHCP no roteador para o MAC da Ethernet (`28:84:85:54:b7:6f`).

### 2. mDNS (`faroldns.local`) só funciona no Wi-Fi
- **Causa**: `mdns_init()` é chamado antes de qualquer interface ter IP. Ao comutar para Ethernet, o mDNS não tem `netif` associado.
- **Solução proposta**: Criar `mdns_manager_set_netif()` e chamar ao comutar interfaces.

### 3. Latência no boot (Ethernet demora para ativar)
- **Causa**: Espera inicial de 4s pode ser insuficiente; há race condition entre o event handler e a checagem da task.
- **Solução proposta**: Aumentar espera inicial + debounce de 500ms antes de decidir pelo Wi-Fi.

### 4. Nova Arquitetura Solicitada (Alta Prioridade)
O usuário quer uma **lógica de rede mais flexível** na UI web:

| Configuração | Comportamento |
|--------------|---------------|
| **Wi-Fi: ON / ETH: OFF** | Apenas Wi-Fi ativo |
| **Wi-Fi: OFF / ETH: ON** | Apenas Ethernet ativo |
| **Ambos ON com mesmo IP** | Apenas um ativo por vez (prioridade ETH) |
| **Ambos ON com IPs diferentes** | Ambos ativos simultaneamente (até em sub-redes diferentes) |

**📋 Pendente**: Atualizar a estrutura de configuração (`storage_manager.h`) para armazenar:
- `wifi_enabled`
- `eth_enabled`
- `wifi_ip`, `wifi_netmask`, `wifi_gw`
- `eth_ip`, `eth_netmask`, `eth_gw`

---

## Arquivos Modificados/Adicionados

| Arquivo | Alteração |
|---------|-----------|
| `sdkconfig.defaults` | Adicionado `CONFIG_SPIRAM_MODE_OCT=y` e `CONFIG_SPIRAM_SPEED_80M=y` |
| `components/ethernet_w5500/ethernet_w5500.c` | Correções: `command_bits=16`, MAC via eFuse, `s_connected` no link up, GPIO ISR |
| `docs/historico_projeto.md` | **PENDENTE** (deve ser atualizado) |
| `docs/plano_de_testes.md` | Criado com cenários de teste |
| `README.md` | Criado com visão geral do projeto |

---

## Commits Realizados (GitHub)

| Commit | Descrição |
|--------|-----------|
| `32070c2` | Adiciona plano de testes e documentação de funcionalidades |
| `b2a78ae` | Corrige configuração de PSRAM Octal e inicialização do barramento SPI/GPIO do W5500 |

**Último push**: `b2a78ae` (enviado para `origin/main`)

---

## Próximo Passo (PARA RETOMAR)

1. **Atualizar `docs/historico_projeto.md`** com:
   - Correções realizadas nesta sessão
   - Problemas identificados (IP diferente, mDNS, latência)
   - Nova arquitetura de rede solicitada (Wi-Fi/ETH com IPs diferentes)

2. **Push para GitHub** das atualizações pendentes.

3. **Implementar nova arquitetura de rede**:
   - Expandir `storage_manager.h` com flags de enable e IPs separados
   - Modificar `web_config/index.html` e `web_config.c` para suportar nova UI
   - Ajustar `network_manager.c` para suportar ambos os modos (ativo único ou dual)

4. **Gravar nova versão e testar cenários 4 e 5** com o firmware corrigido.

---

## Logs Disponíveis
- `espmon.log` na raiz do projeto (logs da serial durante os testes)

---

## Comandos Úteis para Retomada

```bash
# Compilar
. /home/isaac/esp/esp-idf/export.sh && idf.py build

# Gravar
idf.py -p /dev/ttyACM0 flash

# Monitorar
idf.py -p /dev/ttyACM0 monitor