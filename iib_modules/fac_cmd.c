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
 * @file fac_cmd.c
 * @brief Brief description of module
 * 
 * Detailed description
 *
 * @author allef.silva
 * @date 20 de out de 2018
 *
 */

#include <iib_modules/fac_cmd.h>
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

#define FAC_CMD_CAPBANK_OVERVOLTAGE_ITLK_LIM        300.0
#define FAC_CMD_CAPBANK_OVERVOLTAGE_ALM_LIM         250.0
#define FAC_CMD_OUTPUT_OVERVOLTAGE_ITLK_LIM         210.0
#define FAC_CMD_OUTPUT_OVERVOLTAGE_ALM_LIM          180.0
#define FAC_CMD_HS_OVERTEMP_ITLK_LIM                60.0
#define FAC_CMD_HS_OVERTEMP_ALM_LIM                 55.0
#define FAC_CMD_INDUC_OVERTEMP_ITLK_LIM             60.0
#define FAC_CMD_INDUC_OVERTEMP_ALM_LIM              55.0

typedef struct
{

    union {
        float       f;
        uint8_t     u8[4];
    }TempHeatSink;

    bool TempHeatSinkAlarmSts;
    bool TempHeatSinkItlkSts;

    union {
        float       f;
        uint8_t     u8[4];
    }TempL;

    bool TempLAlarmSts;
    bool TempLItlkSts;

    union {
        float       f;
        uint8_t     u8[4];
    }VcapBank;

    bool VcapBankAlarmSts;
    bool VcapBankItlkSts;

    union {
        float       f;
        uint8_t     u8[4];
    }Vout;

    bool VoutAlarmSts;
    bool VoutItlkSts;
    bool ExtItlkSts;
    bool ExtItlk2Sts;

} fac_cmd_t;

fac_cmd_t fac_cmd;
uint32_t fac_cmd_interlocks_indication   = 0;
uint32_t fac_cmd_alarms_indication       = 0;

static uint32_t itlk_id = 0;
static uint32_t alarm_id = 0;

static void get_itlks_id();
static void get_alarms_id();
static void map_vars();
static void config_module();

/**
 * TODO: Put here the implementation for your public functions.
 */

void init_fac_cmd()
{
    config_module();
}

void clear_fac_cmd_interlocks()
{
    fac_cmd.VcapBankItlkSts         = 0;
    fac_cmd.VoutItlkSts             = 0;
    fac_cmd.TempHeatSinkItlkSts     = 0;
    fac_cmd.TempLItlkSts            = 0;
    fac_cmd.ExtItlkSts              = 0;
    fac_cmd.ExtItlk2Sts             = 0;

    itlk_id = 0;
}

uint8_t check_fac_cmd_interlocks()
{
    uint8_t test = 0;

    test |= fac_cmd.TempHeatSinkItlkSts;
    test |= fac_cmd.TempLItlkSts;
    test |= fac_cmd.VcapBankItlkSts;
    test |= fac_cmd.VoutItlkSts;
    test |= fac_cmd.ExtItlkSts;
    test |= fac_cmd.ExtItlk2Sts;

    return test;
}

void clear_fac_cmd_alarms()
{
    fac_cmd.VcapBankAlarmSts        = 0;
    fac_cmd.VoutAlarmSts            = 0;
    fac_cmd.TempHeatSinkAlarmSts    = 0;
    fac_cmd.TempLAlarmSts           = 0;

    alarm_id = 0;
}

uint8_t check_fac_cmd_alarms()
{
    uint8_t test = 0;

    test |= fac_cmd.TempHeatSinkAlarmSts;
    test |= fac_cmd.TempLAlarmSts;
    test |= fac_cmd.VcapBankAlarmSts;
    test |= fac_cmd.VoutAlarmSts;

    return 0;
}

void check_fac_cmd_indication_leds()
{
    if (fac_cmd.VcapBankItlkSts) Led2TurnOff();
    else if (fac_cmd.VcapBankAlarmSts) Led2Toggle();
    else Led2TurnOn();

    if (fac_cmd.VoutItlkSts) Led3TurnOff();
    else if (fac_cmd.VoutAlarmSts) Led3Toggle();
    else Led3TurnOn();

    if (fac_cmd.TempHeatSinkItlkSts) Led4TurnOff();
    else if (fac_cmd.TempHeatSinkAlarmSts) Led4Toggle();
    else Led4TurnOn();

    if (fac_cmd.TempLItlkSts) Led5TurnOff();
    else if (fac_cmd.TempLAlarmSts) Led5Toggle();
    else Led5TurnOn();

    if (fac_cmd.ExtItlkSts) Led6TurnOff();
    else Led6TurnOn();

    if (fac_cmd.ExtItlk2Sts) Led7TurnOff();
    else Led7TurnOn();
}

void fac_cmd_application_readings()
{
    fac_cmd.TempHeatSink.f = (float) Pt100ReadCh1();
    fac_cmd.TempHeatSinkAlarmSts = Pt100ReadCh1AlarmSts();
    if (!fac_cmd.TempHeatSinkItlkSts) fac_cmd.TempHeatSinkItlkSts = Pt100ReadCh1TripSts();

    fac_cmd.TempL.f = (float) Pt100ReadCh2();
    fac_cmd.TempLAlarmSts = Pt100ReadCh2AlarmSts();
    if (!fac_cmd.TempLItlkSts) fac_cmd.TempLItlkSts = Pt100ReadCh2TripSts();

    fac_cmd.VcapBank.f = VoltageCh1Read();
    fac_cmd.VcapBankAlarmSts = VoltageCh1AlarmStatusRead();
    if (!fac_cmd.VcapBankItlkSts) fac_cmd.VcapBankItlkSts = VoltageCh1TripStatusRead();

    fac_cmd.Vout.f = VoltageCh2Read();
    fac_cmd.VoutAlarmSts = VoltageCh2AlarmStatusRead();
    if (!fac_cmd.VoutItlkSts) fac_cmd.VoutItlkSts = VoltageCh2TripStatusRead();

    if(!fac_cmd.ExtItlkSts) fac_cmd.ExtItlkSts = Gpdi5Read();

    if(!fac_cmd.ExtItlk2Sts) fac_cmd.ExtItlk2Sts = Gpdi6Read();

    map_vars();

    get_alarms_id();
    get_itlks_id();
}

void fac_cmd_power_on_check()
{
    Led1TurnOn();
}

static void map_vars()
{
    g_controller_iib.iib_signals[0].u32     = fac_cmd_interlocks_indication;
    g_controller_iib.iib_signals[1].u32     = fac_cmd_alarms_indication;
    g_controller_iib.iib_signals[2].f       = fac_cmd.VcapBank.f;
    g_controller_iib.iib_signals[3].f       = fac_cmd.Vout.f;
    g_controller_iib.iib_signals[4].f       = fac_cmd.TempL.f;
    g_controller_iib.iib_signals[5].f       = fac_cmd.TempHeatSink.f;
}

void send_fac_cmd_data()
{
    //uint8_t i;
    //
    //for (i = 2; i < 6; i++) send_data_message(i);
    static uint8_t i = 2;

    send_data_message(i);

    i++;

    if (i > 5) i = 2;
}

static void get_itlks_id()
{
    if (fac_cmd.VcapBankItlkSts)        itlk_id |= FAC_CMD_CAPBANK_OVERVOLTAGE_ITLK;
    if (fac_cmd.VoutItlkSts)            itlk_id |= FAC_CMD_OUTPUT_OVERVOLTAGE_ITLK;
    if (fac_cmd.TempHeatSinkItlkSts)    itlk_id |= FAC_CMD_HS_OVERTEMP_ITLK;
    if (fac_cmd.TempLItlkSts)           itlk_id |= FAC_CMD_INDUC_OVERTEMP_ITLK;
    if (fac_cmd.ExtItlkSts)             itlk_id |= FAC_CMD_EXTERNAL1_ITLK;
    if (fac_cmd.ExtItlk2Sts)            itlk_id |= FAC_CMD_EXTERNAL2_ITLK;
}

static void get_alarms_id()
{
    if (fac_cmd.VcapBankAlarmSts)     alarm_id |= FAC_CMD_CAPBANK_OVERVOLTAGE_ALM;
    if (fac_cmd.VoutItlkSts)          alarm_id |= FAC_CMD_OUTPUT_OVERVOLTAGE_ALM;
    if (fac_cmd.TempHeatSinkAlarmSts) alarm_id |= FAC_CMD_HS_OVERTEMP_ALM;
    if (fac_cmd.TempLAlarmSts)        alarm_id |= FAC_CMD_INDUC_OVERTEMP_ALM;
}

void send_fac_cmd_itlk_msg()
{
    send_data_message(0);
}

static void config_module()
{
    //Setar ranges de entrada
    VoltageCh1Init(330.0, 3);                 // Capacitors Voltage Configuration.
    VoltageCh2Init(250.0, 3);                 // Output Voltage Configuration.

    ConfigVoltCh1AsNtc(0);                 // Config Voltage Ch1 as a voltage input
    ConfigVoltCh2AsNtc(0);                 // Config Voltage Ch2 as a voltage input

    //Setar limites
    VoltageCh1AlarmLevelSet(FAC_CMD_CAPBANK_OVERVOLTAGE_ALM_LIM); // Rectifier1 Voltage Alarm
    VoltageCh1TripLevelSet(FAC_CMD_CAPBANK_OVERVOLTAGE_ITLK_LIM); // Rectifier1 Voltage Trip
    VoltageCh2AlarmLevelSet(FAC_CMD_OUTPUT_OVERVOLTAGE_ALM_LIM); // Rectifier2 Voltage Alarm
    VoltageCh2TripLevelSet(FAC_CMD_OUTPUT_OVERVOLTAGE_ITLK_LIM); // Rectifier2 Voltage Trip

    // PT100 configuration limits
    Pt100SetCh1AlarmLevel(FAC_CMD_HS_OVERTEMP_ALM_LIM); // HEATSINK TEMPERATURE ALARM LEVEL
    Pt100SetCh1TripLevel(FAC_CMD_HS_OVERTEMP_ITLK_LIM); // HEATSINK TEMPERATURE TRIP LEVEL
    Pt100SetCh2AlarmLevel(FAC_CMD_INDUC_OVERTEMP_ALM_LIM); // INDUCTOR TEMPERATURE ALARM LEVEL
    Pt100SetCh2TripLevel(FAC_CMD_INDUC_OVERTEMP_ITLK_LIM); // INDUCTOR TEMPERATURE TRIP LEVEL

    // PT100 channel enable
    Pt100Ch1Enable();                     // HEATSINK TEMPERATURE CHANNEL ENABLE
    Pt100Ch2Enable();                     // INDUCTOR TEMPERATURE CHANNEL ENABLE
    Pt100Ch3Disable();
    Pt100Ch4Disable();

    Pt100SetCh1Delay(4);
    Pt100SetCh2Delay(4);
    Pt100SetCh3Delay(4);
    Pt100SetCh4Delay(4);

    fac_cmd.VcapBank.f               = 0.0;
    fac_cmd.VcapBankAlarmSts         = 0;
    fac_cmd.VcapBankItlkSts          = 0;

    fac_cmd.Vout.f                   = 0.0;
    fac_cmd.VoutAlarmSts             = 0;
    fac_cmd.VoutItlkSts              = 0;

    fac_cmd.TempHeatSink.f           = 0;
    fac_cmd.TempHeatSinkAlarmSts     = 0;
    fac_cmd.TempHeatSinkItlkSts      = 0;

    fac_cmd.TempL.f                  = 0;
    fac_cmd.TempLAlarmSts            = 0;
    fac_cmd.TempLItlkSts             = 0;

    fac_cmd.ExtItlkSts               = 0;
    fac_cmd.ExtItlk2Sts              = 0;
}

