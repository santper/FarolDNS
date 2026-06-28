#ifndef MDNS_MANAGER_H
#define MDNS_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>

esp_err_t mdns_manager_start(void);
void mdns_manager_stop(void);
bool mdns_manager_is_probe_done(void);
const char* mdns_manager_get_hostname(void);

#endif // MDNS_MANAGER_H