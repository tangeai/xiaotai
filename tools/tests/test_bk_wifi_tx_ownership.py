"""Exercise the SDK netif TX boundary with deferred completion and reused pbufs."""

from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SDK = Path(os.environ.get('SDK_DIR', ROOT / '.tools/bk_avdk_smp-release-v3.1.1.10'))
SOURCE = SDK / 'ap/components/lwip_intf_v2_1/lwip-2.1.2/port/wlanif.c'


def extract_function(source, signature):
    begin = source.index(signature)
    end = source.index('{', begin) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[begin:end]


class BKWifiTxOwnership(unittest.TestCase):
    def test_command_confirmation_producer_and_consumer_share_mutex(self):
        tx_source = SDK / 'ap/components/bk_wifi_driver/wdrv_tx.c'
        rx_source = SDK / 'ap/components/bk_wifi_driver/wdrv_rx.c'
        if not tx_source.is_file():
            self.skipTest('BK SDK checkout is required')
        tx = extract_function(tx_source.read_text(), 'int wdrv_tx_msg(')
        rx = extract_function(rx_source.read_text(), 'void wdrv_rx_confirm_tx_msg(')
        harness = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#define BK_OK 0
#define BK_ASSERT(x) assert(x)
#define BK_LOGD(...) ((void)0)
#define WDRV_LOGV(...) ((void)0)
#define WDRV_LOGD(...) ((void)0)
#define WDRV_LOGE(...) ((void)0)
#define WDRV_LOGW(...) ((void)0)
#define MAX_CMD_BUF_PAYLOAD 256
#define WDRV_CMD_WAITCFM 1
#define WDRV_CMD_NOWAITCFM 0
#define WDRV_CMD_CFM_OFFSET 100
#define WDRV_CMDCFM_TIMEOUT 1000
#define WDRV_ENTER_TXMSG_CRITICAL(level) ((level)=0)
#define WDRV_EXIT_TXMSG_CRITICAL(level) ((void)(level))
struct co_list_hdr { struct co_list_hdr *next; };
struct co_list { struct co_list_hdr *first, *last; };
typedef struct { uint16_t cmd_id, cmd_sn; } wdrv_cmd_hdr;
typedef struct { struct co_list_hdr list; int sema; uint8_t *cfm_buf;
    unsigned cfm_id, waitcfm, cfm_sn, cfm_len; } wdrv_cmd_cfm;
typedef struct { unsigned id, cfm_sn, param_len; uint8_t param[4]; } wdrv_rx_msg;
static struct { int cfm_lock; struct co_list cfm_pending_list; } wdrv_host_env;
static int timeout, init_failure, signals;
static void rtos_lock_mutex(int *lock) { assert(!*lock); *lock=1; }
static void rtos_unlock_mutex(int *lock) { assert(*lock); *lock=0; }
static void co_list_push_back(struct co_list *list, struct co_list_hdr *node) {
    assert(wdrv_host_env.cfm_lock == 1);
    node->next=0; list->first=list->last=node;
}
static void co_list_extract(struct co_list *list, struct co_list_hdr *node) {
    assert(wdrv_host_env.cfm_lock == 1 && list->first == node);
    list->first=list->last=0;
}
static int rtos_init_semaphore(int *sema, unsigned count) {
    (void)count; *sema=0; return init_failure ? -7 : 0;
}
static int rtos_set_semaphore(int *sema) { *sema=1; signals++; return 0; }
static int rtos_get_semaphore(int *sema, unsigned wait) {
    (void)wait; assert(wdrv_host_env.cfm_lock == 0);
    return timeout ? -1 : !*sema;
}
static void rtos_deinit_semaphore(int *sema) { *sema=0; }
static void wdrv_print_debug_info(void) {}
static void wdrv_cntrl_get_cif_stats(void) {}
static int wdrv_tx_msg_send(uint8_t *, uint16_t, uint8_t);
''' + rx + '\n' + tx + r'''
static int wdrv_tx_msg_send(uint8_t *data, uint16_t len, uint8_t wait) {
    (void)len; assert(wdrv_host_env.cfm_lock == 0);
    if (wait && !timeout) {
        wdrv_cmd_hdr *hdr=(wdrv_cmd_hdr *)data;
        wdrv_rx_msg response={.id=hdr->cmd_id+WDRV_CMD_CFM_OFFSET,
                             .cfm_sn=hdr->cmd_sn, .param_len=1, .param={42}};
        wdrv_rx_confirm_tx_msg(&response);
    }
    return 0;
}
int main(void) {
    wdrv_cmd_hdr command={.cmd_id=1}; uint8_t result=0;
    wdrv_cmd_cfm confirmation={.waitcfm=WDRV_CMD_WAITCFM};
    assert(wdrv_tx_msg((uint8_t *)&command,sizeof(command),&confirmation,&result) == 1);
    assert(result == 42 && signals == 1 && !wdrv_host_env.cfm_pending_list.first);
    timeout=1;
    assert(wdrv_tx_msg((uint8_t *)&command,sizeof(command),&confirmation,&result) == -3);
    assert(!wdrv_host_env.cfm_pending_list.first && !wdrv_host_env.cfm_lock);
    init_failure=1;
    assert(wdrv_tx_msg((uint8_t *)&command,sizeof(command),&confirmation,&result) == -7);
    assert(!wdrv_host_env.cfm_pending_list.first && !wdrv_host_env.cfm_lock);
}
'''
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp)
            source_file = temp / 'command.c'
            source_file.write_text(harness)
            executable = temp / 'command'
            subprocess.run(['cc', '-std=c11', str(source_file), '-o', str(executable)],
                           check=True, capture_output=True)
            result = subprocess.run([str(executable)], text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_netif_tx_owns_each_deferred_descriptor_and_balances_failures(self):
        if not SOURCE.is_file():
            self.skipTest('BK SDK checkout is required')
        source = SOURCE.read_text()
        begin = source.index('static err_t low_level_output(struct netif *netif, struct pbuf *p)')
        end = source.index('{', begin)
        depth = 1
        end += 1
        while depth:
            depth += (source[end] == '{') - (source[end] == '}')
            end += 1
        function = source[begin:end]
        harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define CONFIG_P2P 0
#define BK_LOGD(...) ((void)0)
#define ERR_OK 0
#define ERR_MEM -1
#define ERR_TIMEOUT -3
#define ERR_ARG -4
#define BK_ERR_NO_MEM -5
#define PBUF_RAW_TX 32
#define PBUF_RAM 0
typedef int err_t;
struct netif { int unused; } sta, sap, unknown;
struct pbuf { struct pbuf *next; void *payload; unsigned tot_len, len, ref; };
typedef struct cpdu_t { struct cpdu_t *next; struct {
    unsigned vif_idx, need_free, special_type;
} co_hdr; } cpdu_t;
struct packet { struct pbuf p; cpdu_t descriptor; unsigned char payload[32]; };
static struct pbuf *queued[2];
static unsigned queued_count, allocations, frees;
static int clone_fail, send_result;
static struct netif *net_get_sta_handle(void) { return &sta; }
static struct netif *net_get_uap_handle(void) { return &sap; }
static struct pbuf *pbuf_clone(int layer, int type, struct pbuf *p) {
    assert(layer == PBUF_RAW_TX && type == PBUF_RAM);
    if (clone_fail) return NULL;
    struct packet *copy = malloc(sizeof(*copy)); assert(copy);
    memset(copy, 0xa5, sizeof(*copy));
    copy->p = (struct pbuf){.payload=copy->payload, .tot_len=p->tot_len,
                          .len=p->len, .ref=1};
    memcpy(copy->payload, p->payload, p->len);
    allocations++;
    return &copy->p;
}
static void pbuf_free(struct pbuf *p) {
    assert(p->ref > 0);
    if (--p->ref == 0) { frees++; free(p); }
}
static int wdrv_txdata_sender(struct pbuf *p, uint32_t vif) {
    assert(vif == 0);
    if (send_result == BK_ERR_NO_MEM) return send_result;
    p->ref++;
    if (send_result) { pbuf_free(p); return send_result; }
    assert(queued_count < 2); queued[queued_count++] = p;
    return 0;
}
''' + function + r'''
int main(void) {
    struct packet source = {0};
    source.p = (struct pbuf){.payload=source.payload, .len=32, .tot_len=32, .ref=1};
    memset(source.payload, 0x5a, sizeof(source.payload));
    memset(&source.descriptor, 0x3c, sizeof(source.descriptor));
    cpdu_t original_descriptor = source.descriptor;
    assert(low_level_output(&sta, &source.p) == ERR_OK);
    assert(low_level_output(&sta, &source.p) == ERR_OK);
    assert(queued[0] != queued[1] && queued[0] != &source.p && queued[1] != &source.p);
    assert(source.p.ref == 1);
    assert(memcmp(&source.descriptor, &original_descriptor, sizeof(cpdu_t)) == 0);
    for (unsigned i=0; i<2; i++) {
        cpdu_t *descriptor = (cpdu_t *)(queued[i]+1);
        assert(descriptor->co_hdr.need_free == 0 && descriptor->co_hdr.special_type == 0);
        assert(queued[i]->ref == 1 && memcmp(queued[i]->payload, source.payload, 32) == 0);
    }
    ((cpdu_t *)(queued[0]+1))->co_hdr.need_free = 1;
    assert(((cpdu_t *)(queued[1]+1))->co_hdr.need_free == 0);
    pbuf_free(queued[0]); pbuf_free(queued[1]);
    clone_fail=1; assert(low_level_output(&sta,&source.p) == ERR_MEM); clone_fail=0;
    send_result=BK_ERR_NO_MEM; assert(low_level_output(&sta,&source.p) == ERR_MEM);
    send_result=-99; assert(low_level_output(&sta,&source.p) == ERR_TIMEOUT);
    assert(low_level_output(&unknown,&source.p) == ERR_ARG);
    assert(source.p.ref == 1 && allocations == frees);
    puts("PASS: deferred TX descriptors are independent; source and failure ownership preserved");
}
'''
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp)
            c_file = temp / 'tx.c'
            c_file.write_text(harness)
            executable = temp / 'tx'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', str(c_file),
                            '-o', str(executable)], check=True, capture_output=True)
            result = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
