#pragma once
#include <zephyr/bluetooth/conn.h>
#include <zephyr/net_buf.h>
/* Prototype discovery label, NOT a cryptographic identity or saved bond. */
#define RX_DISCOVERY_NAME "VNS_RX_01"
int rx_link_init(void);
bool rx_link_configured(void);
bool rx_link_allowed(struct bt_conn *conn);
bool rx_link_candidate(const bt_addr_le_t *addr,struct net_buf_simple *ad);
