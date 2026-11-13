// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QThreadPool>

#include <atomic>
#include <cstdlib>

#ifdef __GLIBC__
#include <malloc.h>
#endif

/// Hands the freed heap back to the system, from a worker thread. Reading the pacman
/// databases or an AppStream catalogue takes a hundred megabytes for a moment, and glibc
/// keeps the pages otherwise. Requests made while one is waiting to run are folded into it.
inline void releaseFreedMemory() {
#ifdef __GLIBC__
    static std::atomic_flag queued;
    if (!queued.test_and_set()) {
        QThreadPool::globalInstance()->start([] {
            queued.clear();
            malloc_trim(0);
        });
    }
#endif
}
