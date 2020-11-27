
/////////////////////////////////////////////////////////////////////////////////////////////

/*
 * i2c_driver.c
 *
 *  Created on: 05 de jun de 2020
 *      Author: Rogerio Jose Marcondeli
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include "inc/hw_i2c.h"
#include "inc/hw_types.h"
#include "inc/hw_gpio.h"
#include "inc/hw_memmap.h"
#include "inc/hw_ints.h"
#include "inc/hw_types.h"
#include "driverlib/debug.h"
#include "driverlib/gpio.h"
#include "driverlib/sysctl.h"
#include "driverlib/i2c.h"
#include "driverlib/pin_map.h"
#include "driverlib/interrupt.h"
#include "i2c_driver.h"
#include "board_drivers/hardware_def.h"
#include "peripheral_drivers/gpio/gpio_driver.h"
#include "peripheral_drivers/timer/timer.h"

/////////////////////////////////////////////////////////////////////////////////////////////

//initialize I2C module 2
void InitI2C2(void)
{
    // Configure Pins for I2C2 Master Interface
    GPIOPinConfigure(GPIO_PN5_I2C2SCL);
    GPIOPinConfigure(GPIO_PN4_I2C2SDA);

    GPIOPinTypeI2CSCL(GPIO_PORTN_BASE, GPIO_PIN_5);
    GPIOPinTypeI2C(GPIO_PORTN_BASE, GPIO_PIN_4);

    // Disable I2C2 peripheral
    SysCtlPeripheralDisable(SYSCTL_PERIPH_I2C2);

    // Reset I2C2 peripheral
    SysCtlPeripheralReset(SYSCTL_PERIPH_I2C2);

    // Enable I2C2 peripheral
    SysCtlPeripheralEnable(SYSCTL_PERIPH_I2C2);

    // Wait for the I2C2 module to be ready.
    while(!SysCtlPeripheralReady(SYSCTL_PERIPH_I2C2));

    // Disable the I2C2 module.
    I2CMasterDisable(I2C2_BASE);

    // Initialize and Configure the Master Module
    // false = 100Khz, true = 400Khz.
    I2CMasterInitExpClk(I2C2_BASE, SysCtlClockGetTM4C129(), true);

    // Enable the Glitch Filter
    I2CMasterGlitchFilterConfigSet(I2C2_BASE, I2C_MASTER_GLITCH_FILTER_8);

}

/////////////////////////////////////////////////////////////////////////////////////////////

//initialize I2C module 5
void InitI2C5(void)
{
    // Configure Pins for I2C5 Master Interface
    GPIOPinConfigure(GPIO_PB0_I2C5SCL);
    GPIOPinConfigure(GPIO_PB1_I2C5SDA);

    GPIOPinTypeI2CSCL(GPIO_PORTB_BASE, GPIO_PIN_0);
    GPIOPinTypeI2C(GPIO_PORTB_BASE, GPIO_PIN_1);

    // Disable I2C5 peripheral
    SysCtlPeripheralDisable(SYSCTL_PERIPH_I2C5);

    // Reset I2C5 peripheral
    SysCtlPeripheralReset(SYSCTL_PERIPH_I2C5);

    // Enable I2C5 peripheral
    SysCtlPeripheralEnable(SYSCTL_PERIPH_I2C5);

    // Wait for the I2C5 module to be ready.
    while(!SysCtlPeripheralReady(SYSCTL_PERIPH_I2C5));

    // Disable the I2C5 module.
    I2CMasterDisable(I2C5_BASE);

    // Initialize and Configure the Master Module
    // false = 100Khz, true = 400Khz.
    I2CMasterInitExpClk(I2C5_BASE, SysCtlClockGetTM4C129(), true);

    // Enable the Glitch Filter
    I2CMasterGlitchFilterConfigSet(I2C5_BASE, I2C_MASTER_GLITCH_FILTER_8);

}

/////////////////////////////////////////////////////////////////////////////////////////////

//sends an I2C2 command to the specified slave
void I2C2Send16Bits(uint8_t slave_addr, uint8_t num_of_args, ...)
{
    // Tell the master module what address it will place on the bus when
    // communicating with the slave.
    I2CMasterSlaveAddrSet(I2C2_BASE, slave_addr, false);

    //stores list of variable number of arguments
    va_list vargs;

    //specifies the va_list to "open" and the last fixed argument
    //so vargs knows where to start looking
    va_start(vargs, num_of_args);

    //put data to be sent into FIFO
    I2CMasterDataPut(I2C2_BASE, va_arg(vargs, uint32_t));

    //if there is only one argument, we only need to use the
    //single send I2C function
    if(num_of_args == 1)
    {
        //Initiate send of data from the MCU
        I2CMasterControl(I2C2_BASE, I2C_MASTER_CMD_SINGLE_SEND);

        // Wait until MCU is done transferring.
        while(!I2CMasterBusy(I2C2_BASE));
        while(I2CMasterBusy(I2C2_BASE));

        //"close" variable argument list
        va_end(vargs);
    }

    //otherwise, we start transmission of multiple bytes on the
    //I2C bus
    else
    {
        //Initiate send of data from the MCU
        I2CMasterControl(I2C2_BASE, I2C_MASTER_CMD_BURST_SEND_START);

        // Wait until MCU is done transferring.
        while(!I2CMasterBusy(I2C2_BASE));
        while(I2CMasterBusy(I2C2_BASE));

        //send num_of_args-2 pieces of data, using the
        //BURST_SEND_CONT command of the I2C module
        unsigned char i;

        for(i = 1; i < (num_of_args - 1); i++)
        {
            //put next piece of data into I2C FIFO
            I2CMasterDataPut(I2C2_BASE, va_arg(vargs, uint32_t));

            //send next data that was just placed into FIFO
            I2CMasterControl(I2C2_BASE, I2C_MASTER_CMD_BURST_SEND_CONT);

            // Wait until MCU is done transferring.
            while(!I2CMasterBusy(I2C2_BASE));
            while(I2CMasterBusy(I2C2_BASE));
        }

        //put last piece of data into I2C FIFO
        I2CMasterDataPut(I2C2_BASE, va_arg(vargs, uint32_t));

        //send next data that was just placed into FIFO
        I2CMasterControl(I2C2_BASE, I2C_MASTER_CMD_BURST_SEND_FINISH);

        // Wait until MCU is done transferring.
        while(!I2CMasterBusy(I2C2_BASE));
        while(I2CMasterBusy(I2C2_BASE));

        //"close" variable args list
        va_end(vargs);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////

//read I2C2 specified register on slave device
uint16_t I2C2Receive16Bits(uint32_t slave_addr, uint8_t reg)
{
    // returns MSB and LSB in a 16-bit word
    uint8_t data[2];

    uint16_t value;

    //specify that we are writing (a register address) to the
    //slave device
    I2CMasterSlaveAddrSet(I2C2_BASE, slave_addr, false);

    //specify register to be read
    I2CMasterDataPut(I2C2_BASE, reg);

    //send control byte and register address byte to slave device
    I2CMasterControl(I2C2_BASE, I2C_MASTER_CMD_BURST_SEND_START);

    // Wait until MCU is done transferring.
    while(!I2CMasterBusy(I2C2_BASE));
    while(I2CMasterBusy(I2C2_BASE));

    //specify that we are going to read from slave device
    I2CMasterSlaveAddrSet(I2C2_BASE, slave_addr, true);

    //send control byte and read from the register we
    //specified
    I2CMasterControl(I2C2_BASE, I2C_MASTER_CMD_BURST_RECEIVE_START);

    // Wait until MCU is done transferring.
    while(!I2CMasterBusy(I2C2_BASE));
    while(I2CMasterBusy(I2C2_BASE));

    //return 1st byte pulled from the specified register
    data[0] = (uint8_t)I2CMasterDataGet(I2C2_BASE);

    I2CMasterControl(I2C2_BASE, I2C_MASTER_CMD_BURST_RECEIVE_FINISH);

    // Wait until MCU is done transferring.
    while(!I2CMasterBusy(I2C2_BASE));
    while(I2CMasterBusy(I2C2_BASE));

    //return 2th byte pulled from the specified register
    data[1] = (uint8_t)I2CMasterDataGet(I2C2_BASE);

    value = (data[0] << 8) | data[1];

    //return data pulled from the specified register
    return value;
}

/////////////////////////////////////////////////////////////////////////////////////////////

//sends an I2C5 command to the specified slave
void I2C5Send(uint8_t slave_addr, uint8_t num_of_args, ...)
{
    // Tell the master module what address it will place on the bus when
    // communicating with the slave.
    I2CMasterSlaveAddrSet(I2C5_BASE, slave_addr, false);

    //stores list of variable number of arguments
    va_list vargs;

    //specifies the va_list to "open" and the last fixed argument
    //so vargs knows where to start looking
    va_start(vargs, num_of_args);

    //put data to be sent into FIFO
    I2CMasterDataPut(I2C5_BASE, va_arg(vargs, uint32_t));

    //if there is only one argument, we only need to use the
    //single send I2C function
    if(num_of_args == 1)
    {
        //Initiate send of data from the MCU
        I2CMasterControl(I2C5_BASE, I2C_MASTER_CMD_SINGLE_SEND);

        // Wait until MCU is done transferring.
        while(!I2CMasterBusy(I2C5_BASE));
        while(I2CMasterBusy(I2C5_BASE));

        //"close" variable argument list
        va_end(vargs);
    }

    //otherwise, we start transmission of multiple bytes on the
    //I2C bus
    else
    {
        //Initiate send of data from the MCU
        I2CMasterControl(I2C5_BASE, I2C_MASTER_CMD_BURST_SEND_START);

        // Wait until MCU is done transferring.
        while(!I2CMasterBusy(I2C5_BASE));
        while(I2CMasterBusy(I2C5_BASE));

        //send num_of_args-2 pieces of data, using the
        //BURST_SEND_CONT command of the I2C module
        unsigned char i;

        for(i = 1; i < (num_of_args - 1); i++)
        {
            //put next piece of data into I2C FIFO
            I2CMasterDataPut(I2C5_BASE, va_arg(vargs, uint32_t));

            //send next data that was just placed into FIFO
            I2CMasterControl(I2C5_BASE, I2C_MASTER_CMD_BURST_SEND_CONT);

            // Wait until MCU is done transferring.
            while(!I2CMasterBusy(I2C5_BASE));
            while(I2CMasterBusy(I2C5_BASE));
        }

        //put last piece of data into I2C FIFO
        I2CMasterDataPut(I2C5_BASE, va_arg(vargs, uint32_t));

        //send next data that was just placed into FIFO
        I2CMasterControl(I2C5_BASE, I2C_MASTER_CMD_BURST_SEND_FINISH);

        // Wait until MCU is done transferring.
        while(!I2CMasterBusy(I2C5_BASE));
        while(I2CMasterBusy(I2C5_BASE));

        //"close" variable args list
        va_end(vargs);
    }
}

/////////////////////////////////////////////////////////////////////////////////////////////

//read I2C5 specified register on slave device
uint32_t I2C5Receive(uint32_t slave_addr, uint8_t reg)
{
    //specify that we are writing (a register address) to the
    //slave device
    I2CMasterSlaveAddrSet(I2C5_BASE, slave_addr, false);

    //specify register to be read
    I2CMasterDataPut(I2C5_BASE, reg);

    //send control byte and register address byte to slave device
    I2CMasterControl(I2C5_BASE, I2C_MASTER_CMD_BURST_SEND_START);

    // Wait until MCU is done transferring.
    while(!I2CMasterBusy(I2C5_BASE));
    while(I2CMasterBusy(I2C5_BASE));

    //specify that we are going to read from slave device
    I2CMasterSlaveAddrSet(I2C5_BASE, slave_addr, true);

    //send control byte and read from the register we
    //specified
    I2CMasterControl(I2C5_BASE, I2C_MASTER_CMD_SINGLE_RECEIVE);

    // Wait until MCU is done transferring.
    while(!I2CMasterBusy(I2C5_BASE));
    while(I2CMasterBusy(I2C5_BASE));

    //return data pulled from the specified register
    return I2CMasterDataGet(I2C5_BASE);
}

/////////////////////////////////////////////////////////////////////////////////////////////





