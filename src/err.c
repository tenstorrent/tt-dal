// SPDX-FileCopyrightText: © 2026 Tenstorrent Inc.

#include "ttdal.h"

// Thread-local errno for thread safety
_Thread_local tt_error_t tt_errno = TT_OK;
