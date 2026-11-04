#include "rx_stim.h"
#include "rx_profile.h"
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/drivers/gpio.h>
#if RX_HAS_OUTPUT_PINS
#include <zephyr/irq.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <zephyr/sys/onoff.h>
#include <gpiote_nrfx.h>
#include <nrfx_timer.h>
#include <nrfx_pwm.h>
#include <nrfx_gpiote.h>
#include <helpers/nrfx_gppi.h>
#include <hal/nrf_gpio.h>
#define USER DT_PATH(zephyr_user)
#define ISET DT_GPIO_PIN(USER,iset_gpios)
#define GATE DT_GPIO_PIN(USER,stim_enable_gpios)
#define LED DT_GPIO_PIN(USER,pulse_led_gpios)
#define ACTIVE_LOW ((DT_GPIO_FLAGS(USER,stim_enable_gpios)&GPIO_ACTIVE_LOW)!=0)
BUILD_ASSERT(ISET==18&&GATE==16&&ACTIVE_LOW,"Keep the user-confirmed legacy RX pin mapping");
BUILD_ASSERT(LED==12&&DT_GPIO_FLAGS(USER,pulse_led_gpios)==GPIO_ACTIVE_HIGH,"Keep legacy LED2 P0.12 active-high");
#define TOP 160U
static nrfx_timer_t pulse=NRFX_TIMER_INSTANCE(NRF_TIMER2),duration=NRFX_TIMER_INSTANCE(NRF_TIMER3),lease=NRFX_TIMER_INSTANCE(NRF_TIMER4);
/* TIMER3 is already the session deadline; TIMER1 is free on nRF52832/MPSL. */
static nrfx_timer_t led_carrier=NRFX_TIMER_INSTANCE(NRF_TIMER1);
static nrfx_pwm_t pwm=NRFX_PWM_INSTANCE(NRF_PWM0);
static nrfx_gpiote_t *const gpiote=&GPIOTE_NRFX_INST_BY_NODE(DT_NODELABEL(gpiote));
static nrfx_gppi_handle_t on_route,off_route,end_route,lease_route;
static nrfx_gppi_handle_t led_on_route,led_off_route,led_stop_route,led_end_route,led_lease_route;
static nrfx_gppi_group_handle_t group;
static bool routes,group_ready,ready;
static uint8_t channel=UINT8_MAX,led_channel=UINT8_MAX;
static uint16_t duty __aligned(4)=BIT(15);
static const nrf_pwm_sequence_t sequence={.values.p_common=&duty,.length=1,.repeats=31,.end_delay=0};
static uint32_t gate_off_task(void){return ACTIVE_LOW?nrfx_gpiote_set_task_address_get(gpiote,GATE):nrfx_gpiote_clr_task_address_get(gpiote,GATE);}
static uint32_t led_off_task(void){return nrfx_gpiote_clr_task_address_get(gpiote,LED);}
static int early_safe(void) {
    nrf_gpio_pin_write(GATE,ACTIVE_LOW); /* Inactive latch before output enable. */
    nrf_gpio_cfg(GATE,NRF_GPIO_PIN_DIR_OUTPUT,NRF_GPIO_PIN_INPUT_DISCONNECT,
        ACTIVE_LOW?NRF_GPIO_PIN_PULLUP:NRF_GPIO_PIN_PULLDOWN,NRF_GPIO_PIN_S0S1,NRF_GPIO_PIN_NOSENSE);
    nrf_gpio_pin_clear(ISET);nrf_gpio_cfg_output(ISET);
    nrf_gpio_pin_clear(LED);nrf_gpio_cfg_output(LED);return 0;
}
SYS_INIT(early_safe,PRE_KERNEL_1,1);
void rx_hw_off(void) {
    if(group_ready)nrfx_gppi_group_disable(group);
    nrf_timer_task_trigger(NRF_TIMER2,NRF_TIMER_TASK_STOP);
    nrf_timer_task_trigger(NRF_TIMER3,NRF_TIMER_TASK_STOP);
    nrf_timer_task_trigger(NRF_TIMER4,NRF_TIMER_TASK_STOP);
    nrf_timer_task_trigger(NRF_TIMER1,NRF_TIMER_TASK_STOP);
    nrf_timer_task_trigger(NRF_TIMER1,NRF_TIMER_TASK_CLEAR);
    if(channel!=UINT8_MAX)nrf_gpiote_task_trigger(NRF_GPIOTE,ACTIVE_LOW?nrf_gpiote_set_task_get(channel):nrf_gpiote_clr_task_get(channel));
    if(led_channel!=UINT8_MAX)nrf_gpiote_task_trigger(NRF_GPIOTE,nrf_gpiote_clr_task_get(led_channel));
    nrf_gpio_pin_write(GATE,ACTIVE_LOW);nrf_gpio_pin_clear(LED);duty=BIT(15);__DMB();
}
static K_SEM_DEFINE(clock_ready,0,1);
static struct onoff_client clock_client;
static int clock_error;
static void clock_started(struct onoff_manager *m,struct onoff_client *c,uint32_t state,int result) {
    ARG_UNUSED(m);ARG_UNUSED(c);ARG_UNUSED(state);clock_error=result;k_sem_give(&clock_ready);
}
int rx_hw_init(void) {
    struct onoff_manager *hf=z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF);
    if(!hf)return -ENODEV;sys_notify_init_callback(&clock_client.notify,clock_started);
    int err=onoff_request(hf,&clock_client);if(err<0)return err;
    if(k_sem_take(&clock_ready,K_SECONDS(1)))return -ETIMEDOUT;if(clock_error)return clock_error;
    nrfx_timer_config_t tc=NRFX_TIMER_DEFAULT_CONFIG(1000000);tc.bit_width=NRF_TIMER_BIT_WIDTH_32;
    if((err=nrfx_timer_init(&pulse,&tc,NULL))||(err=nrfx_timer_init(&duration,&tc,NULL))||(err=nrfx_timer_init(&lease,&tc,NULL)))return err;
    nrfx_timer_config_t lc=NRFX_TIMER_DEFAULT_CONFIG(16000000);lc.bit_width=NRF_TIMER_BIT_WIDTH_16;
    if((err=nrfx_timer_init(&led_carrier,&lc,NULL)))return err;
    if((err=nrfx_gpiote_channel_alloc(gpiote,&channel)))return err;
    if((err=nrfx_gpiote_channel_alloc(gpiote,&led_channel)))return err;
    const nrfx_gpiote_output_config_t output=NRFX_GPIOTE_DEFAULT_OUTPUT_CONFIG;
    const nrfx_gpiote_task_config_t task={.task_ch=channel,.polarity=NRF_GPIOTE_POLARITY_TOGGLE,
        .init_val=ACTIVE_LOW?NRF_GPIOTE_INITIAL_VALUE_HIGH:NRF_GPIOTE_INITIAL_VALUE_LOW};
    if((err=nrfx_gpiote_output_configure(gpiote,GATE,&output,&task)))return err;
    const nrfx_gpiote_task_config_t led_task={.task_ch=led_channel,.polarity=NRF_GPIOTE_POLARITY_TOGGLE,
        .init_val=NRF_GPIOTE_INITIAL_VALUE_LOW};
    if((err=nrfx_gpiote_output_configure(gpiote,LED,&output,&led_task)))return err;
    nrfx_gpiote_out_task_enable(gpiote,GATE);
    nrfx_gpiote_out_task_enable(gpiote,LED);
    uint32_t on_task=ACTIVE_LOW?nrfx_gpiote_clr_task_address_get(gpiote,GATE):nrfx_gpiote_set_task_address_get(gpiote,GATE);
    uint32_t on_event=nrfx_timer_compare_event_address_get(&pulse,NRF_TIMER_CC_CHANNEL0);
    if((err=nrfx_gppi_conn_alloc(on_event,on_task,&on_route))||
       (err=nrfx_gppi_conn_alloc(nrfx_timer_compare_event_address_get(&pulse,NRF_TIMER_CC_CHANNEL1),gate_off_task(),&off_route)))return err;
    if((err=nrfx_gppi_group_alloc(nrfx_gppi_domain_id_get(on_event),&group)))return err;group_ready=true;
    if((err=nrfx_gppi_group_ep_add(group,on_event)))return err;
    uint32_t led_on_event=nrfx_timer_compare_event_address_get(&led_carrier,NRF_TIMER_CC_CHANNEL0);
    uint32_t led_off_event=nrfx_timer_compare_event_address_get(&led_carrier,NRF_TIMER_CC_CHANNEL1);
    /* Nested 100 kHz PWM. The gate ON event also starts TIMER1 through the
     * same PPI channel; the envelope OFF event independently clears LED2. */
    if((err=nrfx_gppi_ep_attach(nrfx_timer_task_address_get(&led_carrier,NRF_TIMER_TASK_START),on_route))||
       (err=nrfx_gppi_ep_attach(led_off_task(),off_route))||
       (err=nrfx_gppi_conn_alloc(nrfx_timer_compare_event_address_get(&pulse,NRF_TIMER_CC_CHANNEL1),
                               nrfx_timer_task_address_get(&led_carrier,NRF_TIMER_TASK_STOP),&led_stop_route))||
       (err=nrfx_gppi_ep_attach(nrfx_timer_task_address_get(&led_carrier,NRF_TIMER_TASK_CLEAR),led_stop_route))||
       (err=nrfx_gppi_conn_alloc(led_on_event,nrfx_gpiote_set_task_address_get(gpiote,LED),&led_on_route))||
       (err=nrfx_gppi_conn_alloc(led_off_event,led_off_task(),&led_off_route))||
       (err=nrfx_gppi_group_ep_add(group,led_on_event))||
       (err=nrfx_gppi_group_ep_add(group,led_off_event)))return err;
    /* Each local deadline disables all future ON events AND asserts OFF in HW. */
    if((err=nrfx_gppi_conn_alloc(nrfx_timer_compare_event_address_get(&duration,NRF_TIMER_CC_CHANNEL0),nrfx_gppi_group_task_dis_addr(group),&end_route))||
       (err=nrfx_gppi_ep_attach(gate_off_task(),end_route))||
       (err=nrfx_gppi_conn_alloc(nrfx_timer_compare_event_address_get(&lease,NRF_TIMER_CC_CHANNEL0),nrfx_gppi_group_task_dis_addr(group),&lease_route))||
       (err=nrfx_gppi_ep_attach(gate_off_task(),lease_route)))return err;
    /* nRF52 PPI has only two task endpoints per channel. Each deadline needs
     * another channel for TIMER1 STOP + LED CLR; never overfill the gate route. */
    if((err=nrfx_gppi_conn_alloc(nrfx_timer_compare_event_address_get(&duration,NRF_TIMER_CC_CHANNEL0),
                               nrfx_timer_task_address_get(&led_carrier,NRF_TIMER_TASK_STOP),&led_end_route))||
       (err=nrfx_gppi_ep_attach(led_off_task(),led_end_route))||
       (err=nrfx_gppi_conn_alloc(nrfx_timer_compare_event_address_get(&lease,NRF_TIMER_CC_CHANNEL0),
                               nrfx_timer_task_address_get(&led_carrier,NRF_TIMER_TASK_STOP),&led_lease_route))||
       (err=nrfx_gppi_ep_attach(led_off_task(),led_lease_route)))return err;
    routes=true;rx_hw_off();
    IRQ_CONNECT(DT_IRQN(DT_NODELABEL(pwm0)),DT_IRQ(DT_NODELABEL(pwm0),priority),nrfx_pwm_irq_handler,&pwm,0);
#if NRF_ERRATA_STATIC_CHECK(52,109)
    IRQ_CONNECT(DT_IRQN(DT_NODELABEL(egu4)),DT_IRQ(DT_NODELABEL(egu4),priority),nrfx_pwm_nrf52_anomaly_109_handler,&pwm,0);
    BUILD_ASSERT(NRFX_PWM_NRF52_ANOMALY_109_EGU_INSTANCE==4);
#endif
    nrfx_pwm_config_t pc=NRFX_PWM_DEFAULT_CONFIG(ISET,NRF_PWM_PIN_NOT_CONNECTED,NRF_PWM_PIN_NOT_CONNECTED,NRF_PWM_PIN_NOT_CONNECTED);
    pc.irq_priority=DT_IRQ(DT_NODELABEL(pwm0),priority);pc.base_clock=NRF_PWM_CLK_16MHz;pc.top_value=TOP;
    if((err=nrfx_pwm_init(&pwm,&pc,NULL,NULL)))return err;
    nrfx_pwm_simple_playback(&pwm,&sequence,1,NRFX_PWM_FLAG_LOOP);ready=true;return 0;
}
int rx_hw_apply(struct stim_params *s) {
    if(!ready||!RX_ISET_FULL_SCALE_UA)return -EACCES;
    rx_hw_off();
    nrf_timer_event_clear(NRF_TIMER3,NRF_TIMER_EVENT_COMPARE0);nrf_timer_event_clear(NRF_TIMER4,NRF_TIMER_EVENT_COMPARE0);
    uint32_t ticks=(uint32_t)s->current_ua*TOP/RX_ISET_FULL_SCALE_UA;
    if(!ticks||ticks>TOP)return -EINVAL;
    duty=BIT(15)|ticks;__DMB();s->current_ua=(ticks*RX_ISET_FULL_SCALE_UA+TOP/2)/TOP;return 0;
}
void rx_hw_keepalive(void) {
    if(!ready)return;
    nrfx_timer_disable(&lease);nrfx_timer_clear(&lease);nrf_timer_event_clear(NRF_TIMER4,NRF_TIMER_EVENT_COMPARE0);
    nrfx_timer_extended_compare(&lease,NRF_TIMER_CC_CHANNEL0,RX_LEASE_MS*1000U,NRF_TIMER_SHORT_COMPARE0_STOP_MASK,false);
    nrfx_gppi_conn_enable(lease_route);nrfx_gppi_conn_enable(led_lease_route);nrfx_timer_enable(&lease);
}
int rx_hw_start(const struct stim_params *s) {
    if(!ready||!routes||!s->frequency_mhz)return -EACCES;
    uint64_t exact=(1000000000ULL+s->frequency_mhz/2)/s->frequency_mhz;
    if(exact>UINT32_MAX||s->width_us+2U>=exact)return -EINVAL;
    uint32_t period=exact;
    nrfx_timer_disable(&pulse);nrfx_timer_clear(&pulse);
    nrfx_timer_extended_compare(&pulse,NRF_TIMER_CC_CHANNEL0,1,0,false);
    nrfx_timer_extended_compare(&pulse,NRF_TIMER_CC_CHANNEL1,s->width_us+1U,0,false);
    nrfx_timer_extended_compare(&pulse,NRF_TIMER_CC_CHANNEL2,period,NRF_TIMER_SHORT_COMPARE2_CLEAR_MASK,false);
    nrfx_timer_disable(&led_carrier);nrfx_timer_clear(&led_carrier);
    /* One 16 MHz tick offsets SET from the integer-us envelope OFF edge.
     * The high width is exactly the same quantized duty as ISET. At TOP duty,
     * CC1=TOP+1 is never reached, giving solid ON within each envelope. */
    nrfx_timer_extended_compare(&led_carrier,NRF_TIMER_CC_CHANNEL0,1,0,false);
    nrfx_timer_extended_compare(&led_carrier,NRF_TIMER_CC_CHANNEL1,1U+(duty&0x7fffU),0,false);
    nrfx_timer_extended_compare(&led_carrier,NRF_TIMER_CC_CHANNEL2,TOP,NRF_TIMER_SHORT_COMPARE2_CLEAR_MASK,false);
    nrfx_timer_disable(&duration);nrfx_timer_clear(&duration);nrf_timer_event_clear(NRF_TIMER3,NRF_TIMER_EVENT_COMPARE0);
    nrfx_timer_extended_compare(&duration,NRF_TIMER_CC_CHANNEL0,s->duration_ms*1000U,NRF_TIMER_SHORT_COMPARE0_STOP_MASK,false);
    nrfx_gppi_conn_enable(off_route);nrfx_gppi_conn_enable(end_route);nrfx_gppi_conn_enable(lease_route);
    nrfx_gppi_conn_enable(led_stop_route);nrfx_gppi_conn_enable(led_end_route);nrfx_gppi_conn_enable(led_lease_route);
    nrfx_timer_enable(&duration);nrfx_gppi_group_enable(group);nrfx_timer_enable(&pulse);return 0;
}
bool rx_hw_expired(void){return nrf_timer_event_check(NRF_TIMER3,NRF_TIMER_EVENT_COMPARE0)||nrf_timer_event_check(NRF_TIMER4,NRF_TIMER_EVENT_COMPARE0);}
#else
/* No GPIO guesses: this build is a BLE/parameter-development receiver only. */
int rx_hw_init(void){return -ENOTSUP;}
int rx_hw_apply(struct stim_params *s){ARG_UNUSED(s);return -EACCES;}
int rx_hw_start(const struct stim_params *s){ARG_UNUSED(s);return -EACCES;}
void rx_hw_keepalive(void){}
void rx_hw_off(void){}
bool rx_hw_expired(void){return false;}
#endif
