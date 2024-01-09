#pragma once

void https_request_init(void);
esp_err_t https_request_get_symbol_quote(char *symbol, float *quote);