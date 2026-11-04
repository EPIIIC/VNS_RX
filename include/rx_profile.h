#pragma once
#include <zephyr/devicetree.h>
/* User-confirmed legacy RX wiring is in app.overlay. Availability comes from
 * actual GPIO/driver initialization, not a separate approval switch. */
#define RX_HAS_OUTPUT_PINS (DT_NODE_HAS_PROP(DT_PATH(zephyr_user),iset_gpios) && \
                            DT_NODE_HAS_PROP(DT_PATH(zephyr_user),stim_enable_gpios))
#define RX_ISET_FULL_SCALE_UA 1939U /* Legacy VNS_nRF conversion model; NOT measured feedback. */
#define RX_DEFAULT_CURRENT_UA 1000U
#define RX_DEFAULT_FREQUENCY_MHZ 15000U
#define RX_DEFAULT_WIDTH_US 250U
/* Legacy software ranges, bounded by the ISET model and v2 wire fields.
 * These are implementation limits, not clinical treatment recommendations. */
#define RX_MAX_CURRENT_UA RX_ISET_FULL_SCALE_UA
#define RX_MIN_FREQUENCY_MHZ 100U
#define RX_MAX_FREQUENCY_MHZ 500000U
#define RX_MIN_WIDTH_US 100U
#define RX_MAX_WIDTH_US 65535U
#define RX_MAX_DURATION_MS 3600000U
#define RX_ISET_SETTLE_US 6000U /* Legacy stimulation.c SETTLE_MS=6. */
#define RX_LEASE_MS 1500U /* Fault-response deadline; never auto-renewed on reconnect. */
