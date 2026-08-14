# Análise de Segurança - Servidor DNS (dns_server.c)

## Resumo Executivo

O código do servidor DNS forwarder (`/workspace/components/dns_server/dns_server.c`) apresenta **múltiplas vulnerabilidades críticas** que o tornam **inadequado para uso em produção**. Abaixo está a análise detalhada de cada falha, classificada por severidade, com recomendações específicas de correção.

---

## 🔴 FALHAS CRÍTICAS (Segurança)

### 1. DNS Rebinding Attack (CRÍTICO)

**Localização:** Linhas 275-298 (`dns_server_task`)

**Descrição:** O servidor encaminha cegamente respostas DNS do upstream para os clientes sem validar se os IPs retornados são de redes privadas/internas (RFC 1918). Isso permite que atacantes realizem ataques de DNS rebinding, redirecionando vítimas para serviços internos da rede local.

**Código Vulnerável:**
```c
// Forward to upstream
int fwd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
// ... envia query e recebe resposta
if (rlen > 0) {
    // Cache the response
    uint32_t ttl = dns_parse_min_ttl(tx, rlen);
    cache_insert(qname, qtype, ttl, tx, rlen);
    
    // ENVIA RESPOSTA SEM VALIDAÇÃO ALGUMA
    int sent = sendto(server_sock, tx, rlen, 0, (struct sockaddr *)&client, clen);
}
```

**Impacto:** Um atacante pode registrar um domínio que retorna IPs privados (ex: 192.168.1.1, 10.0.0.1) e fazer com que dispositivos na rede interna acessem serviços administrativos, câmeras IoT, impressoras, etc.

**Correção Necessária:**
- Implementar função `dns_validate_response()` que verifica todos os IPs nas respostas DNS
- Filtrar IPs das faixas RFC 1918 (10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16)
- Filtrar localhost (127.0.0.0/8) e link-local (169.254.0.0/16)
- Retornar SERVFAIL se IPs inválidos forem detectados

---

### 2. Cache Poisoning / Transaction ID Bypass (CRÍTICO)

**Localização:** Linhas 252-261 (cache lookup) e 275-298 (forward)

**Descrição:** O código não valida o Transaction ID (ID da transação DNS) entre a query enviada ao cliente e a resposta recebida do upstream. Além disso, não verifica se a resposta veio realmente do servidor upstream esperado.

**Código Vulnerável:**
```c
// Check cache - NÃO VALIDA TRANSACTION ID
cache_entry_t *cached = cache_lookup(qname, qtype);
if (cached && cached->data) {
    memcpy(tx, cached->data, cached->len);
    // Fix the ID in cached response (CORREÇÃO PARCIAL E INSEGURA)
    tx[0] = rx[0]; tx[1] = rx[1];
    // ENVIA SEM VALIDAR SE A RESPOSTA É VÁLIDA
    int sent = sendto(server_sock, tx, cached->len, 0, (struct sockaddr *)&client, clen);
    continue;
}
```

**Problemas:**
1. Ao receber resposta do upstream, não há validação do Transaction ID
2. Não há verificação se `from` (quem enviou a resposta) é igual ao upstream configurado
3. O cache armazena respostas sem vincular ao Transaction ID original
4. Atacante na rede pode injetar respostas falsas antes do upstream responder

**Correção Necessária:**
- Armazenar Transaction ID no cache junto com a resposta
- Validar `tx[0], tx[1]` da resposta contra a query original antes de enviar ao cliente
- Verificar se `from.sin_addr.s_addr == inet_addr(srv_cfg.upstream_dns)`
- Implementar timeout adequado e descartar respostas fora da janela temporal

---

### 3. Buffer Overflow no Parser DNS (CRÍTICO)

**Localização:** Linhas 162-183 (`dns_decode_qname`)

**Descrição:** A função de decode de nomes DNS não protege adequadamente contra ponteiros de compressão DNS maliciosos, permitindo loops infinitos, leitura out-of-bounds, ou estouro de buffer.

**Código Vulnerável:**
```c
static int dns_decode_qname(const uint8_t *msg, int msglen, int offset, char *out, int outlen)
{
    int wrote = 0;
    while (offset < msglen) {
        if (msg[offset] == 0) return offset + 1;
        if ((msg[offset] & 0xC0) == 0xC0) {
            int ptr = ((msg[offset] & 0x3F) << 8) | msg[offset + 1];
            // PROBLEMA: Não verifica se ptr >= offset (loop circular)
            if (ptr >= msglen || ptr < 12) return -1;  // VERIFICAÇÃO INSUFICIENTE
            int ret = dns_decode_qname(msg, msglen, ptr, out + wrote, outlen - wrote);
            if (ret < 0) return -1;
            return offset + 2;
        }
        // ... restante do código
    }
    return -1;
}
```

**Vulnerabilidades Específicas:**
1. **Loop Circular:** Ponteiro de compressão pode apontar para posição atual ou anterior, causando recursão infinita
2. **Stack Overflow:** Recursão sem limite de profundidade máxima
3. **Buffer Overflow:** Verificação `wrote < outlen - 1` pode ser burlada com múltiplos ponteiros de compressão
4. **Leitura Out-of-Bounds:** Validação `ptr < 12` é arbitrária e pode não ser segura

**Correção Necessária:**
- Adicionar parâmetro de profundidade máxima de recursão (ex: max_depth = 10)
- Implementar detecção de loops: rastrear offsets já visitados
- Validar que `ptr < offset` (ponteiro sempre aponta para trás)
- Adicionar validação rigorosa de limites em todas as operações

---

### 4. Race Condition no Cache (ALTO)

**Localização:** Linhas 66-81 (`cache_lookup`) e 83-126 (`cache_insert`)

**Descrição:** O mutex do cache é liberado prematuramente em `cache_lookup`, criando uma janela de competição entre verificar e usar o entry do cache.

**Código Vulnerável:**
```c
static cache_entry_t* cache_lookup(const char *qname, uint16_t qtype)
{
    if (!s_cache_mutex) return NULL;
    xSemaphoreTake(s_cache_mutex, portMAX_DELAY);
    cache_purge();
    cache_entry_t *e = s_cache;
    while (e) {
        if (e->qtype == qtype && strcasecmp(e->qname, qname) == 0 && e->expiry > uptime_s()) {
            xSemaphoreGive(s_cache_mutex);  // ⚠️ LIBERA ANTES DE USAR
            return e;  // ⚠️ RETORNA PONTEIRO QUE PODE SER INVALIDADO
        }
        e = e->next;
    }
    xSemaphoreGive(s_cache_mutex);
    return NULL;
}
```

**Problema:** Outra thread pode remover/modificar o entry do cache imediatamente após o mutex ser liberado, resultando em use-after-free ou dados corrompidos.

**Correção Necessária:**
- Manter mutex até que os dados sejam copiados para buffer local
- Ou implementar copy-on-read dos dados do cache
- Considerar RCU (Read-Copy-Update) para melhor performance

---

## 🟠 FALHAS DE MÉDIA SEVERIDADE

### 5. Vazamento de Memória (MÉDIO)

**Localização:** Linhas 113-126 (`cache_insert`)

**Descrição:** Se `malloc(e->data)` falhar, a struct `e` já foi alocada mas não é liberada.

**Código Vulnerável:**
```c
cache_entry_t *e = malloc(sizeof(cache_entry_t));
if (!e) { xSemaphoreGive(s_cache_mutex); return; }
memset(e, 0, sizeof(*e));
// ... inicialização ...
e->data = malloc(len);
if (e->data) memcpy(e->data, data, len);  // ⚠️ Se falhar, 'e' vaza
e->next = s_cache;
s_cache = e;
```

**Correção:**
```c
e->data = malloc(len);
if (!e->data) { 
    free(e); 
    xSemaphoreGive(s_cache_mutex); 
    return; 
}
```

---

### 6. Validação Insuficiente de Packets DNS (MÉDIO)

**Localização:** Linhas 232-240

**Descrição:** Validação mínima do packet DNS recebido. Não verifica:
- `qdcount` máximo permitido
- `ancount`, `nscount`, `arcount` nos packets de resposta
- Tamanho total coerente com headers
- Flags malformadas

**Código Vulnerável:**
```c
if (len < 12) continue;  // Mínimo básico
uint16_t qdcount = (rx[4] << 8) | rx[5];
if (qdcount == 0) continue;  // Só verifica se é zero
// Nenhuma outra validação antes de parsear
```

**Correção:** Validar todos os campos do header DNS e limites antes de processar.

---

### 7. Timeout Fixo Muito Baixo (MÉDIO)

**Localização:** Linha 267

**Descrição:** Timeout de 3 segundos é insuficiente para redes instáveis ou upstreams lentos, causando falhas desnecessárias.

**Código Vulnerável:**
```c
struct timeval tv = { .tv_sec = 3, .tv_usec = 0 };  // ⚠️ FIXO E BAIXO
setsockopt(fwd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
```

**Correção:**
- Aumentar para 5-10 segundos
- Tornar configurável via NVS
- Implementar retry com backoff exponencial

---

### 8. Ausência de Rate Limiting (MÉDIO)

**Localização:** Todo o loop principal (linhas 220-299)

**Descrição:** Não há limitação de queries por segundo por cliente, tornando o servidor vulnerável a:
- DNS Amplification Attacks
- Query Floods (DoS)
- Exaustão de recursos (CPU, memória, cache)

**Correção Necessária:**
- Implementar contador de queries por IP/cliente
- Definir threshold (ex: 100 queries/segundo)
- Bloquear temporariamente clientes que excederem limite
- Adicionar logging de eventos suspeitos

---

### 9. Socket Leakage em Condições de Erro (MÉDIO)

**Localização:** Linhas 264-280

**Descrição:** Em certas condições de erro, o socket `fwd` pode não ser fechado adequadamente.

**Código Vulnerável:**
```c
int fwd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
if (fwd < 0) continue;  // OK

// ... setup ...

sendto(fwd, rx, len, 0, (struct sockaddr *)&up, sizeof(up));
// ⚠️ Se sendto falhar de forma específica, ainda executa recvfrom

struct sockaddr_in from;
socklen_t fromlen = sizeof(from);
int rlen = recvfrom(fwd, tx, MAX_DNS_PACKET, 0, (struct sockaddr *)&from, &fromlen);
close(fwd);  // ⚠️ Sempre fecha? E se houver erro antes?
```

**Correção:** Garantir `close(fwd)` em todos os caminhos de execução, preferencialmente com goto cleanup ou padrão similar.

---

### 10. Root Hints Não Utilizados (BAIXO)

**Localização:** `/workspace/components/dns_server/include/dns_root_hints.h`

**Descrição:** O arquivo `dns_root_hints.h` define servidores root DNS mas nunca é incluído ou utilizado no código. Código morto que pode causar confusão.

**Correção:**
- Remover o arquivo se não for necessário
- Ou implementar fallback para root hints quando upstream falhar

---

## 📋 RECOMENDAÇÕES GERAIS DE SEGURANÇA

### Prioridade 1 (Crítico - Corrigir Imediatamente)
1. ✅ Implementar validação de IPs privados nas respostas (DNS Rebinding protection)
2. ✅ Validar Transaction ID entre query e resposta
3. ✅ Corrigir buffer overflow no `dns_decode_qname`
4. ✅ Fixar race condition no cache

### Prioridade 2 (Alto - Corrigir Antes de Produção)
5. ✅ Implementar rate limiting por cliente
6. ✅ Adicionar validação completa de packets DNS
7. ✅ Corrigir vazamento de memória no cache_insert
8. ✅ Implementar múltiplos upstreams com failover

### Prioridade 3 (Médio - Melhorias Recomendadas)
9. ✅ Aumentar/configurar timeout do upstream
10. ✅ Adicionar suporte a DNSSEC (validação de integridade)
11. ✅ Implementar logging de segurança
12. ✅ Adicionar métricas de segurança (queries bloqueadas, etc.)

---

## 🛡️ CHECKLIST PARA A IA DE CORREÇÃO

Ao corrigir o código, garantir que TODOS os seguintes itens sejam implementados:

- [ ] Função `dns_validate_response()` que filtra IPs RFC 1918, localhost, link-local
- [ ] Validação de Transaction ID (query ID == response ID)
- [ ] Verificação de que resposta vem do upstream configurado
- [ ] Limite de profundidade de recursão em `dns_decode_qname()` (max 10)
- [ ] Detecção de loops em ponteiros de compressão DNS
- [ ] Mutex mantido durante todo o uso do entry do cache
- [ ] Correção de vazamento de memória quando `malloc(e->data)` falha
- [ ] Validação completa de headers DNS (qdcount, ancount, flags, tamanhos)
- [ ] Timeout configurável (NVS) com valor mínimo de 5 segundos
- [ ] Rate limiting: máximo X queries/segundo por IP
- [ ] Close garantido de sockets em todos os caminhos de erro
- [ ] Decisão sobre root hints (remover ou implementar)
- [ ] Comentários explicativos nas funções de segurança
- [ ] Testes unitários para cenários de ataque

---

## 📝 EXEMPLO DE IMPLEMENTAÇÃO ESPERADA

### Validação de IPs Privados (Exemplo)

```c
static bool is_private_ip(uint32_t ip)
{
    // 10.0.0.0/8
    if ((ip & htonl(0xFF000000)) == htonl(0x0A000000)) return true;
    // 172.16.0.0/12
    if ((ip & htonl(0xFFF00000)) == htonl(0xAC100000)) return true;
    // 192.168.0.0/16
    if ((ip & htonl(0xFFFF0000)) == htonl(0xC0A80000)) return true;
    // 127.0.0.0/8
    if ((ip & htonl(0xFF000000)) == htonl(0x7F000000)) return true;
    // 169.254.0.0/16 (link-local)
    if ((ip & htonl(0xFFFF0000)) == htonl(0xA9FE0000)) return true;
    return false;
}

static bool dns_validate_response(const uint8_t *msg, int len)
{
    // Parsear respostas e verificar todos os IPs
    // Retornar false se algum IP for privado
}
```

### Validação de Transaction ID (Exemplo)

```c
// Ao receber resposta do upstream
if (rlen > 12) {
    uint16_t resp_id = (tx[0] << 8) | tx[1];
    uint16_t query_id = (rx[0] << 8) | rx[1];
    
    if (resp_id != query_id) {
        ESP_LOGW(TAG, "Transaction ID mismatch");
        close(fwd);
        continue;
    }
    
    // Verificar se veio do upstream correto
    if (from.sin_addr.s_addr != up.sin_addr.s_addr) {
        ESP_LOGW(TAG, "Resposta de origem desconhecida");
        close(fwd);
        continue;
    }
    
    // OK para processar
}
```

---

## ⚠️ AVISO FINAL

**ESTE CÓDIGO NÃO DEVE SER USADO EM PRODUÇÃO** até que todas as falhas críticas sejam corrigidas. As vulnerabilidades identificadas permitem:
- Redirecionamento de tráfego para servidores maliciosos
- Envenenamento de cache DNS
- Ataques de negação de serviço
- Acesso não autorizado a recursos de rede interna

A correção deve seguir princípios de **defesa em profundidade**, implementando múltiplas camadas de validação e proteção.
