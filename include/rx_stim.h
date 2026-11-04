#pragma once
#include "stim_wire.h"
int rx_stim_init(void);
void rx_stim_poll(void);
void rx_stim_stop(uint8_t reason);
void rx_stim_emergency_off(void);
void rx_stim_session(uint32_t nonce,bool link_allowed);
uint8_t rx_stim_command(uint8_t op,uint32_t nonce,const struct stim_params *params,uint32_t permit);
uint32_t rx_stim_generation(void);
void rx_stim_status(uint8_t *p,uint16_t id,uint8_t op,uint8_t result);
void rx_stim_params(uint8_t *p,uint16_t id);
void rx_stim_limits(uint8_t *p);
int rx_hw_init(void);
int rx_hw_apply(struct stim_params *params);
int rx_hw_start(const struct stim_params *params);
void rx_hw_keepalive(void);
void rx_hw_off(void);
bool rx_hw_expired(void);
