#include "rx_stim.h"
#include "rx_service.h"
#include <zephyr/kernel.h>
#include <zephyr/fatal.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/sys/reboot.h>
static void expired(const struct device *dev,int channel){ARG_UNUSED(dev);ARG_UNUSED(channel);rx_stim_emergency_off();}
void k_sys_fatal_error_handler(unsigned int reason,const struct arch_esf *esf) {
    ARG_UNUSED(reason);ARG_UNUSED(esf);rx_stim_emergency_off();sys_reboot(SYS_REBOOT_COLD);CODE_UNREACHABLE;
}
int main(void) {
    /* Hardware initialization failures are reported over the RX status link. */
    (void)rx_stim_init();
    const struct device *wdt=DEVICE_DT_GET(DT_NODELABEL(wdt0));
    if(!device_is_ready(wdt))goto failed;
    const struct wdt_timeout_cfg cfg={.window={.min=0,.max=2000},.callback=expired,.flags=WDT_FLAG_RESET_SOC};
    int channel=wdt_install_timeout(wdt,&cfg);
    /* SWD flash/verify halts the CPU. Pause WDT only while debugger-halted;
     * the normal runtime watchdog remains active, including during sleep. */
    if(channel<0||wdt_setup(wdt,WDT_OPT_PAUSE_HALTED_BY_DBG))goto failed;
    if(rx_service_init())goto failed;
    while(1){rx_service_poll();rx_stim_poll();if(wdt_feed(wdt,channel))goto failed;k_sleep(K_MSEC(20));}
failed:
    rx_stim_emergency_off();k_panic();return 0;
}
