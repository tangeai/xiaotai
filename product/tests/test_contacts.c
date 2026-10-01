#include <assert.h>
#include <string.h>

#include "xiaotai_contacts.h"

int main(void)
{
    xiaotai_contacts_t contacts;
    xiaotai_contact_t match;
    size_t grouped[XIAOTAI_CONTACTS_MAX] = {0};
    xiaotai_contacts_init(&contacts);

    /* The device contacts endpoint is a combined snapshot. This is the shape
     * consumed by the P4 runtime: non-voip rows are devices, voip rows carry
     * the WeChat dial metadata, and missing remarks fall back to a useful
     * device name before finally falling back to the device ID. */
    assert(xiaotai_contacts_replace_all_json(&contacts,
        "{\"code\":0,\"data\":{\"contacts\":["
        "{\"type\":\"tirtc\",\"device_id\":\"dev-0\","
        "\"device_name\":\"客厅设备\",\"online\":true},"
        "{\"type\":\"voip\",\"device_id\":\"wx-device-0\","
        "\"remark\":\"爸爸\",\"wx_model_id\":\"model-0\","
        "\"wx_app_id\":\"app-0\"}]}}") == 2);
    assert(contacts.count == 2U);
    assert(contacts.entries[0].type == XIAOTAI_CONTACT_DEVICE);
    assert(strcmp(contacts.entries[0].name, "客厅设备") == 0);
    assert(contacts.entries[1].type == XIAOTAI_CONTACT_VOIP);
    assert(strcmp(contacts.entries[1].name, "爸爸") == 0);
    assert(strcmp(contacts.entries[1].model_id, "model-0") == 0);
    assert(strcmp(contacts.entries[1].app_id, "app-0") == 0);

    xiaotai_contacts_init(&contacts);
    assert(xiaotai_contacts_replace_device_json(&contacts,
        "{\"code\":0,\"data\":{\"contacts\":["
        "{\"type\":\"device\",\"device_id\":\"dev-1\","
        "\"remark\":\"Front Desk\",\"online\":true}]}}") == 1);
    assert(xiaotai_contacts_replace_voip_json(&contacts,
        "{\"code\":200,\"data\":{\"list\":["
        "{\"wxa_user_openid\":\"wx-1\",\"wx_model_id\":\"model-1\","
        "\"wx_app_id\":\"app-1\",\"nickname\":\"Alice\"}]}}") == 1);
    assert(contacts.count == 2U);

    /* Contact pages group device and WeChat entries without mutating the
     * platform order used by contacts[0]. */
    assert(xiaotai_contacts_grouped_indices(&contacts, grouped,
                                            XIAOTAI_CONTACTS_MAX) == 2U);
    assert(grouped[0] == 0U);
    assert(grouped[1] == 1U);

    assert(xiaotai_contacts_match(&contacts, " front\tdesk ", NULL,
                                  "device", &match) ==
           XIAOTAI_CONTACT_MATCH_OK);
    assert(match.type == XIAOTAI_CONTACT_DEVICE);
    assert(strcmp(match.id, "dev-1") == 0);
    assert(xiaotai_contacts_match(&contacts, "ALICE", "wx-1", "wechat",
                                  &match) == XIAOTAI_CONTACT_MATCH_OK);
    assert(match.type == XIAOTAI_CONTACT_VOIP);
    assert(xiaotai_contacts_first(&contacts, &match) ==
           XIAOTAI_CONTACT_MATCH_OK);
    assert(strcmp(match.id, "dev-1") == 0);
    assert(xiaotai_contacts_first_of_type(&contacts, XIAOTAI_CONTACT_VOIP,
                                          &match) ==
           XIAOTAI_CONTACT_MATCH_OK);
    assert(strcmp(match.id, "wx-1") == 0);
    assert(xiaotai_contacts_resolve_call_intent_json(&contacts,
        "{\"method\":\"call_intent\",\"params\":{"
        "\"target_name\":\"alice\",\"target_id\":\"wx-1\","
        "\"channel\":\"wechat\",\"media\":\"audio\"}}", &match) ==
        XIAOTAI_CONTACT_MATCH_OK);
    assert(strcmp(match.id, "wx-1") == 0);
    assert(xiaotai_contacts_resolve_call_intent_json(&contacts,
        "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"device_action\","
        "\"params\":{\"name\":\"call_contact\",\"arguments\":{"
        "\"contact_name\":\"front desk\",\"call_type\":\"audio\","
        "\"contact_type\":\"device\"}}}", &match) ==
        XIAOTAI_CONTACT_MATCH_OK);
    assert(strcmp(match.id, "dev-1") == 0);
    assert(xiaotai_contacts_resolve_call_intent_json(&contacts,
        "{\"method\":\"device_action\",\"params\":{"
        "\"action\":\"call_wechat\",\"target\":\"Alice\","
        "\"call_type\":\"voice\"}}", &match) ==
        XIAOTAI_CONTACT_MATCH_OK);
    assert(strcmp(match.id, "wx-1") == 0);
    assert(xiaotai_contacts_resolve_call_intent_json(&contacts,
        "{\"method\":\"device_action\",\"params\":{"
        "\"name\":\"CALL_CONTACT\",\"data\":{"
        "\"name\":\"front desk\",\"contact_type\":\"设备联系人\","
        "\"call_type\":\"AUDIO\"}}}", &match) ==
        XIAOTAI_CONTACT_MATCH_OK);
    assert(strcmp(match.id, "dev-1") == 0);
    assert(xiaotai_contacts_resolve_call_intent_json(&contacts,
        "{\"method\":\"device_action\",\"params\":{"
        "\"name\":\"CALL_CONTACT\",\"data\":{"
        "\"name\":\"Alice\",\"contact_type\":\"微信联系人\"}}}",
        &match) == XIAOTAI_CONTACT_MATCH_OK);
    assert(strcmp(match.id, "wx-1") == 0);
    assert(xiaotai_contacts_resolve_call_intent_json(&contacts,
        "{\"method\":\"device_action\",\"params\":{"
        "\"action\":\"change_volume\",\"target\":\"Alice\"}}",
        &match) == XIAOTAI_CONTACT_MATCH_INVALID);
    assert(xiaotai_contacts_resolve_call_intent_json(&contacts,
        "{\"method\":\"device_action\",\"params\":{"
        "\"action\":\"call_contact\",\"target\":\"Alice\","
        "\"call_type\":\"video\"}}", &match) ==
        XIAOTAI_CONTACT_MATCH_INVALID);
    assert(xiaotai_contacts_resolve_call_intent_json(&contacts,
        "{\"params\":{\"target\":\"Alice\",\"media\":\"video\"}}",
        &match) == XIAOTAI_CONTACT_MATCH_INVALID);

    /* Replacing one channel must preserve the other channel. */
    assert(xiaotai_contacts_replace_device_json(&contacts,
        "{\"code\":0,\"data\":{\"contacts\":[]}}") == 0);
    assert(contacts.count == 1U);
    assert(contacts.entries[0].type == XIAOTAI_CONTACT_VOIP);
    assert(xiaotai_contacts_replace_voip_json(&contacts,
        "{\"code\":0,\"data\":{\"contacts\":["
        "{\"wx_open_id\":\"wx-2\",\"wxa_model_id\":\"model-2\","
        "\"alias\":\"Same\"},{\"wx_open_id\":\"wx-3\","
        "\"wxa_model_id\":\"model-3\",\"alias\":\"same\"}]}}") == 2);
    assert(xiaotai_contacts_match(&contacts, "same", NULL, NULL, &match) ==
           XIAOTAI_CONTACT_MATCH_AMBIGUOUS);

    assert(xiaotai_contacts_replace_device_json(&contacts,
        "{\"code\":0,\"data\":{\"contacts\":["
        "{\"type\":\"device\",\"device_id\":\"dev-2\","
        "\"remark\":\"Workshop\",\"online\":false}]}}") == 1);
    assert(contacts.entries[0].type == XIAOTAI_CONTACT_VOIP);
    assert(xiaotai_contacts_grouped_indices(&contacts, grouped, 2U) == 2U);
    assert(contacts.entries[grouped[0]].type == XIAOTAI_CONTACT_DEVICE);
    assert(contacts.entries[grouped[1]].type == XIAOTAI_CONTACT_VOIP);
    assert(xiaotai_contacts_first(&contacts, &match) ==
           XIAOTAI_CONTACT_MATCH_OK);
    assert(match.type == XIAOTAI_CONTACT_VOIP);
    assert(xiaotai_contacts_replace_device_json(&contacts, "{}") == -1);

    xiaotai_contacts_init(&contacts);
    assert(xiaotai_contacts_replace_device_json(&contacts,
        "{\"code\":0,\"data\":{\"contacts\":["
        "{\"type\":\"device\",\"device_id\":\"dev-only\","
        "\"remark\":\"Only Device\",\"online\":true}]}}") == 1);
    assert(xiaotai_contacts_first_of_type(&contacts, XIAOTAI_CONTACT_VOIP,
                                          &match) ==
           XIAOTAI_CONTACT_MATCH_NOT_FOUND);
    return 0;
}
