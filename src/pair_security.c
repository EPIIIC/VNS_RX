#include "pair_security.h"
#include "pair_provision.h"
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
static bt_addr_le_t expected;
static bool configured;
static void find_bond(const struct bt_bond_info *info,void *user) {
    bool *found=user;if(!bt_addr_le_cmp(&info->addr,&expected))*found=true;
}
static bool bonded_expected(void) {
    bool found=false;bt_foreach_bond(BT_ID_DEFAULT,find_bond,&found);return found;
}
bool pair_security_configured(void){return configured;}
void pair_security_confirm_identity(void) {
    bt_addr_le_t actual[1],required;size_t count=1;
#if defined(VNS_ROLE_TX)
    const char *addr=VNS_TX_IDENTITY,*type=VNS_TX_ADDR_TYPE;
#else
    const char *addr=VNS_RX_IDENTITY,*type=VNS_RX_ADDR_TYPE;
#endif
    if(!configured)return;
    bt_id_get(actual,&count);
    if(!count||bt_addr_le_from_str(addr,type,&required)||bt_addr_le_cmp(actual,&required))configured=false;
}
bool pair_security_expected(const bt_addr_le_t *a){return configured&&!bt_addr_le_cmp(a,&expected);}
bool pair_security_trusted(struct bt_conn *c) {
    struct bt_conn_info i;
    return c&&!bt_conn_get_info(c,&i)&&pair_security_expected(i.le.dst)&&
        bt_conn_get_security(c)>=BT_SECURITY_L4&&bonded_expected();
}
static enum bt_security_err accept_pair(struct bt_conn *c,const struct bt_conn_pairing_feat *const feat) {
    ARG_UNUSED(feat);struct bt_conn_info i;
    if(!configured||bt_conn_get_info(c,&i))return BT_SECURITY_ERR_PAIR_NOT_ALLOWED;
#if defined(VNS_ROLE_TX)
    if(i.role==BT_CONN_ROLE_PERIPHERAL)return BT_SECURITY_ERR_SUCCESS;
#endif
    /* A stored bond must be reused. Never silently replace it after a failed encryption. */
    if(!pair_security_expected(i.le.dst)||bonded_expected())return BT_SECURITY_ERR_PAIR_NOT_ALLOWED;
#if !defined(VNS_ROLE_TX)
    if(k_uptime_get()>120000)return BT_SECURITY_ERR_PAIR_NOT_ALLOWED;
#endif
    return BT_SECURITY_ERR_SUCCESS;
}
static void cancelled(struct bt_conn *c){ARG_UNUSED(c);}
static void display(struct bt_conn *c,unsigned int key){ARG_UNUSED(c);ARG_UNUSED(key);/* Out-of-band provisioning record, never public telemetry. */}
static uint32_t passkey(struct bt_conn *c) {
    ARG_UNUSED(c);
#if defined(VNS_ROLE_TX)
    return VNS_PHONE_PASSKEY;
#else
    return VNS_RX_PASSKEY;
#endif
}
static const struct bt_conn_auth_cb auth={.pairing_accept=accept_pair,.app_passkey=passkey,.passkey_display=display,.cancel=cancelled};
static void enter_key(struct bt_conn *c) {
    struct bt_conn_info i;
    if(configured&&!bt_conn_get_info(c,&i)&&pair_security_expected(i.le.dst))
        (void)bt_conn_auth_passkey_entry(c,VNS_RX_PASSKEY);
    else (void)bt_conn_auth_cancel(c);
}
static const struct bt_conn_auth_cb central_auth={.pairing_accept=accept_pair,.passkey_entry=enter_key,.cancel=cancelled};
int pair_security_for_central(struct bt_conn *c){return bt_conn_auth_cb_overlay(c,&central_auth);}
int pair_security_init(void) {
#if defined(VNS_ROLE_TX)
    const char *addr=VNS_RX_IDENTITY,*type=VNS_RX_ADDR_TYPE;
#else
    const char *addr=VNS_TX_IDENTITY,*type=VNS_TX_ADDR_TYPE;
#endif
    configured=VNS_PAIR_PROVISIONED&&VNS_RX_PASSKEY>0&&VNS_RX_PASSKEY<=999999&&
        VNS_PHONE_PASSKEY>0&&VNS_PHONE_PASSKEY<=999999&&!bt_addr_le_from_str(addr,type,&expected);
    return bt_conn_auth_cb_register(&auth);
}
