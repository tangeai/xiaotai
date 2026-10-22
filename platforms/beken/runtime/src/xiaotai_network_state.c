#include "xiaotai_network_state.h"

bool xiaotai_network_link_changed(bool was_ready, bool now_ready)
{
    return was_ready != now_ready;
}
