#pragma once
#include <stdbool.h>
#include <stdint.h>
bool carousel_settings_init(void);
unsigned carousel_settings_seconds(void);
bool carousel_settings_save(unsigned seconds);
int carousel_settings_result(void);
