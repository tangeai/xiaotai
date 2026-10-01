#ifndef XIAOTAI_CONTACTS_H
#define XIAOTAI_CONTACTS_H

#include <stdbool.h>
#include <stddef.h>

#define XIAOTAI_CONTACTS_MAX 16U
#define XIAOTAI_CONTACT_ID_MAX 64U
#define XIAOTAI_CONTACT_NAME_MAX 48U
#define XIAOTAI_CONTACT_MODEL_ID_MAX 64U
#define XIAOTAI_CONTACT_APP_ID_MAX 64U

typedef enum {
    XIAOTAI_CONTACT_DEVICE = 1,
    XIAOTAI_CONTACT_VOIP,
} xiaotai_contact_type_t;

typedef struct {
    xiaotai_contact_type_t type;
    bool online;
    char id[XIAOTAI_CONTACT_ID_MAX + 1U];
    char name[XIAOTAI_CONTACT_NAME_MAX + 1U];
    char model_id[XIAOTAI_CONTACT_MODEL_ID_MAX + 1U];
    char app_id[XIAOTAI_CONTACT_APP_ID_MAX + 1U];
} xiaotai_contact_t;

typedef struct {
    xiaotai_contact_t entries[XIAOTAI_CONTACTS_MAX];
    size_t count;
} xiaotai_contacts_t;

typedef enum {
    XIAOTAI_CONTACT_MATCH_OK = 0,
    XIAOTAI_CONTACT_MATCH_NOT_FOUND = -1,
    XIAOTAI_CONTACT_MATCH_AMBIGUOUS = -2,
    XIAOTAI_CONTACT_MATCH_INVALID = -3,
} xiaotai_contact_match_t;

void xiaotai_contacts_init(xiaotai_contacts_t *contacts);

/* Replace the complete, platform-ordered contact snapshot returned by
 * /v1/call/device/contacts. Entries with type "voip" are WeChat contacts;
 * every other typed entry is a device contact, matching the ESP32 runtime. */
int xiaotai_contacts_replace_all_json(xiaotai_contacts_t *contacts,
                                      const char *json);

/* Each refresh replaces only its own channel and preserves the other channel. */
int xiaotai_contacts_replace_device_json(xiaotai_contacts_t *contacts,
                                         const char *json);
int xiaotai_contacts_replace_voip_json(xiaotai_contacts_t *contacts,
                                       const char *json);

/* Name matching removes ASCII whitespace and folds ASCII letters. The
 * optional id and channel only validate/filter the unique name match. */
xiaotai_contact_match_t xiaotai_contacts_match(
    const xiaotai_contacts_t *contacts, const char *name,
    const char *target_id, const char *channel, xiaotai_contact_t *result);

/* Quick call is deliberately policy-free: the platform-ordered first entry is
 * returned unchanged. The device must not sort or prefer a channel. */
xiaotai_contact_match_t xiaotai_contacts_first(
    const xiaotai_contacts_t *contacts, xiaotai_contact_t *result);

/* Return the first contact of a requested channel while preserving platform
 * order within that channel. */
xiaotai_contact_match_t xiaotai_contacts_first_of_type(
    const xiaotai_contacts_t *contacts, xiaotai_contact_type_t type,
    xiaotai_contact_t *result);

/* Build a presentation-only order with device contacts first and WeChat
 * contacts second. The snapshot remains unchanged, so contacts[0] keeps its
 * platform-defined quick-call meaning. */
size_t xiaotai_contacts_grouped_indices(const xiaotai_contacts_t *contacts,
                                        size_t *indices, size_t capacity);

/* Decode the AI call_intent envelope and resolve it against the same bounded
 * contact snapshot used by local interaction adapters. Only audio calls are
 * accepted; target_id and channel narrow rather than override name matching. */
xiaotai_contact_match_t xiaotai_contacts_resolve_call_intent_json(
    const xiaotai_contacts_t *contacts, const char *json,
    xiaotai_contact_t *result);

#endif
