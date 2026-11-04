#include "rx_link_policy.h"
#include "stim_wire.h"
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/uuid.h>
int rx_link_init(void){return 0;}
bool rx_link_configured(void){return sizeof(RX_DISCOVERY_NAME)>1;}
bool rx_link_allowed(struct bt_conn *conn) {
    struct bt_conn_info info;
    if(!conn||bt_conn_get_info(conn,&info)||info.state!=BT_CONN_STATE_CONNECTED)return false;
#if defined(VNS_ROLE_TX)
    return info.role==BT_CONN_ROLE_CENTRAL;
#else
    return info.role==BT_CONN_ROLE_PERIPHERAL;
#endif
}
#if defined(VNS_ROLE_TX)
static const struct bt_uuid_128 rx_service_uuid=BT_UUID_INIT_128(STIM_UUID(STIM_RX_SERVICE));
struct candidate {bt_addr_le_t addr;uint32_t seen,name_at,service_at;bool used,name,service;};
static struct candidate candidates[4];
struct fields {bool has_name,name,has_service,service;};
static bool parse(struct bt_data *d,void *user) {
    struct fields *f=user;
    if(d->type==BT_DATA_NAME_COMPLETE) {
        f->has_name=true;f->name=d->data_len==sizeof(RX_DISCOVERY_NAME)-1&&
            !memcmp(d->data,RX_DISCOVERY_NAME,sizeof(RX_DISCOVERY_NAME)-1);
    } else if(d->type==BT_DATA_UUID128_ALL||d->type==BT_DATA_UUID128_SOME) {
        bool found=false;
        for(unsigned i=0;i+16<=d->data_len;i+=16)if(!memcmp(d->data+i,rx_service_uuid.val,16))found=true;
        if(found||d->type==BT_DATA_UUID128_ALL){f->has_service=true;f->service=found;}
    }
    return true;
}
bool rx_link_candidate(const bt_addr_le_t *addr,struct net_buf_simple *ad) {
    struct fields f={0};bt_data_parse(ad,parse,&f);
    uint32_t now=k_uptime_get_32();struct candidate *slot=NULL,*oldest=&candidates[0];
    for(unsigned i=0;i<ARRAY_SIZE(candidates);i++) {
        struct candidate *c=&candidates[i];
        if(c->used&&!bt_addr_le_cmp(&c->addr,addr)){slot=c;break;}
        if(!c->used||now-c->seen>now-oldest->seen)oldest=c;
    }
    if(!slot) {
        if(!f.name&&!f.service)return false;
        slot=oldest;memset(slot,0,sizeof(*slot));slot->addr=*addr;slot->used=true;
    }
    slot->seen=now;
    if(f.has_name){slot->name=f.name;slot->name_at=now;}
    if(f.has_service){slot->service=f.service;slot->service_at=now;}
    /* Active-scan response can carry the name while the advertisement carries
     * the UUID. Merge ONLY for the same address and a bounded two-second window. */
    return slot->name&&slot->service&&now-slot->name_at<=2000&&now-slot->service_at<=2000;
}
#else
bool rx_link_candidate(const bt_addr_le_t *addr,struct net_buf_simple *ad){ARG_UNUSED(addr);ARG_UNUSED(ad);return false;}
#endif
