#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cJSON.h"
#define INVENTORY_LIMIT 100
#define INVENTORY_RESPONSE_MAX 65536U
typedef struct { char id[37], name[121], quantity[16], unit[25], expires_on[11]; unsigned batches; } inventory_row_t;
bool inventory_model_init(void);
void inventory_model_identity(const char *device);
void inventory_model_connection(bool online, bool available);
void inventory_model_revision(char revision[65]);
bool inventory_model_accept(const cJSON *json);
bool inventory_model_meta(unsigned *count, bool *online, unsigned *serial);
bool inventory_model_row(unsigned index, inventory_row_t *row);
unsigned inventory_model_page(unsigned start, inventory_row_t *rows, unsigned capacity, unsigned *total);
bool inventory_model_find(const char *id, inventory_row_t *row);

unsigned inventory_model_batches(const char *id, inventory_row_t *rows, unsigned capacity);

unsigned inventory_model_group_batches(const char *name, const char *unit, inventory_row_t *rows, unsigned capacity);
