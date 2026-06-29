#ifndef DNS_SERVER_H
#define DNS_SERVER_H

#include "esp_err.h"
#include <stdint.h>

#ifndef GIT_COMMIT_COUNT
#define GIT_COMMIT_COUNT 0
#endif
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
#define FAROLDNS_VERSION "0.2." STR(GIT_COMMIT_COUNT)
#define FAROLDNS_VERSION_STR "FarolDNS v" FAROLDNS_VERSION " (" __DATE__ " " __TIME__ ")"

esp_err_t dns_server_start(void);
void dns_server_stop(void);
uint32_t dns_server_get_query_count(void);
uint32_t dns_server_get_bytes_sent(void);
uint32_t dns_server_get_bytes_received(void);
const char* dns_server_get_version(void);

#endif // DNS_SERVER_H
