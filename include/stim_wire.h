#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define STIM_WIRE_VERSION 2
#define STIM_PACKET_SIZE 20
#define STIM_UUID(last) BT_UUID_128_ENCODE(last,0x1b15,0x4f1b,0x9e90,0x77a4c9b5a200)
#define STIM_RX_SERVICE 0x7f520001
#define STIM_RX_WRITE   0x7f520002
#define STIM_RX_NOTIFY  0x7f520003
#define STIM_GW_SERVICE 0x7f510001
#define STIM_GW_WRITE   0x7f510002
#define STIM_GW_NOTIFY  0x7f510003
enum stim_op { STIM_QUERY=1,STIM_CONFIG,STIM_START,STIM_STOP,STIM_KEEPALIVE };
enum stim_packet { STIM_STATUS=0x80,STIM_PARAMS,STIM_GATEWAY,STIM_LIMITS,STIM_REJECT };
enum stim_state { STIM_OFF,STIM_SETTLING,STIM_ACTIVE,STIM_LOCKED,STIM_FAULT };
enum stim_result { STIM_OK,STIM_BAD_FRAME,STIM_BAD_RANGE,STIM_NOT_READY,STIM_SECURITY,
    STIM_POWER,STIM_TIMEOUT,STIM_HARDWARE,STIM_BUSY,STIM_STALE,STIM_PROFILE };
enum stim_flags { STIM_F_READY=1,STIM_F_SECURE=2,STIM_F_POWER=4,STIM_F_PROFILE=8,
    STIM_F_BOARD=16,STIM_F_CONFIGURED=32,STIM_F_LINK_ALLOWED=64 };
struct stim_params { uint16_t current_ua;uint32_t frequency_mhz;uint16_t width_us;uint32_t duration_ms; };
static inline uint16_t sw16(const uint8_t *p){return p[0]|((uint16_t)p[1]<<8);}
static inline uint32_t sw32(const uint8_t *p){return sw16(p)|((uint32_t)sw16(p+2)<<16);}
static inline void sp16(uint8_t *p,uint16_t n){p[0]=n;p[1]=n>>8;}
static inline void sp32(uint8_t *p,uint32_t n){sp16(p,n);sp16(p+2,n>>16);}
static inline struct stim_params stim_decode_params(const uint8_t *p) {
    return (struct stim_params){sw16(p+8),sw32(p+10),sw16(p+14),sw32(p+16)};
}
static inline void stim_encode_params(uint8_t *p,const struct stim_params *s) {
    sp16(p+8,s->current_ua);sp32(p+10,s->frequency_mhz);sp16(p+14,s->width_us);sp32(p+16,s->duration_ms);
}
static inline bool stim_command_valid(const uint8_t *p,size_t len) {
    if(len!=20||p[0]!=STIM_WIRE_VERSION||p[1]<STIM_QUERY||p[1]>STIM_KEEPALIVE||sw16(p+2)==0xffff)return false;
    if(p[1]!=STIM_CONFIG)for(unsigned i=8;i<20;i++)if(p[i])return false;
    return true;
}
