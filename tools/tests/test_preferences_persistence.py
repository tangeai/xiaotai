from config_store_fixture import config_store_fixture
"""Replay real ESP preference saves with storage and task failures."""
import re
import unittest
from test_s3_product_regressions import ROOT, function, run_c

SOURCE = ROOT / 'platforms/esp-idf/components/starter_product/src/starter_product.c'


class PreferencesPersistence(unittest.TestCase):
    def replay(self, scenario):
        source = SOURCE.read_text()
        prefs = re.search(r'typedef struct \{[^{}]+\} product_preferences_t;', source).group()
        # The worker is reached through the production scheduling entry point.
        start = source.find('/* PREFERENCE_SAVE_STATE */')
        state = source[start:source.index('/* PREFERENCE_SAVE_STATE_END */', start)] if start >= 0 else ''
        run_c(config_store_fixture() + r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stddef.h>
#include <string.h>
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM -2
#define ESP_ERR_INVALID_STATE -3
#define ESP_ERR_INVALID_ARG -4
#define ESP_ERR_NVS_NOT_FOUND -5
#define NVS_READONLY 0
#define NVS_READWRITE 1
#define PRODUCT_NVS "product"
#define pdPASS 1
#define ESP_LOGE(...) ((void)0)
typedef int esp_err_t;
typedef int nvs_handle_t;
''' + prefs + r'''
static product_preferences_t s_preferences = {.volume=7,.microphone_sensitivity=4,.sleep_index=1};
static const char *s_emoji_names[21];
static unsigned char stored[8]={1,7,0,0,4,0,1,0};
static atomic_bool s_factory_reset_requested;
static int writes, commits, closes, fail_at, open_fail, task_fail, tasks;
static unsigned volume_written;
static int reset_at;
static void (*worker)(void *);
static int nvs_open(const char *s,int mode,int *h){if(open_fail)return -1;*h=1;return 0;}
static int nvs_get_blob(int h,const char*k,void*v,size_t*n){assert(*n==sizeof(stored));memcpy(v,stored,sizeof(stored));return 0;}
static int nvs_get_u8(int h,const char*k,uint8_t*v){return ESP_ERR_NVS_NOT_FOUND;}
static int write_boundary(void){writes++;if(writes==reset_at)atomic_store(&s_factory_reset_requested,true);return writes==fail_at?-1:0;}
static int nvs_set_blob(int h,const char*k,const void*v,size_t n){int rc=write_boundary();if(rc)return rc;assert(n==sizeof(stored));memcpy(stored,v,n);volume_written=stored[1];return 0;}
static int nvs_erase_key(int h,const char*k){return write_boundary();}
static int nvs_commit(int h){commits++;return fail_at==10?-1:0;}
static int starter_media_set_speaker_volume(unsigned n){return 0;}
static int starter_media_set_speaker_muted(bool n){return 0;}
static void starter_media_set_microphone_muted(bool n){}
static int starter_media_set_microphone_sensitivity(unsigned n){return 0;}
static void nvs_close(int h){closes++;}
#define ESP_LOGW(...) ((void)0)
#define FACTORY_RESET_FAILED 2
#define pdMS_TO_TICKS(n) (n)
static atomic_int s_factory_reset_status;
static int cleared,restarted;
static bool endpoint_saved;
static bool ntp_saved;
static int ntp_clear_fail;
static int runtime_config_clear_ntp_server(void){cleared++;if(ntp_clear_fail)return ESP_FAIL;ntp_saved=false;return ESP_OK;}
static int endpoint_clear_fail;
static int runtime_config_recover_tirtc(const void *value){cleared++;return 0;}
static int runtime_config_clear_endpoint(void){cleared++;if(endpoint_clear_fail)return ESP_FAIL;endpoint_saved=false;return ESP_OK;}
static int wifi_manager_recover_credentials(const char *ssid,const char *password){cleared++;return 0;}
static int clear_product_preferences(void){cleared++;return 0;}
static void vTaskDelay(unsigned n){}
static void esp_restart(void){restarted++;config_store_reset_end(false); /* emulate a new boot for the next scenario */}
static void vTaskDelete(void *p){}
static int xTaskCreatePinnedToCore(void (*fn)(void*),const char *s,unsigned size,void *a,unsigned p,void *t,unsigned core){
 if(task_fail){return 0;} tasks++;worker=fn;return pdPASS;
}
''' + source[source.index('#define PRODUCT_PREFERENCES_KEY'):source.index('static void preferences_save_task')] + state + function(source, 'preferences_load') + function(source, 'preferences_save_task') + function(source, 'preferences_save') + function(source, 'preferences_poll') + function(source, 'factory_reset_task') + r'''
int main(void){
''' + scenario + r'''
}
''')

    def test_failed_write_reloads_a_complete_old_settings_record(self):
        self.replay(r'''
 for(int failure=1;failure<=9;failure++){
  memcpy(stored,(unsigned char[]){1,7,0,0,4,0,1,0},8);
  s_preferences.volume=9;s_preferences.speaker_muted=true;s_preferences.microphone_muted=true;
  writes=commits=closes=0;fail_at=failure;
  preferences_save();assert(worker);worker(NULL);
  assert(commits==0 && "failed NVS write must not commit a partial preference set");
  assert(writes==failure && closes==1);
  preferences_load();assert(s_preferences.volume==7 && !s_preferences.speaker_muted && !s_preferences.microphone_muted);
  assert(atomic_load(&s_preferences_save_status)==PREFERENCES_FAILED);
 }
 writes=commits=0;fail_at=0;open_fail=1;
 preferences_save();worker(NULL);assert(writes==0 && commits==0);
 assert(atomic_load(&s_preferences_save_status)==PREFERENCES_FAILED);
 open_fail=0;task_fail=1;int before=tasks;
 preferences_save();assert(tasks==before && atomic_load(&s_preferences_save_status)==PREFERENCES_FAILED);
 task_fail=0;fail_at=10;preferences_save();worker(NULL);
 assert(atomic_load(&s_preferences_save_status)==PREFERENCES_FAILED);
 fail_at=0;writes=0;s_preferences.volume=2;preferences_save();before=tasks;
 for(unsigned i=3;i<=10;i++){s_preferences.volume=i;preferences_save();}
 assert(tasks==before);worker(NULL);assert(volume_written==2);
 preferences_poll();assert(tasks==before+1);writes=0;worker(NULL);
 assert(volume_written==10 && atomic_load(&s_preferences_save_status)==PREFERENCES_SAVED);
 before=tasks;preferences_poll();assert(tasks==before);
 // Reset closes admission and suppresses any queued write.
 preferences_save();atomic_store(&s_factory_reset_requested,true);
 factory_reset_task(NULL);assert(cleared==0 && restarted==0 && atomic_load(&s_factory_reset_status)==FACTORY_RESET_FAILED);
 writes=commits=0;worker(NULL);preferences_poll();preferences_save();
 assert(writes==0 && commits==0 && tasks==before+1);
 factory_reset_task(NULL);assert(cleared==5 && restarted==1);
''')

    def test_factory_reset_clears_ep_and_retries_failed_clear(self):
        self.replay(r'''
 // A saved ep-set override must be gone before reset can reboot.
 endpoint_saved=true;ntp_saved=true;
 factory_reset_task(NULL);
 assert(!endpoint_saved && !ntp_saved && restarted==1 && cleared==5);
 // A storage failure must remain visible and allow a successful retry.
 endpoint_saved=true;endpoint_clear_fail=1;restarted=cleared=0;
 factory_reset_task(NULL);
 assert(endpoint_saved && restarted==0 && cleared==5);
 assert(atomic_load(&s_factory_reset_status)==FACTORY_RESET_FAILED);
 endpoint_clear_fail=0;
 factory_reset_task(NULL);
 assert(!endpoint_saved && !ntp_saved && restarted==1 && cleared==10);
 // Reset is also valid when no override is saved.
 factory_reset_task(NULL);
 assert(!endpoint_saved && restarted==2 && cleared==15);
 ntp_saved=true;ntp_clear_fail=1;
 factory_reset_task(NULL);
 assert(ntp_saved && restarted==2);
 ntp_clear_fail=0;factory_reset_task(NULL);
 assert(!ntp_saved && restarted==3);
''')

    def test_reset_during_each_write_then_cancel_preserves_pending_save(self):
        self.replay(r'''
 for(int boundary=1;boundary<=9;boundary++){
  writes=commits=closes=0;reset_at=boundary;
  s_preferences.volume=2;preferences_save();int before=tasks;
  s_preferences.volume=8;preferences_save();
  worker(NULL);
  assert(commits==0 && closes==1);
  assert(atomic_load(&s_preferences_save_status)==PREFERENCES_FAILED);
  preferences_poll();assert(tasks==before && s_preferences_dirty);
  // Cancel reset: the pending edit must survive the admission gate.
  atomic_store(&s_factory_reset_requested,false);reset_at=0;writes=0;
  preferences_poll();assert(tasks==before+1);
  worker(NULL);
  assert(volume_written==8 && commits==1 && closes==2);
  assert(atomic_load(&s_preferences_save_status)==PREFERENCES_SAVED);
  preferences_poll();assert(tasks==before+1);
 }
''')

    def test_thousand_save_failure_and_retry_cycles_keep_latest_value(self):
        self.replay(r'''
 for(unsigned cycle=0;cycle<1000;cycle++){
  writes=commits=closes=0;fail_at=(cycle%9)+1;
  open_fail=(cycle%11)==0;task_fail=0;
  unsigned first=cycle%11,latest=(cycle+7)%11;
  s_preferences.volume=first;preferences_save();int before=tasks;
  for(unsigned edit=0;edit<20;edit++){
   s_preferences.volume=(cycle+edit)%11;preferences_save();preferences_poll();
   assert(tasks==before);
  }
  s_preferences.volume=latest;preferences_save();worker(NULL);
  assert(closes==(open_fail?0:1));

  if(open_fail || fail_at<=9)assert(commits==0);
  // The coalesced update is saved even when the preceding snapshot failed.
  writes=commits=closes=0;fail_at=open_fail=0;
  preferences_poll();assert(tasks==before+1);worker(NULL);
  assert(volume_written==latest && commits==1 && closes==1);
  assert(!s_preferences_dirty);
  assert(atomic_load(&s_preferences_save_status)==PREFERENCES_SAVED);
  preferences_poll();assert(tasks==before+1);
 }
''')


if __name__ == '__main__':
    unittest.main()
