"""Exercise the runtime status readers selected by production UI/diagnostics."""
import re
import unittest
from test_s3_product_regressions import ROOT, function, run_c

RUNTIME = ROOT / "platforms/esp-idf/components/starter_runtime"
PRODUCT = ROOT / "platforms/esp-idf/components/starter_product/src/starter_product.c"
CONSOLE = ROOT / "platforms/esp-idf/components/starter_console/src/starter_console.c"

class UiStatusBudget(unittest.TestCase):
    def test_ui_reads_state_without_stack_scan_and_diagnostics_still_probe(self):
        source = (RUNTIME / "src/starter_runtime.c").read_text()
        ui = PRODUCT.read_text()
        def reader(body):
            return re.search(r"starter_runtime_status_t\s+\w+\s*=\s*(\w+)\(\)", body).group(1)
        tick_reader = reader(function(ui, "product_tick"))
        tap_reader = reader(function(ui, "on_home_tap"))
        console_reader = reader(function(CONSOLE.read_text(), "command_status"))
        diagnostic_reader = reader(function(ui, "refresh_diagnostics"))
        functions = ""
        if "starter_runtime_status_snapshot(void)" in source:
            functions += function(source, "starter_runtime_status_snapshot")
        functions += function(source, "starter_runtime_status")
        run_c(r'''
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stddef.h>
typedef int starter_runtime_state_t;
typedef struct {
 int state; uint32_t session_generation,connection_generation;
 int last_error; uint32_t stack_high_water_bytes;
} starter_runtime_status_t;
static atomic_int s_public_state=3,s_last_error;
static atomic_uint s_public_session_generation=7,s_public_connection_generation=7;
static void *s_task=(void *)1;
static unsigned stack_scans;
static uint32_t uxTaskGetStackHighWaterMark(void *task){
 assert(task==s_task);stack_scans++;return 11612;
}
''' + functions + r'''
int main(void){
 for(unsigned i=0;i<100;i++){
  starter_runtime_status_t state=TICK_READER();
  assert(state.state==3 && state.session_generation==7 && state.connection_generation==7 && state.last_error==0);
 }
 (void)TAP_READER();
 assert(stack_scans==0); /* ordinary UI must not walk the task stack */
 s_public_state=0;s_public_session_generation=8;s_public_connection_generation=0;s_last_error=262;
 starter_runtime_status_t state=TICK_READER();
 assert(state.state==0 && state.session_generation==8 && state.connection_generation==0 && state.last_error==262);
 assert(stack_scans==0);
 state=CONSOLE_READER();assert(state.stack_high_water_bytes==11612 && stack_scans==1);
 state=DIAGNOSTIC_READER();assert(state.stack_high_water_bytes==11612 && stack_scans==2);
 s_task=NULL;state=CONSOLE_READER();assert(state.stack_high_water_bytes==0 && stack_scans==2);
}
'''.replace("TICK_READER",tick_reader).replace("TAP_READER",tap_reader)
   .replace("CONSOLE_READER",console_reader).replace("DIAGNOSTIC_READER",diagnostic_reader))

if __name__ == "__main__":
    unittest.main()
