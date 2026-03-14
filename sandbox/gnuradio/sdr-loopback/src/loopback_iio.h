#ifndef LOOPBACK_IIO_H
#define LOOPBACK_IIO_H

#include <string>
#include <iio.h>
#include "pretty_log.h"

using namespace pretty;

// Helper: restore loopback attribute via a fresh IIO context.
// Called after run_loopback() returns so no context conflicts with GNU Radio.
inline void restore_loopback(const std::string& uri, const std::string& prev_loopback) {
    log_info() << "Restoring loopback to '" << prev_loopback << "'...\n";
    struct iio_context* ctx = iio_create_context_from_uri(uri.c_str());
    if (!ctx) {
        log_warning() << "could not connect to IIO for loopback restore\n";
        return;
    }
    struct iio_device* phy = iio_context_find_device(ctx, "ad9361-phy");
    if (!phy) {
        log_warning() << "could not find ad9361-phy for loopback restore\n";
        iio_context_destroy(ctx);
        return;
    }
    int ret = iio_device_debug_attr_write(phy, "loopback", prev_loopback.c_str());
    if (ret < 0) {
        log_warning() << "restore failed (ret=" << ret << "), trying '0'\n";
        iio_device_debug_attr_write(phy, "loopback", "0");
    }
    iio_context_destroy(ctx);
}

#endif // LOOPBACK_IIO_H
