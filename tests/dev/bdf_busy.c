// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.
//
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <ttdal.h>
#include <unistd.h>

// MARK: hardware
int main(void) {
    tt_device_t dev;
    if (tt_dev_scan(1, &dev) <= 0)
        return EXIT_FAILURE;

    // Read the address while the device is free
    tt_session_t sess;
    assert(tt_open(&dev, &sess, TT_OPEN_NONBLOCK) == 0);
    tt_dev_info_t info;
    assert(tt_dev_info(&sess, &info) == 0);
    assert(tt_close(&sess) == 0);
    char addr[20];
    snprintf(
        addr,
        sizeof(addr),
        "%04x:%02x:%02x.%x",
        info.pci_domain,
        (info.bus_dev_fn >> 8) & 0xFF,
        (info.bus_dev_fn >> 3) & 0x1F,
        info.bus_dev_fn & 0x7
    );

    // Lookup must find the free device
    tt_device_t found;
    assert(tt_dev_from_bdf(addr, &found) == 0);
    assert(found.id == dev.id);

    // Hold the device exclusively
    tt_session_t excl;
    assert(tt_open(&dev, &excl, TT_OPEN_EXCL | TT_OPEN_NONBLOCK) == 0);

    // Lookup must fail fast while the device is held
    alarm(5); // a lookup that waits ends here instead
    assert(tt_dev_from_bdf(addr, &found) < 0);
    assert(errno == EAGAIN);
    alarm(0);

    assert(tt_close(&excl) == 0);
}
