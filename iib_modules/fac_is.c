/******************************************************************************
 * Copyright (C) 2017 by LNLS - Brazilian Synchrotron Light Laboratory
 *
 * Redistribution, modification or use of this software in source or binary
 * forms is permitted as long as the files maintain this copyright. LNLS and
 * the Brazilian Center for Research in Energy and Materials (CNPEM) are not
 * liable for any misuse of this material.
 *
 *****************************************************************************/

/**
 * @file fac_is.c
 * @brief Brief description of module
 * 
 * Detailed description
 *
 * @author allef.silva
 * @date 20 de out de 2018
 *
 */

#include <iib_modules/fac_is.h>
#include "iib_data.h"

#include "adc_internal.h"
#include "application.h"

#include "BoardTempHum.h"
#include "pt100.h"
#include "output.h"
#include "leds.h"
#include "can_bus.h"
#include "input.h"

#include <stdbool.h>
#include <stdint.h>

#define FAC_IS_INPUT_OVERCURRENT_ALM            160.0
#define FAC_IS_INPUT_OVERCURRENT_ITLK           170.0
#define FAC_IS_DCLINK_OVERVOLTAGE_ALM           550.0
#define FAC_IS_DCLINK_OVERVOLTAGE_ITLK          555.0
#define FAC_IS_HS_OVERTEMP_ALM                  45.0
#define FAC_IS_HS_OVERTEMP_ITLK                 50.0
#define FAC_IS_INDUC_OVERTEMP_ALM               55.0
#define FAC_IS_INDUC_OVERTEMP_ITLK              60.0
#define FAC_IS_RH_ALM                           80.0
#define FAC_IS_RH_ITLK                          90.0
#define FAC_IS_BOARD_TEMP_ALM                   80.0
#define FAC_IS_BOARD_TEMP_ITLK                  90.0

typedef struct
{
    union {
        float   f;
        uint8_t u8[4];
    } Iin;

    bool IinAlarmSts;
    bool IinItlkSts;

    union {
        float   f;
        uint8_t u8[4];
    } VdcLink;

    bool VdcLinkAlarmSts;
    bool VdcLinkItlkSts;

    union {
        float   f;
        uint8_t u8[4];
    } TempHeatsink;

    bool TempHeatsinkAlarmSts;
    bool TempHeatsinkItlkSts;

    union {
        float   f;
        uint8_t u8[4];
    } TempL;

    bool TempLAlarmSts;
    bool TempLItlkSts;
    bool Driver1Error;
    bool Driver1ErrorItlk;
    bool Driver2Error;
    bool Driver2ErrorItlk;

} fac_is_t;

/**
 * TODO: Put here your defines. Just what is local. If you don't
 * need to access it from other module, consider use a constant (const)
 */


/**
 * TODO: Put here your constants and variables. Always use static for 
 * private members.
 */
fac_is_t fac_is;
uint32_t im_interlocks_indication = 0;
uint32_t im_alarms_indication = 0;

static uint32_t itlk_id;
static uint32_t alarm_id;

static void get_itlks_id();
static void get_alarms_id();

/**
 * TODO: Put here your function prototypes for private functions. Use
 * static in declaration.
 */

void init_fac_is()
{
    //Set Current Range
    CurrentCh1Init(300.0, 0.150, 50.0, 10);    // INPUT CURRENT

    //Set Protection Limits
    CurrentCh1AlarmLevelSet(FAC_IS_INPUT_OVERCURRENT_ALM); // INPUT CURRENT ALARM LEVEL
    CurrentCh1TripLevelSet(FAC_IS_INPUT_OVERCURRENT_ITLK); // INPUT CURRENT TRIP LEVEL

    //LV20-P INPUTS
    LvCurrentCh1Init(555.0, 0.025, 120.0, 10); // CONFIG CHANNEL FOR DC_LINK MEASURE

    //LV20-P LIMITS
    CurrentCh1AlarmLevelSet(FAC_IS_DCLINK_OVERVOLTAGE_ALM); // INPUT DC_LINK VOLTAGE ALARM LEVEL
    CurrentCh1TripLevelSet(FAC_IS_DCLINK_OVERVOLTAGE_ITLK); // INPUT DC_LINK VOLTAGE TRIP LEVEL

    // PT100 configuration limits
    Pt100SetCh1AlarmLevel(FAC_IS_HS_OVERTEMP_ALM); // HEATSINK TEMPERATURE ALARM LEVEL
    Pt100SetCh1TripLevel(FAC_IS_HS_OVERTEMP_ITLK); // HEATSINK TEMPERATURE TRIP LEVEL
    Pt100SetCh2AlarmLevel(FAC_IS_INDUC_OVERTEMP_ALM); // INDUCTOR TEMPERATURE ALARM LEVEL
    Pt100SetCh2TripLevel(FAC_IS_INPUT_OVERCURRENT_ITLK); // INDUCTOR TEMPERATURE TRIP LEVEL

    // PT100 channel enable
    Pt100Ch1Enable();                     // HEATSINK TEMPERATURE CHANNEL ENABLE
    Pt100Ch2Enable();                     // INDUCTOR TEMPERATURE CHANNEL ENABLE
    Pt100Ch3Disable();
    Pt100Ch4Disable();

    // Delay 4 seconds
    Pt100SetCh1Delay(4);
    // Delay 4 seconds
    Pt100SetCh2Delay(4);
    // Delay 4 seconds
    Pt100SetCh3Delay(4);
    // Delay 4 seconds
    Pt100SetCh4Delay(4);

    // Rh configuration limits
    RhAlarmLimitSet(FAC_IS_RH_ALM);
    RhTripLimitSet(FAC_IS_RH_ITLK);

    // Temp board configuration limits
    TempBoardAlarmLimitSet(FAC_IS_BOARD_TEMP_ALM);
    TempBoardTripLimitSet(FAC_IS_BOARD_TEMP_ITLK);

    // Disable all Driver Error Monitoring
    Driver1ErrDisable();
    Driver2ErrDisable();

    // Init Variables
    fac_is.Iin.f                      = 0.0;
    fac_is.IinAlarmSts                = 0;
    fac_is.IinItlkSts                 = 0;

    fac_is.VdcLink.f                  = 0.0;
    fac_is.VdcLinkAlarmSts            = 0;
    fac_is.VdcLinkItlkSts             = 0;

    fac_is.TempHeatsink.f             = 0.0;
    fac_is.TempHeatsinkAlarmSts       = 0;
    fac_is.TempHeatsinkItlkSts        = 0;

    fac_is.TempL.f                    = 0.0;
    fac_is.TempLAlarmSts              = 0;
    fac_is.TempLItlkSts               = 0;

    fac_is.Driver1Error               = 0;
    fac_is.Driver1ErrorItlk           = 0;

    fac_is.Driver2Error               = 0;
    fac_is.Driver2ErrorItlk           = 0;
}

void clear_fac_is_interlocks()
{
    fac_is.IinItlkSts                = 0;
    fac_is.VdcLinkItlkSts            = 0;
    fac_is.TempHeatsinkItlkSts       = 0;
    fac_is.TempLItlkSts              = 0;
    fac_is.Driver1ErrorItlk          = 0;
    fac_is.Driver2ErrorItlk          = 0;

    itlk_id = 0;
}

uint8_t check_fac_is_interlocks()
{
    uint8_t test = 0;

    test |= fac_is.IinItlkSts;
    test |= fac_is.VdcLinkItlkSts;
    test |= fac_is.TempHeatsinkItlkSts;
    test |= fac_is.TempLItlkSts;
    test |= fac_is.Driver1ErrorItlk;
    test |= fac_is.Driver2ErrorItlk;

    return test;
}

void clear_fac_is_alarms()
{
    fac_is.IinAlarmSts               = 0;
    fac_is.VdcLinkAlarmSts           = 0;
    fac_is.TempHeatsinkAlarmSts      = 0;
    fac_is.TempLAlarmSts             = 0;

    alarm_id = 0;
}

uint8_t check_fac_is_alarms()
{
    uint8_t test = 0;

    test |= fac_is.IinAlarmSts;
    test |= fac_is.TempHeatsinkAlarmSts;
    test |= fac_is.TempLAlarmSts;
    test |= fac_is.VdcLinkAlarmSts;

    return test;
}

void check_fac_is_indication_leds()
{
    // Input Over Current
    if(fac_is.IinItlkSts) Led2TurnOff();
    else if(fac_is.IinAlarmSts) Led2Toggle();
    else Led2TurnOn();

    // Dc-Link Overvoltage
    if(fac_is.VdcLinkItlkSts) Led3TurnOff();
    else if(fac_is.VdcLinkAlarmSts) Led3Toggle();
    else Led3TurnOn();

    // Heatsink Over Temperature
    if(fac_is.TempHeatsinkItlkSts) Led4TurnOff();
    else if(fac_is.TempHeatsinkAlarmSts) Led4Toggle();
    else Led4TurnOn();

    // Inductor Over Temperature
    if(fac_is.TempLItlkSts) Led5TurnOff();
    else if(fac_is.TempLAlarmSts) Led5Toggle();
    else Led5TurnOn();

    // Driver Error
    if(fac_is.Driver1ErrorItlk || fac_is.Driver2ErrorItlk) Led6TurnOff();
    else Led6TurnOn();
}

void fac_is_application_readings()
{
    fac_is.Iin.f = CurrentCh1Read();
    fac_is.IinAlarmSts = CurrentCh1AlarmStatusRead();
    if(!fac_is.IinItlkSts) fac_is.IinItlkSts                    = CurrentCh1TripStatusRead();

    fac_is.VdcLink.f = LvCurrentCh1Read();
    fac_is.VdcLinkAlarmSts = CurrentCh1AlarmStatusRead();
    if(!fac_is.VdcLinkItlkSts) fac_is.VdcLinkItlkSts            = CurrentCh1TripStatusRead();

    fac_is.TempHeatsink.f = (float) Pt100ReadCh1();
    fac_is.TempHeatsinkAlarmSts = Pt100ReadCh1AlarmSts();
    if(!fac_is.TempHeatsinkItlkSts) fac_is.TempHeatsinkItlkSts  = Pt100ReadCh1TripSts();

    fac_is.TempL.f = (float) Pt100ReadCh2();
    fac_is.TempLAlarmSts = Pt100ReadCh2AlarmSts();
    if(!fac_is.TempLItlkSts) fac_is.TempLItlkSts                = Pt100ReadCh2TripSts();

    fac_is_map_vars();
    get_itlks_id();
    get_alarms_id();
}

void fac_is_map_vars()
{
    g_controller_iib.iib_signals[0].u32     = im_interlocks_indication;
    g_controller_iib.iib_signals[1].u32     = im_alarms_indication;
    g_controller_iib.iib_signals[2].f       = fac_is.Iin.f;
    g_controller_iib.iib_signals[3].f       = fac_is.VdcLink.f;
    g_controller_iib.iib_signals[4].f       = fac_is.TempL.f;
    g_controller_iib.iib_signals[5].f       = fac_is.TempHeatsink.f;
}

void send_fac_is_data()
{
    uint8_t i;
    for (i = 2; i < 6; i++) send_data_message(i);
}

static void get_itlks_id()
{
    if (fac_is.IinItlkSts)           itlk_id |= INPUT_OVERCURRENT_ITLK;
    if (fac_is.VdcLinkItlkSts)       itlk_id |= DCLINK_OVERVOLTAGE_ITLK;
    if (fac_is.TempHeatsinkItlkSts)  itlk_id |= HS_OVERTEMP_ITLK;
    if (fac_is.TempLItlkSts)         itlk_id |= INDUC_OVERTEMP_ITLK;
    if (fac_is.Driver1ErrorItlk)     itlk_id |= DRIVER1_ERROR_ITLK;
    if (fac_is.Driver2ErrorItlk)     itlk_id |= DRIVER2_ERROR_ITLK;
}

static void get_alarms_id()
{
    if (fac_is.IinAlarmSts)          alarm_id |= INPUT_OVERCURRENT_ALM;
    if (fac_is.VdcLinkAlarmSts)      alarm_id |= DCLINK_OVERVOLTAGE_ALM;
    if (fac_is.TempHeatsinkAlarmSts) alarm_id |= HS_OVERTEMP_ALM;
    if (fac_is.TempLAlarmSts)        alarm_id |= INDUC_OVERTEMP_ALM;
}

void send_fac_is_itlk_msg()
{
    send_data_message(0);
}

float fac_is_iin_read(void)
{
    return fac_is.Iin.f;
}

unsigned char fac_is_iin_alarm_sts_read(void)
{
    return fac_is.IinAlarmSts;
}

unsigned char fac_is_iin_itlk_sts_read(void)
{
    return fac_is.IinItlkSts;
}

//******************************************************************************
float fac_is_vdclink_read(void)
{
    return fac_is.VdcLink.f;
}

unsigned char fac_is_vdclink_alarm_sts_read(void)
{
    return fac_is.VdcLinkAlarmSts;
}

unsigned char fac_is_vdclink_itlk_sts_read(void)
{
    return fac_is.VdcLinkItlkSts;
}

//******************************************************************************
float fac_is_temp_heatsink_read(void)
{
    return fac_is.TempHeatsink.f;
}

unsigned char fac_is_temp_heatsink_alarm_sts_read(void)
{
    return fac_is.TempHeatsinkAlarmSts;
}

unsigned char fac_is_temp_heatsink_itlk_sts_read(void)
{
    return fac_is.TempHeatsinkItlkSts;
}

//******************************************************************************
float fac_is_tempL_read(void)
{
    return fac_is.TempL.f;
}

unsigned char fac_is_tempL_alarm_sts_read(void)
{
    return fac_is.TempLAlarmSts;
}

unsigned char fac_is_tempL_itlk_sts_read(void)
{
    return fac_is.TempLItlkSts;
}
