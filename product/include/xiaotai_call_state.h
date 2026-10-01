#ifndef XIAOTAI_CALL_STATE_H
#define XIAOTAI_CALL_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define XIAOTAI_CALL_ROOM_ID_SIZE 128U
#define XIAOTAI_CALL_PEER_ID_SIZE 65U
#define XIAOTAI_CALL_TYPE_SIZE 16U
#define XIAOTAI_VOIP_DESCRIPTOR_SIZE 1024U
#define XIAOTAI_VOIP_TOKEN_SIZE 1024U
#define XIAOTAI_VOIP_OPENID_SIZE 129U

typedef struct {
    char room_id[XIAOTAI_CALL_ROOM_ID_SIZE];
    char peer_id[XIAOTAI_CALL_PEER_ID_SIZE];
    char type[XIAOTAI_CALL_TYPE_SIZE];
    bool outbound;
} xiaotai_device_call_state_t;

typedef struct {
    char peer_id[XIAOTAI_VOIP_DESCRIPTOR_SIZE];
    char token[XIAOTAI_VOIP_TOKEN_SIZE];
    char room_id[XIAOTAI_CALL_ROOM_ID_SIZE];
    char call_id[XIAOTAI_CALL_ROOM_ID_SIZE];
    char outbound_openid[XIAOTAI_VOIP_OPENID_SIZE];
    bool outbound;
} xiaotai_voip_call_state_t;

typedef struct {
    xiaotai_device_call_state_t device;
    xiaotai_voip_call_state_t voip;
    char stale_voip_call_id[XIAOTAI_CALL_ROOM_ID_SIZE];
    uint32_t stale_voip_deadline_ms;
} xiaotai_call_state_t;

typedef enum {
    XIAOTAI_VOIP_OFFER_INVALID = 0,
    XIAOTAI_VOIP_OFFER_INCOMING,
    XIAOTAI_VOIP_OFFER_OUTBOUND_MATCH,
    XIAOTAI_VOIP_OFFER_OUTBOUND_MISMATCH,
    XIAOTAI_VOIP_OFFER_STALE,
} xiaotai_voip_offer_t;

void xiaotai_call_state_init(xiaotai_call_state_t *state);
void xiaotai_call_state_clear_device(xiaotai_call_state_t *state);
void xiaotai_call_state_clear_voip(xiaotai_call_state_t *state);
bool xiaotai_call_state_begin_device_outbound(xiaotai_call_state_t *state,
                                              const char *peer_id);
bool xiaotai_call_state_begin_voip_outbound(xiaotai_call_state_t *state,
                                            const char *openid);
bool xiaotai_call_state_set_device_incoming(xiaotai_call_state_t *state,
                                            const char *room_id,
                                            const char *caller_id,
                                            const char *call_type);
bool xiaotai_call_state_set_device_room(xiaotai_call_state_t *state,
                                        const char *room_id);
bool xiaotai_call_state_set_voip_call_id(xiaotai_call_state_t *state,
                                         const char *call_id);
void xiaotai_call_state_mark_voip_stale(xiaotai_call_state_t *state,
                                        const char *call_id,
                                        uint32_t deadline_ms);
xiaotai_voip_offer_t xiaotai_call_state_classify_voip(
    const xiaotai_call_state_t *state, bool outbound_pending,
    uint32_t now_ms, const char *device_id, const char *peer_id,
    const char *token, const char *room_id, const char *call_id,
    const char *openid, const char *from);
bool xiaotai_call_state_accept_voip(xiaotai_call_state_t *state,
                                    const char *peer_id, const char *token,
                                    const char *room_id, const char *call_id,
                                    bool complete_outbound);
bool xiaotai_call_state_matches_device(const xiaotai_call_state_t *state,
                                       const char *room_id);
bool xiaotai_call_state_matches_voip(const xiaotai_call_state_t *state,
                                     const char *room_id);

#endif
