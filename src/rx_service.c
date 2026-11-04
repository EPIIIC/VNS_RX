#include "rx_service.h"
#include "rx_stim.h"
#include "rx_link_policy.h"
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
static struct bt_uuid_128 service=BT_UUID_INIT_128(STIM_UUID(STIM_RX_SERVICE));
static struct bt_uuid_128 command_uuid=BT_UUID_INIT_128(STIM_UUID(STIM_RX_WRITE));
static struct bt_uuid_128 notify_uuid=BT_UUID_INIT_128(STIM_UUID(STIM_RX_NOTIFY));
static struct bt_conn *peer;
static struct k_spinlock lock;
static atomic_t epoch,subscribed,stop_requested;
static uint32_t nonce;
static uint16_t last_id;
static bool have_last;
struct request {uint32_t epoch,permit;uint8_t data[20];};
K_MSGQ_DEFINE(requests,sizeof(struct request),6,4);
static void advertise(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(advertising,advertise);
static void invalidate(void) {
    rx_stim_emergency_off();atomic_inc(&epoch);atomic_set(&stop_requested,1);
}
static ssize_t read_status(struct bt_conn *c,const struct bt_gatt_attr *a,void *buf,uint16_t len,uint16_t off) {
    uint8_t p[20];rx_stim_status(p,0xffff,0,0);return bt_gatt_attr_read(c,a,buf,len,off,p,20);
}
static void ccc(const struct bt_gatt_attr *a,uint16_t value) {
    ARG_UNUSED(a);atomic_set(&subscribed,value==BT_GATT_CCC_NOTIFY);
    if(value!=BT_GATT_CCC_NOTIFY)invalidate();
}
static ssize_t received(struct bt_conn *c,const struct bt_gatt_attr *a,const void *buf,uint16_t len,uint16_t off,uint8_t flags) {
    ARG_UNUSED(a);const uint8_t *p=buf;
    if(off||(flags&BT_GATT_WRITE_FLAG_PREPARE)||!stim_command_valid(p,len)){invalidate();return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);}
    if(!rx_link_allowed(c)){invalidate();return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);}
    if(!atomic_get(&subscribed)){invalidate();return BT_GATT_ERR(BT_ATT_ERR_CCC_IMPROPER_CONF);}
    if(p[1]==STIM_STOP)invalidate();
    struct request q={.epoch=atomic_get(&epoch),.permit=rx_stim_generation()};memcpy(q.data,p,20);
    if(k_msgq_put(&requests,&q,K_NO_WAIT)){invalidate();return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);}
    return len;
}
BT_GATT_SERVICE_DEFINE(rx_service,
    BT_GATT_PRIMARY_SERVICE(&service),
    BT_GATT_CHARACTERISTIC(&command_uuid.uuid,BT_GATT_CHRC_WRITE,BT_GATT_PERM_WRITE,NULL,received,NULL),
    BT_GATT_CHARACTERISTIC(&notify_uuid.uuid,BT_GATT_CHRC_READ|BT_GATT_CHRC_NOTIFY,BT_GATT_PERM_READ,read_status,NULL,NULL),
    BT_GATT_CCC(ccc,BT_GATT_PERM_READ|BT_GATT_PERM_WRITE));
static const struct bt_data ad[]={BT_DATA_BYTES(BT_DATA_FLAGS,BT_LE_AD_GENERAL|BT_LE_AD_NO_BREDR),BT_DATA_BYTES(BT_DATA_UUID128_ALL,STIM_UUID(STIM_RX_SERVICE))};
static const struct bt_data scan_data[]={BT_DATA(BT_DATA_NAME_COMPLETE,CONFIG_BT_DEVICE_NAME,sizeof(CONFIG_BT_DEVICE_NAME)-1)};
static void advertise(struct k_work *work) {
    ARG_UNUSED(work);k_spinlock_key_t key=k_spin_lock(&lock);bool busy=peer!=NULL;k_spin_unlock(&lock,key);
    if(busy)return;
    int err=bt_le_adv_start(BT_LE_ADV_CONN_FAST_2,ad,ARRAY_SIZE(ad),scan_data,ARRAY_SIZE(scan_data));
    if(err&&err!=-EALREADY)k_work_reschedule(&advertising,K_SECONDS(1));
}
static void connected(struct bt_conn *c,uint8_t error) {
    if(error){k_work_reschedule(&advertising,K_SECONDS(1));return;}
    struct bt_conn_info i;
    if(bt_conn_get_info(c,&i)||i.role!=BT_CONN_ROLE_PERIPHERAL){(void)bt_conn_disconnect(c,BT_HCI_ERR_AUTH_FAIL);return;}
    k_spinlock_key_t key=k_spin_lock(&lock);
    if(peer){k_spin_unlock(&lock,key);(void)bt_conn_disconnect(c,BT_HCI_ERR_REMOTE_USER_TERM_CONN);return;}
    peer=bt_conn_ref(c);atomic_clear(&subscribed);k_spin_unlock(&lock,key);invalidate();
    /* No SMP or saved bond in prototype mode. A new session still clears output. */
}
static void disconnected(struct bt_conn *c,uint8_t reason) {
    ARG_UNUSED(reason);invalidate();
    k_spinlock_key_t key=k_spin_lock(&lock);struct bt_conn *old=peer==c?peer:NULL;
    if(old){peer=NULL;atomic_clear(&subscribed);}k_spin_unlock(&lock,key);if(old)bt_conn_unref(old);
}
static void recycled(void){k_work_reschedule(&advertising,K_NO_WAIT);}
BT_CONN_CB_DEFINE(rx_connections)={.connected=connected,.disconnected=disconnected,.recycled=recycled};
static void notify(struct bt_conn *c,uint16_t id,uint8_t op,uint8_t result) {
    uint8_t p[20];rx_stim_status(p,id,op,result);
    if(bt_gatt_notify(c,&rx_service.attrs[4],p,20)){invalidate();return;}
    rx_stim_params(p,id);if(bt_gatt_notify(c,&rx_service.attrs[4],p,20)){invalidate();return;}
    rx_stim_limits(p);if(bt_gatt_notify(c,&rx_service.attrs[4],p,20))invalidate();
}
void rx_service_poll(void) {
    k_spinlock_key_t key=k_spin_lock(&lock);struct bt_conn *c=peer?bt_conn_ref(peer):NULL;k_spin_unlock(&lock,key);
    if(atomic_cas(&stop_requested,1,0)){rx_stim_session(0,false);nonce=0;have_last=false;}
    bool allowed=c&&rx_link_allowed(c)&&atomic_get(&subscribed);
    if(!allowed){if(nonce){rx_stim_session(0,false);nonce=0;have_last=false;}if(c)bt_conn_unref(c);return;}
    if(!nonce){do{nonce=sys_rand32_get();}while(!nonce);rx_stim_session(nonce,true);have_last=false;}
    struct request q;
    while(!k_msgq_get(&requests,&q,K_NO_WAIT)) {
        if(q.epoch!=(uint32_t)atomic_get(&epoch))continue;
        uint8_t *p=q.data,op=p[1],result;uint16_t id=sw16(p+2);
        if(have_last&&(int16_t)(id-last_id)<=0){rx_stim_stop(STIM_STALE);result=STIM_STALE;}
        else {
            struct stim_params s=stim_decode_params(p);last_id=id;have_last=true;
            result=rx_stim_command(op,sw32(p+4),&s,q.permit);
        }
        notify(c,id,op,result);
    }
    static uint32_t published;
    if(k_uptime_get_32()-published>=1000){published=k_uptime_get_32();notify(c,0xffff,0,0);}
    bt_conn_unref(c);
}
int rx_service_init(void) {
    int err=rx_link_init();if(!err)err=bt_enable(NULL);
    if(!err)k_work_reschedule(&advertising,K_NO_WAIT);return err;
}
