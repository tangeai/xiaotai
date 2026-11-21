"""Actual configuration admission gate with a controllable task identity."""
from test_s3_product_regressions import ROOT

def config_store_fixture():
    source = (ROOT/'platforms/esp-idf/components/runtime_config/src/config_store.c').read_text()
    source = '\n'.join(line for line in source.splitlines() if not line.startswith('#include "'))
    return '''
#include <stdbool.h>
#include <stdint.h>
static uintptr_t config_task=10;
static void *xTaskGetCurrentTaskHandle(void){return (void*)config_task;}
''' + source
