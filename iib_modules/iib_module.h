/*
 * iib_module.h
 *
 *  Created on: 26 de fev de 2019
 *      Author: allef.silva
 */

#ifndef IIB_MODULE_H_
#define IIB_MODULE_H_

#include<stdint.h>

typedef struct {
    void (*configure_module) (void);
    void (*clear_interlocks) (void);
    void (*check_interlocks) (void);
    void (*clear_alarms) (void);
    void (*check_alarms) (void);
    void (*check_indication_leds) (void);
    void (*application_readings) (void);
    void (*map_vars) (void);
    void (*send_data) (void);
    void (*send_itlk_msg) (void);

} iib_module_t;

#endif /* IIB_MODULE_H_ */