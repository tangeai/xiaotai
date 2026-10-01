#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "xiaotai_signal.h"

static unsigned visits;
static xiaotai_signal_type_t last_type;
static char retained[32];

static void visit(const xiaotai_signal_view_t *signal, void *context)
{
    (void)context;
    visits++;
    last_type = signal->type;
    snprintf(retained, sizeof(retained), "%s", signal->room_id);
}

int main(void)
{
    assert(!xiaotai_signal_decode("{}", visit, NULL));
    assert(visits == 0U);
    assert(xiaotai_signal_decode(
        "{\"type\":\"call_incoming\",\"channel\":\"device\","
        "\"payload\":{\"room_id\":\"room-1\",\"caller_id\":\"dev-1\"}}",
        visit, NULL));
    assert(visits == 1U && last_type == XIAOTAI_SIGNAL_DEVICE_CALL_INCOMING);
    assert(strcmp(retained, "room-1") == 0);
    assert(xiaotai_signal_decode(
        "{\"type\":\"room_assignment_changed\"}", visit, NULL));
    assert(last_type == XIAOTAI_SIGNAL_ROOM_ASSIGNMENT_CHANGED);
    assert(xiaotai_signal_decode("{\"type\":\"future_event\"}", visit,
                                 NULL));
    assert(last_type == XIAOTAI_SIGNAL_UNKNOWN);
    return 0;
}
