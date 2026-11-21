import unittest
from unittest.mock import patch
from pathlib import Path
import sys
sys.path.insert(0,'tools/tests')
import test_configuration_transactions as t
from test_s3_product_regressions import function
console=Path('platforms/esp-idf/components/starter_console/src/starter_console.c').read_text()
original=t.run_c
def probe(code,*args,**kw):
 code=code[:code.index('int main(void){')]+r'''
#define pdMS_TO_TICKS(x) (x)
static int restarted;
static const char *esp_err_to_name(int err){return "error";}
static void esp_restart(void){restarted++;}
static void vTaskDelay(unsigned ms){
 /* A portal request whose body arrived before the serial command reaches
    its actual public storage entry point during the serial reboot delay. */
 config_task=20;
 assert(wifi_manager_save_credentials("portal-ssid","portal-password")==ESP_ERR_INVALID_STATE);
 config_task=10;
}
'''+function(console,'command_wifi_clear')+function(console,'command_wifi_set')+r'''
int main(void){
 char *clear[]={"wifi-clear"};wifi_manager_credentials_t out;
 assert(command_wifi_clear(1,clear)==0 && restarted==1);
 assert(wifi_manager_load_credentials(&out)!=0);
 char *set[]={"wifi-set","serial-ssid","serial-password"};
 assert(command_wifi_set(3,set)==0 && restarted==2);
 assert(wifi_manager_load_credentials(&out)==0);
 printf("ESP wifi-set intended serial-ssid; reboot SSID=%s\n",out.ssid);
 assert(!strcmp(out.ssid,"serial-ssid"));
}
'''
 original(code,*args,**kw)

class WifiRecovery(unittest.TestCase):
 def test_manual_recovery_excludes_delayed_portal(self):
  with patch.object(t,'run_c',probe):
   t.ConfigurationTransactions().test_wifi_record_survives_each_write_failure_and_rejects_corruption()

if __name__=='__main__':unittest.main()
