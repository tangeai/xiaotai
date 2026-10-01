#include <assert.h>
#include <stdbool.h>

#include "xiaotai_network_state.h"

int main(void)
{
    assert(xiaotai_network_link_changed(false, true));
    assert(!xiaotai_network_link_changed(true, true));
    assert(xiaotai_network_link_changed(true, false));
    assert(!xiaotai_network_link_changed(false, false));
    return 0;
}
