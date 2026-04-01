/**
 * sdr.cpp - AD9361 SDR lifecycle management
 *
 * Higher-level AD9361 configuration and cleanup functions built on
 * pretty_iio.h (libiio attribute writes) and libad9361 (hardware FIR).
 */

#include "sdr.h"

#include <iio.h>
#include <ad9361.h>

#include "pretty_log.h"
#include "pretty_iio.h"

using namespace pretty;

bool ad9361_configure(const PipelineConfig& cfg) {
    struct iio_context* cfg_ctx = iio_create_context_from_uri(cfg.sdr_uri.c_str());
    if (!cfg_ctx) {
        log_error() << "Could not connect to IIO at " << cfg.sdr_uri << "\n";
        return false;
    }
    struct iio_device* phy = iio_context_find_device(cfg_ctx, "ad9361-phy");
    if (!phy) {
        log_error() << "No ad9361-phy device\n";
        iio_context_destroy(cfg_ctx);
        return false;
    }

    if (cfg.sdr_hw_fir_enable) {
        // Hardware FIR path: write LO/gain manually, then library sets rate/bandwidth/FIR
        struct iio_channel* rx_lo = iio_device_find_channel(phy, "altvoltage0", true);
        struct iio_channel* rx0 = iio_device_find_channel(phy, "voltage0", false);
        if (!rx_lo || !rx0) {
            log_error() << "Could not find RX LO or voltage0 channel\n";
            iio_context_destroy(cfg_ctx);
            return false;
        }

        int ret;
        ret = iio_channel_attr_write_longlong(rx_lo, "frequency", cfg.sdr_frequency);
        if (ret < 0) {
            log_error() << "Could not write RX LO frequency (ret=" << ret << ")\n";
            iio_context_destroy(cfg_ctx);
            return false;
        }
        // iio_channel_attr_write() returns ssize_t (unlike _longlong/_double which return int)
        ssize_t wret = iio_channel_attr_write(rx0, "gain_control_mode", "manual");
        if (wret < 0) {
            log_error() << "Could not write gain_control_mode (ret=" << wret << ")\n";
            iio_context_destroy(cfg_ctx);
            return false;
        }
        ret = iio_channel_attr_write_double(rx0, "hardwaregain", cfg.sdr_gain);
        if (ret < 0) {
            log_error() << "Could not write hardwaregain (ret=" << ret << ")\n";
            iio_context_destroy(cfg_ctx);
            return false;
        }

        log_info() << "AD9361 RX LO/gain written via libiio\n";
        log_info() << "Configuring AD9361 hardware FIR: rate=" << cfg.sdr_hw_fir_rate
                   << " Fpass=" << cfg.sdr_hw_fir_fpass
                   << " Fstop=" << cfg.sdr_hw_fir_fstop << "\n";

        ret = ad9361_set_bb_rate_custom_filter_manual(
            phy,
            (unsigned long)cfg.sdr_hw_fir_rate,
            (unsigned long)cfg.sdr_hw_fir_fpass,
            (unsigned long)cfg.sdr_hw_fir_fstop,
            (unsigned long)cfg.sdr_hw_fir_wnom_tx,
            (unsigned long)cfg.sdr_hw_fir_wnom_rx
        );
        if (ret < 0) {
            log_error() << "ad9361_set_bb_rate_custom_filter_manual failed (ret=" << ret << ")\n";
            iio_context_destroy(cfg_ctx);
            return false;
        }
        log_info() << "AD9361 hardware FIR configured\n";

        // Verify FIR is actually enabled
        int fir_enabled = 0;
        if (ad9361_get_trx_fir_enable(phy, &fir_enabled) == 0) {
            if (fir_enabled) {
                log_info() << "AD9361 hardware FIR readback: enabled\n";
            } else {
                log_error() << "AD9361 hardware FIR readback: disabled (expected enabled)\n";
                iio_context_destroy(cfg_ctx);
                return false;
            }
        } else {
            log_warning() << "Could not read back hardware FIR enable state\n";
        }

        // Read back FIR config (tap count and decimation factor)
        {
            char fir_buf[256] = {0};
            ssize_t nb = iio_device_attr_read(phy, "filter_fir_config", fir_buf, sizeof(fir_buf) - 1);
            if (nb > 0) {
                log_info() << "AD9361 FIR config readback: " << fir_buf << "\n";
            } else {
                log_warning() << "Could not read back filter_fir_config\n";
            }
        }

        if (cfg.sdr_min_readback) {
            log_info() << "Minimal readback: sample rate mismatch is non-fatal\n";
        }
        if (!readback_iio_rx_config(phy, cfg.sdr_frequency,
                                    (long long)cfg.sdr_hw_fir_rate,
                                    (long long)cfg.sdr_hw_fir_wnom_rx,
                                    cfg.sdr_gain,
                                    cfg.sdr_rate_tolerance, !cfg.sdr_min_readback)) {
            iio_context_destroy(cfg_ctx);
            return false;
        }
    } else {
        // Software-only decimation path
        // Disable FIR in case a previous run left it enabled
        int fir_ret = ad9361_set_trx_fir_enable(phy, 0);
        if (fir_ret < 0) {
            log_warning() << "Could not disable hardware FIR (ret=" << fir_ret
                          << "), may be normal if FIR was never enabled\n";
        }

        if (!write_iio_rx_config(phy, cfg.sdr_frequency, (long long)cfg.sdr_rate,
                                 (long long)cfg.sdr_rf_bandwidth, cfg.sdr_gain)) {
            iio_context_destroy(cfg_ctx);
            return false;
        }
        if (cfg.sdr_min_readback) {
            log_info() << "Minimal readback: sample rate mismatch is non-fatal\n";
        }
        if (!readback_iio_rx_config(phy, cfg.sdr_frequency, (long long)cfg.sdr_rate,
                                    (long long)cfg.sdr_rf_bandwidth, cfg.sdr_gain,
                                    cfg.sdr_rate_tolerance, !cfg.sdr_min_readback)) {
            iio_context_destroy(cfg_ctx);
            return false;
        }
    }

    iio_context_destroy(cfg_ctx);
    return true;
}

bool ad9361_cleanup_fir(const PipelineConfig& cfg) {
    if (!cfg.sdr_hw_fir_enable) {
        return true;
    }

    struct iio_context* ctx = iio_create_context_from_uri(cfg.sdr_uri.c_str());
    if (!ctx) {
        log_warning() << "Could not connect to IIO for FIR cleanup\n";
        return false;
    }

    struct iio_device* phy = iio_context_find_device(ctx, "ad9361-phy");
    if (!phy) {
        log_warning() << "No ad9361-phy device for FIR cleanup\n";
        iio_context_destroy(ctx);
        return false;
    }

    ad9361_set_trx_fir_enable(phy, 0);
    int fir_enabled = 0;
    bool ok = (ad9361_get_trx_fir_enable(phy, &fir_enabled) == 0 && !fir_enabled);
    if (ok) {
        log_info() << "AD9361 hardware FIR disabled (cleanup)\n";
    } else {
        log_warning() << "AD9361 hardware FIR may still be enabled after cleanup\n";
    }

    iio_context_destroy(ctx);
    return ok;
}
