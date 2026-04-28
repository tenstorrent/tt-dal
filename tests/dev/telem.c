// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include "common.h"
#include <assert.h>
#include <ttdal.h>

// MARK: hardware
int main(void) {
    tt_device_t devs[1];
    tt_device_t *dev = open_test_device(devs);
    if (!dev)
        return EXIT_FAILURE;

    tt_telemetry_t table;
    assert(tt_telemetry(dev, table) == 0);
    assert(table[TT_TAG_TIMER_HEARTBEAT] != 0);
    assert(table[TT_TAG_AICLK] > 0);

    tt_dev_close(dev);
}
