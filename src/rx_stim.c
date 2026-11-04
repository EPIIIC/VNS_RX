#include "rx_stim.h"
#include "rx_profile.h"
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
static atomic_t cancellation;
static uint32_t armed_generation;
static struct k_mutex lock;
static bool hw_ready,link_allowed,configured;
static uint8_t state=STIM_LOCKED,fault_code;
static uint32_t session_nonce;
static int64_t end_ms,settle_ms;
static struct stim_params settings;
static bool profile_valid(void) {
    return RX_MAX_CURRENT_UA&&RX_ISET_FULL_SCALE_UA&&
        RX_MAX_CURRENT_UA<=RX_ISET_FULL_SCALE_UA&&RX_MIN_FREQUENCY_MHZ&&RX_MAX_FREQUENCY_MHZ>=RX_MIN_FREQUENCY_MHZ&&
        RX_MIN_WIDTH_US&&RX_MAX_WIDTH_US>=RX_MIN_WIDTH_US&&RX_MAX_DURATION_MS&&RX_MAX_DURATION_MS<=UINT32_MAX/1000U&&
        RX_ISET_SETTLE_US;
}
static void off(uint8_t reason) {
    rx_hw_off();configured=false;end_ms=0;
    fault_code=reason;
    state=profile_valid()&&hw_ready?(reason==STIM_HARDWARE?STIM_FAULT:STIM_OFF):STIM_LOCKED;
}
void rx_stim_emergency_off(void){atomic_inc(&cancellation);rx_hw_off();}
uint32_t rx_stim_generation(void){return atomic_get(&cancellation);}
void rx_stim_stop(uint8_t reason){rx_hw_off();k_mutex_lock(&lock,K_FOREVER);off(reason);k_mutex_unlock(&lock);}
void rx_stim_session(uint32_t nonce,bool allowed) {
    rx_hw_off();k_mutex_lock(&lock,K_FOREVER);off(allowed?STIM_OK:STIM_SECURITY);session_nonce=nonce;link_allowed=allowed;k_mutex_unlock(&lock);
}
int rx_stim_init(void) {
    k_mutex_init(&lock);int err=rx_hw_init();hw_ready=!err;
    state=profile_valid()&&hw_ready?STIM_OFF:STIM_LOCKED;
    return err;
}
static bool valid(const struct stim_params *s) {
    if(!profile_valid()||!s||!s->current_ua||s->current_ua>RX_MAX_CURRENT_UA||s->frequency_mhz<RX_MIN_FREQUENCY_MHZ||
       s->frequency_mhz>RX_MAX_FREQUENCY_MHZ||s->width_us<RX_MIN_WIDTH_US||s->width_us>RX_MAX_WIDTH_US||
       !s->duration_ms||s->duration_ms>RX_MAX_DURATION_MS)return false;
    uint64_t period=(1000000000ULL+s->frequency_mhz/2)/s->frequency_mhz;
    /* Never support a continuous envelope or overlapping ON/OFF events. */
    return period<=UINT32_MAX&&s->width_us+2U<period;
}
uint8_t rx_stim_command(uint8_t op,uint32_t nonce,const struct stim_params *s,uint32_t permit) {
    k_mutex_lock(&lock,K_FOREVER);uint8_t result=STIM_OK;
    if(state==STIM_ACTIVE||state==STIM_SETTLING){if(k_uptime_get()>=end_ms)off(STIM_OK);else if(rx_hw_expired())off(STIM_TIMEOUT);}
    if(op==STIM_STOP)off(STIM_OK);
    else if(op==STIM_QUERY){};
    if(op!=STIM_QUERY&&op!=STIM_STOP) {
        if(!link_allowed||!session_nonce||nonce!=session_nonce){off(STIM_SECURITY);result=STIM_STALE;}
        else if(op==STIM_CONFIG) {
            if(state==STIM_ACTIVE||state==STIM_SETTLING){off(STIM_BUSY);result=STIM_BUSY;}
            else if(!valid(s)){off(STIM_BAD_RANGE);result=profile_valid()?STIM_BAD_RANGE:STIM_PROFILE;}
            else {settings=*s;if(rx_hw_apply(&settings)){off(STIM_HARDWARE);result=STIM_HARDWARE;}else{configured=true;state=STIM_OFF;fault_code=STIM_OK;}}
        } else if(op==STIM_START) {
            if(permit!=rx_stim_generation()||!configured||!valid(&settings)||!hw_ready||state!=STIM_OFF){off(STIM_NOT_READY);result=STIM_NOT_READY;}
            else {state=STIM_SETTLING;settle_ms=k_uptime_get()+(RX_ISET_SETTLE_US+999U)/1000U;
                armed_generation=permit;end_ms=settle_ms+settings.duration_ms;rx_hw_keepalive();}
        } else if(op==STIM_KEEPALIVE) {
            if(state==STIM_ACTIVE||state==STIM_SETTLING)rx_hw_keepalive();
            else if(state!=STIM_OFF)result=STIM_NOT_READY;
        } else {off(STIM_BAD_FRAME);result=STIM_BAD_FRAME;}
    }
    k_mutex_unlock(&lock);return result;
}
void rx_stim_poll(void) {
    k_mutex_lock(&lock,K_FOREVER);
    if(state==STIM_ACTIVE||state==STIM_SETTLING) {
        if(!link_allowed||armed_generation!=rx_stim_generation())off(STIM_SECURITY);
        else if(k_uptime_get()>=end_ms)off(STIM_OK);
        else if(rx_hw_expired())off(STIM_TIMEOUT);
    }
    if(state==STIM_SETTLING&&k_uptime_get()>=settle_ms) {
        unsigned int key=irq_lock();
        if(armed_generation!=rx_stim_generation()||rx_hw_start(&settings))off(STIM_HARDWARE);
        else {state=STIM_ACTIVE;end_ms=k_uptime_get()+settings.duration_ms;}
        irq_unlock(key);
    }
    k_mutex_unlock(&lock);
}
void rx_stim_status(uint8_t *p,uint16_t id,uint8_t op,uint8_t result) {
    memset(p,0,20);p[0]=STIM_WIRE_VERSION;p[1]=STIM_STATUS;sp16(p+2,id);p[4]=op;p[5]=result;
    k_mutex_lock(&lock,K_FOREVER);
    if(!op)p[5]=fault_code;
    uint16_t flags=(link_allowed?STIM_F_LINK_ALLOWED:0)|(profile_valid()?STIM_F_PROFILE:0)|
        (hw_ready?STIM_F_BOARD:0)|(configured?STIM_F_CONFIGURED:0)|(state<<8);
    if(link_allowed&&profile_valid()&&hw_ready&&(state==STIM_OFF||state==STIM_ACTIVE||state==STIM_SETTLING))flags|=STIM_F_READY;
    /* No voltage sensing on this RX. Keep the v2 voltage slots zero (unknown)
     * and leave STIM_F_POWER clear instead of fabricating a measured supply. */
    sp16(p+6,flags);sp32(p+8,session_nonce);
    sp32(p+16,end_ms?MAX(0,end_ms-k_uptime_get()):0);k_mutex_unlock(&lock);
}
void rx_stim_params(uint8_t *p,uint16_t id) {
    memset(p,0,20);p[0]=STIM_WIRE_VERSION;p[1]=STIM_PARAMS;sp16(p+2,id);
    k_mutex_lock(&lock,K_FOREVER);sp32(p+4,session_nonce);stim_encode_params(p,&settings);k_mutex_unlock(&lock);
}
void rx_stim_limits(uint8_t *p) {
    memset(p,0,20);p[0]=STIM_WIRE_VERSION;p[1]=STIM_LIMITS;sp16(p+2,RX_MAX_CURRENT_UA);
    sp32(p+4,RX_MIN_FREQUENCY_MHZ);sp32(p+8,RX_MAX_FREQUENCY_MHZ);
    sp16(p+12,RX_MIN_WIDTH_US);sp16(p+14,RX_MAX_WIDTH_US);sp32(p+16,RX_MAX_DURATION_MS);
}
