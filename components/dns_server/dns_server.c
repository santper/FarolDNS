#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <strings.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "storage_manager.h"
#include "network_manager.h"
#include "dns_server.h"

static const char *TAG = "DNSServer";
#define LOCAL_DNS_PORT 53
#define MAX_DNS_PACKET 1500
#define MAX_CACHE_ENTRIES 256

static TaskHandle_t s_dns_task_handle = NULL;
static bool s_running = false;
static volatile uint32_t s_query_count = 0;
static volatile uint32_t s_bytes_sent = 0;
static volatile uint32_t s_bytes_received = 0;

static uint32_t uptime_s(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

// --- DNS Cache (simples, respostas completas) ---
typedef struct cache_entry {
    uint16_t qid;
    char qname[256];
    uint16_t qtype;
    uint32_t expiry;
    uint16_t len;
    uint8_t *data;
    struct cache_entry *next;
} cache_entry_t;

static cache_entry_t *s_cache = NULL;
static int s_cache_count = 0;
static SemaphoreHandle_t s_cache_mutex = NULL;

static void cache_purge(void)
{
    uint32_t now = uptime_s();
    cache_entry_t **pp = &s_cache;
    while (*pp) {
        if ((*pp)->expiry <= now) {
            cache_entry_t *victim = *pp;
            *pp = victim->next;
            free(victim->data);
            free(victim);
            s_cache_count--;
        } else {
            pp = &(*pp)->next;
        }
    }
}

static cache_entry_t* cache_lookup(const char *qname, uint16_t qtype)
{
    if (!s_cache_mutex) return NULL;
    xSemaphoreTake(s_cache_mutex, portMAX_DELAY);
    cache_purge();
    cache_entry_t *e = s_cache;
    while (e) {
        if (e->qtype == qtype && strcasecmp(e->qname, qname) == 0 && e->expiry > uptime_s()) {
            xSemaphoreGive(s_cache_mutex);
            return e;
        }
        e = e->next;
    }
    xSemaphoreGive(s_cache_mutex);
    return NULL;
}

static void cache_insert(const char *qname, uint16_t qtype, uint32_t ttl_sec,
                          const uint8_t *data, uint16_t len)
{
    if (!s_cache_mutex) return;
    xSemaphoreTake(s_cache_mutex, portMAX_DELAY);
    cache_purge();

    // Remove existing entry for same qname/qtype
    cache_entry_t **pp = &s_cache;
    while (*pp) {
        if ((*pp)->qtype == qtype && strcasecmp((*pp)->qname, qname) == 0) {
            cache_entry_t *old = *pp;
            *pp = old->next;
            free(old->data);
            free(old);
            s_cache_count--;
            break;
        }
        pp = &(*pp)->next;
    }

    // Evict oldest if full
    while (s_cache_count >= MAX_CACHE_ENTRIES && s_cache) {
        cache_entry_t *victim = s_cache;
        s_cache = victim->next;
        free(victim->data);
        free(victim);
        s_cache_count--;
    }

    cache_entry_t *e = malloc(sizeof(cache_entry_t));
    if (!e) { xSemaphoreGive(s_cache_mutex); return; }
    memset(e, 0, sizeof(*e));
    strncpy(e->qname, qname, sizeof(e->qname) - 1);
    e->qtype = qtype;
    e->expiry = uptime_s() + ttl_sec;
    e->len = len;
    e->data = malloc(len);
    if (e->data) memcpy(e->data, data, len);
    e->next = s_cache;
    s_cache = e;
    s_cache_count++;
    xSemaphoreGive(s_cache_mutex);
}

// Parse TTL from first answer in a DNS response
static uint32_t dns_parse_min_ttl(const uint8_t *msg, int msglen)
{
    if (msglen < 12) return 60;
    uint16_t ancount = (msg[6] << 8) | msg[7];
    if (ancount == 0) return 60;

    int pos = 12;
    // Skip question
    while (pos < msglen && msg[pos] != 0) {
        if ((msg[pos] & 0xC0) == 0xC0) { pos += 2; break; }
        pos += msg[pos] + 1;
    }
    if (pos >= msglen) return 60;
    pos += 5; // skip null label + QTYPE + QCLASS

    // Read first answer TTL
    for (int i = 0; i < ancount && pos < msglen; i++) {
        // Skip name
        while (pos < msglen && msg[pos] != 0) {
            if ((msg[pos] & 0xC0) == 0xC0) { pos += 2; break; }
            pos += msg[pos] + 1;
        }
        if (pos >= msglen) return 60;
        pos++; // skip null label
        if (pos + 10 > msglen) return 60;
        uint32_t ttl = (msg[pos+4] << 24) | (msg[pos+5] << 16) | (msg[pos+6] << 8) | msg[pos+7];
        if (i == 0) return ttl;
        uint16_t rdlen = (msg[pos+8] << 8) | msg[pos+9];
        pos += 10 + rdlen;
    }
    return 60;
}

static int dns_decode_qname(const uint8_t *msg, int msglen, int offset, char *out, int outlen)
{
    int wrote = 0;
    while (offset < msglen) {
        if (msg[offset] == 0) return offset + 1;
        if ((msg[offset] & 0xC0) == 0xC0) {
            int ptr = ((msg[offset] & 0x3F) << 8) | msg[offset + 1];
            if (ptr >= msglen || ptr < 12) return -1;
            int ret = dns_decode_qname(msg, msglen, ptr, out + wrote, outlen - wrote);
            if (ret < 0) return -1;
            return offset + 2;
        }
        int labellen = msg[offset];
        offset++;
        if (offset + labellen > msglen) return -1;
        if (wrote > 0 && wrote < outlen - 1) out[wrote++] = '.';
        for (int i = 0; i < labellen && wrote < outlen - 1; i++)
            out[wrote++] = msg[offset + i];
        offset += labellen;
    }
    return -1;
}

static void dns_server_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Aguardando rede...");
    while (network_manager_get_state() == NET_STATE_INIT)
        vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGI(TAG, "Iniciando servidor DNS forwarder...");

    s_cache_mutex = xSemaphoreCreateMutex();

    int server_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (server_sock < 0) {
        ESP_LOGE(TAG, "Falha ao criar socket: errno %d", errno);
        s_running = false; vTaskDelete(NULL); return;
    }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(LOCAL_DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(server_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "Falha ao bind na porta %d: errno %d", LOCAL_DNS_PORT, errno);
        close(server_sock); s_running = false; vTaskDelete(NULL); return;
    }

    ESP_LOGI(TAG, "Servidor DNS escutando na porta %d", LOCAL_DNS_PORT);

    // Carrega config do upstream uma vez
    faroldns_config_t srv_cfg;
    storage_load_config(&srv_cfg);

    uint8_t rx[MAX_DNS_PACKET];
    uint8_t tx[MAX_DNS_PACKET];

    while (s_running) {
        struct sockaddr_in client;
        socklen_t clen = sizeof(client);
        int len = recvfrom(server_sock, rx, sizeof(rx), 0, (struct sockaddr *)&client, &clen);
        if (len < 0) {
            if (s_running) ESP_LOGE(TAG, "recvfrom: errno %d", errno);
            break;
        }

        s_query_count++;
        s_bytes_received += len;

        if (len < 12) continue;
        uint16_t qdcount = (rx[4] << 8) | rx[5];
        if (qdcount == 0) continue;

        // Extract query name and type
        char qname[256] = "";
        int name_end = dns_decode_qname(rx, len, 12, qname, sizeof(qname));
        if (name_end < 0 || name_end + 4 > len) continue;
        uint16_t qtype = (rx[name_end] << 8) | rx[name_end + 1];

        char client_ip[32];
        inet_ntoa_r(client.sin_addr, client_ip, sizeof(client_ip));
        ESP_LOGI(TAG, "Consulta de %s: %s (type %d)", client_ip, qname, qtype);

        if (network_manager_get_state() == NET_STATE_WIFI_AP) {
            ESP_LOGW(TAG, "Modo AP, ignorando.");
            continue;
        }

        // Check cache
        cache_entry_t *cached = cache_lookup(qname, qtype);
        if (cached && cached->data) {
            ESP_LOGI(TAG, "Cache hit para %s", qname);
            memcpy(tx, cached->data, cached->len);
            // Fix the ID in cached response
            tx[0] = rx[0]; tx[1] = rx[1];
            int sent = sendto(server_sock, tx, cached->len, 0, (struct sockaddr *)&client, clen);
            if (sent > 0) s_bytes_sent += sent;
            continue;
        }

        // Forward to upstream
        int fwd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (fwd < 0) continue;

        struct timeval tv = { .tv_sec = 3, .tv_usec = 0 };
        setsockopt(fwd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        struct sockaddr_in up;
        up.sin_family = AF_INET;
        up.sin_port = htons(53);
        up.sin_addr.s_addr = inet_addr(srv_cfg.upstream_dns);

        sendto(fwd, rx, len, 0, (struct sockaddr *)&up, sizeof(up));

        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);
        int rlen = recvfrom(fwd, tx, MAX_DNS_PACKET, 0, (struct sockaddr *)&from, &fromlen);
        close(fwd);

        if (rlen > 0) {
            ESP_LOGI(TAG, "Resposta de %s (%d bytes)", srv_cfg.upstream_dns, rlen);
            // Cache the response
            uint32_t ttl = dns_parse_min_ttl(tx, rlen);
            cache_insert(qname, qtype, ttl, tx, rlen);

            int sent = sendto(server_sock, tx, rlen, 0, (struct sockaddr *)&client, clen);
            if (sent > 0) s_bytes_sent += sent;
        } else {
            ESP_LOGW(TAG, "Timeout do upstream para %s", qname);
            // Respond SERVFAIL
            memset(tx, 0, 12);
            tx[0] = rx[0]; tx[1] = rx[1];
            tx[2] = 0x85; tx[3] = 0x02; // QR | RD | RA | SERVFAIL
            tx[4] = rx[4]; tx[5] = rx[5]; // keep QDCOUNT
            sendto(server_sock, tx, 12, 0, (struct sockaddr *)&client, clen);
        }
    }

    close(server_sock);
    s_running = false;
    vTaskDelete(NULL);
}

esp_err_t dns_server_start(void)
{
    if (s_running) return ESP_OK;
    s_running = true;
    BaseType_t ret = xTaskCreate(dns_server_task, "dns_server", 8192, NULL, 5, &s_dns_task_handle);
    if (ret != pdPASS) { s_running = false; return ESP_FAIL; }
    return ESP_OK;
}

void dns_server_stop(void) { s_running = false; }
uint32_t dns_server_get_query_count(void) { return s_query_count; }
uint32_t dns_server_get_bytes_sent(void) { return s_bytes_sent; }
uint32_t dns_server_get_bytes_received(void) { return s_bytes_received; }
const char* dns_server_get_version(void) { return FAROLDNS_VERSION_STR; }