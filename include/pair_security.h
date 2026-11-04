#pragma once
#include <zephyr/bluetooth/conn.h>
int pair_security_init(void);
void pair_security_confirm_identity(void);
int pair_security_for_central(struct bt_conn *conn);
bool pair_security_configured(void);
bool pair_security_expected(const bt_addr_le_t *address);
bool pair_security_trusted(struct bt_conn *conn);
