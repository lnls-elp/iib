
/////////////////////////////////////////////////////////////////////////////////////////////

#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <math.h>
#include "inc/hw_ssi.h"
#include "inc/hw_types.h"
#include "inc/hw_gpio.h"
#include "inc/hw_memmap.h"
#include "driverlib/debug.h"
#include "driverlib/gpio.h"
#include "driverlib/sysctl.h"
#include "driverlib/ssi.h"
#include "driverlib/pin_map.h"
#include "board_drivers/hardware_def.h"
#include "peripheral_drivers/spi/spi.h"
#include "peripheral_drivers/gpio/gpio_driver.h"
#include "peripheral_drivers/timer/timer.h"
#include "pt100.h"
#include "leds.h"

#include <iib_modules/fap.h>
#include <iib_modules/fac_os.h>
#include <iib_modules/fac_is.h>
#include <iib_modules/fac_cmd.h>

#include "application.h"

/////////////////////////////////////////////////////////////////////////////////////////////

// This lib reports a PT100 Temperature value from 0 to 255�C with a resolution of 1�C

/**
 * Configuration of the MAX31865 from MSB to LSB:
 * BIT      FUNCTION            ASSIGNMENT
 *  7       VBIAS               0=OFF            1=ON
 *  6       Conversion Mode     0=Normally OFF   1=AUTO
 *  5       1-Shot              0= -             1=1-Shot
 *  4       3-Wire              0=2- or 4-Wire   1=3-wire
 * 3,2      Faultdetection      set both to 0
 *  1       Fault Status Clear  set to 1
 *  0       50/60Hz filter      0=60Hz           1=50Hz
 */

// Registers defined in Table 1 on page 12 of the data sheet

/////////////////////////////////////////////////////////////////////////////////////////////

static unsigned char Configuration 					= 0x80;
static unsigned char Fault_Status 					= 0x07;
static unsigned char read_Configuration 			= 0x00;
static unsigned char Write_High_Fault_Threshold_MSB = 0x83;
static unsigned char Write_High_Fault_Threshold_LSB = 0x84;
static unsigned char Write_Low_Fault_Threshold_MSB  = 0x85;
static unsigned char Write_Low_Fault_Threshold_LSB	= 0x86;

/////////////////////////////////////////////////////////////////////////////////////////////

/**
 * Callendar-Van Dusen equation is used for temperature linearization.
 * Coeffeicant of equations are as follows:
 * R(T) = R0(1 + aT + bT^2 + c(T - 100)T^3)
 * Equation from : http://www.honeywell-sensor.com.cn/prodinfo/sensor_temperature/technical/c15_136.pdf
 */

static float a = 0.00390830;
static float b = -0.0000005775;

float Reference_Resistor = 400.0; // Reference Resistor installed on the board.
float RTD_Resistance     = 100.0; // RTD Resistance at 0 Degrees. Please refer to your RTD data sheet.

/////////////////////////////////////////////////////////////////////////////////////////////

pt100_t Pt100Ch1;
pt100_t Pt100Ch2;
pt100_t Pt100Ch3;
pt100_t Pt100Ch4;

/////////////////////////////////////////////////////////////////////////////////////////////

// Temperature Range: 0 to 255�C
/**
 * get_Temp() function checks if the fault bit (D0)of LSB RTD regiset is set.
 * If so, the conversion is aborted. If the fault bit is not set,
 * the conversion is initiated. The Digital Code is then computed
 * to a temperature value and printed on the serial console.
 *
 * For linearization, Callendar-Van Dusen equation is used.
 * R(T) = R0(1 + aT + bT^2 + c(T - 100)T^3)
 */

void get_Temp(pt100_t *pt100)
{
	unsigned int msb_rtd;
	unsigned int lsb_rtd;
	unsigned char fault_test = 0;
	float R;
	float Temp;
	float TempT;
	float RTD;

	msb_rtd = read_spi_byte(0x01);
	lsb_rtd = read_spi_byte(0x02);

	fault_test = lsb_rtd & 0x01;

	if(fault_test == 0)
	{
		// Clear RTD out of range flag
		pt100->RtdOutOfRange = 0;

		RTD = ((msb_rtd << 7) + ((lsb_rtd & 0xFE) >> 1)); // Combining RTD_MSB and RTD_LSB to protray decimal value. Removing MSB and LSB during shifting/Anding

		R = (RTD * Reference_Resistor) / 32768; // Conversion of ADC RTD code to resistance

		Temp = -RTD_Resistance * a + sqrt(RTD_Resistance * RTD_Resistance * a * a - 4 * RTD_Resistance * b * (RTD_Resistance - R)); // Conversion of RTD resistance to Temperature

		TempT = Temp / (2 * RTD_Resistance * b);

		if(TempT < 0.0) TempT = 0.0;

		else if(TempT > 255.0) TempT = 255.0;
	}
	else
	{
		// "Error was detected. The RTD resistance measured is not within the range specified in the Threshold Registers."
		pt100->RtdOutOfRange = 1;
		TempT = 0.0;
	}

	pt100->Temperature = TempT;
}

/////////////////////////////////////////////////////////////////////////////////////////////

void Pt100Channel(pt100_t *pt100)
{
	switch(pt100->Ch)
	{
	case 1:

		set_pin(RTD_CS_A0_BASE, RTD_CS_A0_PIN);
		set_pin(RTD_CS_A1_BASE, RTD_CS_A1_PIN);

		break;

	case 2:

		set_pin(RTD_CS_A0_BASE, RTD_CS_A0_PIN);
		clear_pin(RTD_CS_A1_BASE, RTD_CS_A1_PIN);

		break;

	case 3:

		clear_pin(RTD_CS_A0_BASE, RTD_CS_A0_PIN);
		clear_pin(RTD_CS_A1_BASE, RTD_CS_A1_PIN);

		break;

	case 4:

		clear_pin(RTD_CS_A0_BASE, RTD_CS_A0_PIN);
		set_pin(RTD_CS_A1_BASE, RTD_CS_A1_PIN);

		break;

	default:

		break;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////

void Pt100InitChannel(pt100_t *pt100)
{
	unsigned int Fault_Error = 0; // Variable to read Fault register and compute faults
	unsigned int value = 0;

	// Set mux channel
	Pt100Channel(pt100);

	// Try to clear the error
	write_spi_byte(Configuration, 0x82);

	write_spi_byte(Configuration, 0xD0); // Enabling Vbias of Max31865

	value = read_spi_byte(read_Configuration); // Reading contents of Configuration register to verify communication with Max31865 is done properly

	if(value == 0xD0)
	{
		write_spi_byte(Write_High_Fault_Threshold_MSB, 0xFF); // Writing High Fault Threshold MSB
		write_spi_byte(Write_High_Fault_Threshold_LSB, 0xFF); // Writing High Fault Threshold LSB
		write_spi_byte(Write_Low_Fault_Threshold_MSB, 0x00);  // Writing Low Fault Threshold MSB
		write_spi_byte(Write_Low_Fault_Threshold_LSB, 0x00);  // Writing Low Fault Threshold LSB

		// "Communication successful with Max31865"
		pt100->CanNotCommunicate = 0;

		// Prior to getting started with RTD to Digital Conversion, Users can do a preliminary test to detect if their is a fault in RTD connection with Max31865
		Fault_Error = read_spi_byte(Fault_Status);

		// If their is no fault detected, the get_Temp() is called and it initiates the conversion. The results are displayed on the serial console
		if(Fault_Error == 0)
		{

			pt100->Error = 0;

		}
		else
		{

			// Save the error
			pt100->Error = Fault_Error;

		}

	}
	else
	{
		// " Unable to communicate with the device. Please check your connections and try again"
		pt100->CanNotCommunicate = 1;

	}
}

/////////////////////////////////////////////////////////////////////////////////////////////

void Pt100ReadChannel(pt100_t *pt100)
{
	unsigned int Fault_Error = 0; // Variable to read Fault register and compute faults

	// Set mux channel
	Pt100Channel(pt100);

	// Prior to getting started with RTD to Digital Conversion, Users can do a preliminary test to detect if their is a fault in RTD connection with Max31865
	Fault_Error = read_spi_byte(Fault_Status);

	// If their is no fault detected, the get_Temp() is called and it initiates the conversion. The results are displayed on the serial console
	if(Fault_Error == 0)
	{
		// Calling get_Temp() to read RTD registers and convert to Temperature reading
		get_Temp(pt100);

		pt100->Error = Fault_Error;

		if(pt100->Temperature > pt100->AlarmLimit)
		{
			if(pt100->Alarm_DelayCount < pt100->Alarm_Delay_ms) pt100->Alarm_DelayCount++;

			else
			{
				pt100->Alarm_DelayCount = 0;
				pt100->Alarm = 1;
			}
		}
		else pt100->Alarm_DelayCount = 0;

		if(pt100->Temperature > pt100->TripLimit)
		{
			if(pt100->Itlk_DelayCount < pt100->Itlk_Delay_ms) pt100->Itlk_DelayCount++;

			else
			{
				pt100->Itlk_DelayCount = 0;
				pt100->Trip = 1;
			}
		}
		else pt100->Itlk_DelayCount = 0;
	}
	else
	{
		// Save the error
		pt100->Temperature = 0.0;

		pt100->Error = Fault_Error;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////

void Pt100ChannelClear(pt100_t *pt100)
{
	// Set mux channel
	Pt100Channel(pt100);

	// Try to clear the error
	write_spi_byte(Configuration, 0x82);
}

/////////////////////////////////////////////////////////////////////////////////////////////

void Pt100Init(void)
{
	set_gpio_as_input(RTD_DRDY_1_BASE, RTD_DRDY_1_PIN);
	set_gpio_as_input(RTD_DRDY_2_BASE, RTD_DRDY_2_PIN);
	set_gpio_as_input(RTD_DRDY_3_BASE, RTD_DRDY_3_PIN);
	set_gpio_as_input(RTD_DRDY_4_BASE, RTD_DRDY_4_PIN);

	set_gpio_as_output(RTD_CS_A0_BASE, RTD_CS_A0_PIN);
	set_gpio_as_output(RTD_CS_A1_BASE, RTD_CS_A1_PIN);

	clear_pin(RTD_CS_A0_BASE, RTD_CS_A0_PIN);
	clear_pin(RTD_CS_A1_BASE, RTD_CS_A1_PIN);

	// Setting SPI clock to 1 MHz. Please note it is important to initialize SPI bus prior to making any changes.
	spi_init();

//*******************************************************************************************

    Pt100Ch1.Ch                 = 1;
    Pt100Ch1.Calibration        = 0;
    Pt100Ch1.Temperature        = 0.0;
    Pt100Ch1.AlarmLimit         = 100.0;
    Pt100Ch1.TripLimit          = 110.0;
    Pt100Ch1.Error              = 0;
    Pt100Ch1.RtdOutOfRange      = 0;
    Pt100Ch1.Alarm              = 0;
    Pt100Ch1.Trip               = 0;
    Pt100Ch1.Alarm_Delay_ms     = 0;
    Pt100Ch1.Alarm_DelayCount   = 0;
    Pt100Ch1.Itlk_Delay_ms      = 0;
    Pt100Ch1.Itlk_DelayCount    = 0;

#if (Pt100Ch1Enable == 1)

    Pt100InitChannel(&Pt100Ch1);

#endif

//*******************************************************************************************

    Pt100Ch2.Ch                 = 2;
    Pt100Ch2.Calibration        = 0;
    Pt100Ch2.Temperature        = 0.0;
    Pt100Ch2.AlarmLimit         = 100.0;
    Pt100Ch2.TripLimit          = 110.0;
    Pt100Ch2.Error              = 0;
    Pt100Ch2.RtdOutOfRange      = 0;
    Pt100Ch2.Alarm              = 0;
    Pt100Ch2.Trip               = 0;
    Pt100Ch2.Alarm_Delay_ms     = 0;
    Pt100Ch2.Alarm_DelayCount   = 0;
    Pt100Ch2.Itlk_Delay_ms      = 0;
    Pt100Ch2.Itlk_DelayCount    = 0;

#if (Pt100Ch2Enable == 1)

    Pt100InitChannel(&Pt100Ch2);

#endif

//*******************************************************************************************

    Pt100Ch3.Ch                 = 3;
    Pt100Ch3.Calibration        = 0;
    Pt100Ch3.Temperature        = 0.0;
    Pt100Ch3.AlarmLimit         = 100.0;
    Pt100Ch3.TripLimit          = 110.0;
    Pt100Ch3.Error              = 0;
    Pt100Ch3.RtdOutOfRange      = 0;
    Pt100Ch3.Alarm              = 0;
    Pt100Ch3.Trip               = 0;
    Pt100Ch3.Alarm_Delay_ms     = 0;
    Pt100Ch3.Alarm_DelayCount   = 0;
    Pt100Ch3.Itlk_Delay_ms      = 0;
    Pt100Ch3.Itlk_DelayCount    = 0;

#if (Pt100Ch3Enable == 1)

    Pt100InitChannel(&Pt100Ch3);

#endif

//*******************************************************************************************

    Pt100Ch4.Ch                 = 4;
    Pt100Ch4.Calibration        = 0;
    Pt100Ch4.Temperature        = 0.0;
    Pt100Ch4.AlarmLimit         = 100.0;
    Pt100Ch4.TripLimit          = 110.0;
    Pt100Ch4.Error              = 0;
    Pt100Ch4.RtdOutOfRange      = 0;
    Pt100Ch4.Alarm              = 0;
    Pt100Ch4.Trip               = 0;
    Pt100Ch4.Alarm_Delay_ms     = 0;
    Pt100Ch4.Alarm_DelayCount   = 0;
    Pt100Ch4.Itlk_Delay_ms      = 0;
    Pt100Ch4.Itlk_DelayCount    = 0;

#if (Pt100Ch4Enable == 1)

    Pt100InitChannel(&Pt100Ch4);

#endif

//*******************************************************************************************
 
}

/////////////////////////////////////////////////////////////////////////////////////////////

// Board Functions
//******************************************************************************

// Channel 1 Temperature Sample
void Pt100Ch1Sample(void)
{
    Pt100ReadChannel(&Pt100Ch1);
}

/////////////////////////////////////////////////////////////////////////////////////////////

// Channel 2 Temperature Sample
void Pt100Ch2Sample(void)
{
   Pt100ReadChannel(&Pt100Ch2);
}

/////////////////////////////////////////////////////////////////////////////////////////////

// Channel 3 Temperature Sample
void Pt100Ch3Sample(void)
{
    Pt100ReadChannel(&Pt100Ch3);
}

/////////////////////////////////////////////////////////////////////////////////////////////

// Channel 4 Temperature Sample
void Pt100Ch4Sample(void)
{
    Pt100ReadChannel(&Pt100Ch4);
}

/////////////////////////////////////////////////////////////////////////////////////////////

// Application Functions
//******************************************************************************

// Read Channel 1 Temperature value
float Pt100Ch1Read(void)
{
#if (Pt100Ch1Enable == 1)

    return Pt100Ch1.Temperature;

#else

    return 0;

#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////

// Read Channel 2 Temperature value
float Pt100Ch2Read(void)
{
#if (Pt100Ch2Enable == 1)

    return Pt100Ch2.Temperature;

#else

    return 0;

#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////

// Read Channel 3 Temperature value
float Pt100Ch3Read(void)
{
#if (Pt100Ch3Enable == 1)

    return Pt100Ch3.Temperature;

#else

    return 0;

#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////

// Read Channel 4 Temperature value
float Pt100Ch4Read(void)
{
#if (Pt100Ch4Enable == 1)

    return Pt100Ch4.Temperature;

#else

    return 0;

#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////

// Set Channel 1 Temperature Alarme Level
void Pt100Ch1AlarmLevelSet(float alarm)
{
    Pt100Ch1.AlarmLimit = alarm;
}

/////////////////////////////////////////////////////////////////////////////////////////////

// Set Channel 1 Temperature Trip Level
void Pt100Ch1TripLevelSet(float trip)
{
    Pt100Ch1.TripLimit = trip;
}

/////////////////////////////////////////////////////////////////////////////////////////////

// Set Channel 1 Interlock and Alarm Delay
void Pt100Ch1Delay(unsigned int delay_ms)
{
    Pt100Ch1.Alarm_Delay_ms = delay_ms;
    Pt100Ch1.Itlk_Delay_ms = delay_ms;
}

/////////////////////////////////////////////////////////////////////////////////////////////

// Set Channel 2 Temperature Alarme Level
void Pt100Ch2AlarmLevelSet(float alarm)
{
    Pt100Ch2.AlarmLimit = alarm;
}

/////////////////////////////////////////////////////////////////////////////////////////////

// Set Channel 2 Temperature Trip Level
void Pt100Ch2TripLevelSet(float trip)
{
    Pt100Ch2.TripLimit = trip;
}

/////////////////////////////////////////////////////////////////////////////////////////////

// Set Channel 2 Interlock and Alarm Delay
void Pt100Ch2Delay(unsigned int delay_ms)
{
    Pt100Ch2.Alarm_Delay_ms = delay_ms;
    Pt100Ch2.Itlk_Delay_ms = delay_ms;
}

/////////////////////////////////////////////////////////////////////////////////////////////

// Set Channel 3 Temperature Alarme Level
void Pt100Ch3AlarmLevelSet(float alarm)
{
    Pt100Ch3.AlarmLimit = alarm;
}

/////////////////////////////////////////////////////////////////////////////////////////////

// Set Channel 3 Temperature Trip Level
void Pt100Ch3TripLevelSet(float trip)
{
    Pt100Ch3.TripLimit = trip;
}

/////////////////////////////////////////////////////////////////////////////////////////////

// Set Channel 3 Interlock and Alarm Delay
void Pt100Ch3Delay(unsigned int delay_ms)
{
    Pt100Ch3.Alarm_Delay_ms = delay_ms;
    Pt100Ch3.Itlk_Delay_ms = delay_ms;
}

/////////////////////////////////////////////////////////////////////////////////////////////

// Set Channel 4 Temperature Alarme Level
void Pt100Ch4AlarmLevelSet(float alarm)
{
    Pt100Ch4.AlarmLimit = alarm;
}

/////////////////////////////////////////////////////////////////////////////////////////////

// Set Channel 4 Temperature Trip Level
void Pt100Ch4TripLevelSet(float trip)
{
    Pt100Ch4.TripLimit = trip;
}

/////////////////////////////////////////////////////////////////////////////////////////////

// Set Channel 4 Interlock and Alarm Delay
void Pt100Ch4Delay(unsigned int delay_ms)
{
    Pt100Ch4.Alarm_Delay_ms = delay_ms;
    Pt100Ch4.Itlk_Delay_ms = delay_ms;
}

/////////////////////////////////////////////////////////////////////////////////////////////

void Pt100ClearAlarmTrip(void)
{

	Pt100Ch1.Alarm = 0;
	Pt100Ch1.Trip  = 0;

	Pt100Ch2.Alarm = 0;
	Pt100Ch2.Trip  = 0;

	Pt100Ch3.Alarm = 0;
	Pt100Ch3.Trip  = 0;

	Pt100Ch4.Alarm = 0;
	Pt100Ch4.Trip  = 0;

}

/////////////////////////////////////////////////////////////////////////////////////////////

unsigned char Pt100Ch1AlarmStatusRead(void)
{
#if (Pt100Ch1Enable == 1)

    return Pt100Ch1.Alarm;

#else

    return 0;

#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////

unsigned char Pt100Ch1TripStatusRead(void)
{
#if (Pt100Ch1Enable == 1)

    return Pt100Ch1.Trip;

#else

    return 0;

#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////

unsigned char Pt100Ch2AlarmStatusRead(void)
{
#if (Pt100Ch2Enable == 1)

    return Pt100Ch2.Alarm;

#else

    return 0;

#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////

unsigned char Pt100Ch2TripStatusRead(void)
{
#if (Pt100Ch2Enable == 1)

    return Pt100Ch2.Trip;

#else

    return 0;

#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////

unsigned char Pt100Ch3AlarmStatusRead(void)
{
#if (Pt100Ch3Enable == 1)

    return Pt100Ch3.Alarm;

#else

    return 0;

#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////

unsigned char Pt100Ch3TripStatusRead(void)
{
#if (Pt100Ch3Enable == 1)

    return Pt100Ch3.Trip;

#else

    return 0;

#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////

unsigned char Pt100Ch4AlarmStatusRead(void)
{
#if (Pt100Ch4Enable == 1)

    return Pt100Ch4.Alarm;

#else

    return 0;

#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////

unsigned char Pt100Ch4TripStatusRead(void)
{
#if (Pt100Ch4Enable == 1)

    return Pt100Ch4.Trip;

#else

    return 0;

#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////

// Read Channel 1 Error flag
unsigned char Pt100Ch1ErrorRead(void)
{
#if (Pt100Ch1Enable == 1)

    return Pt100Ch1.Error;

#else

    return 0;

#endif
}
/////////////////////////////////////////////////////////////////////////////////////////////

// Read Channel 2 Error flag
unsigned char Pt100Ch2ErrorRead(void)
{
#if (Pt100Ch2Enable == 1)

    return Pt100Ch2.Error;

#else

    return 0;

#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////

// Read Channel 3 Error flag
unsigned char Pt100Ch3ErrorRead(void)
{
#if (Pt100Ch3Enable == 1)

    return Pt100Ch3.Error;

#else

    return 0;

#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////

// Read Channel 4 Error flag
unsigned char Pt100Ch4ErrorRead(void)
{
#if (Pt100Ch4Enable == 1)

    return Pt100Ch4.Error;

#else

    return 0;

#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////

// Read Channel 1 Can Not Communicate flag
unsigned char Pt100Ch1CNCRead(void)
{
#if (Pt100Ch1Enable == 1)

    return Pt100Ch1.CanNotCommunicate;

#else

    return 0;

#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////

// Read Channel 2 Can Not Communicate flag
unsigned char Pt100Ch2CNCRead(void)
{
#if (Pt100Ch2Enable == 1)

    return Pt100Ch2.CanNotCommunicate;

#else

    return 0;

#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////

// Read Channel 3 Can Not Communicate flag
unsigned char Pt100Ch3CNCRead(void)
{
#if (Pt100Ch3Enable == 1)

    return Pt100Ch3.CanNotCommunicate;

#else

    return 0;

#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////

// Read Channel 4 Can Not Communicate flag
unsigned char Pt100Ch4CNCRead(void)
{
#if (Pt100Ch4Enable == 1)

    return Pt100Ch4.CanNotCommunicate;

#else

    return 0;

#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////

// Read Channel 1 RTD Out Of Range flag
unsigned char Pt100Ch1RtdStatusRead(void)
{
#if (Pt100Ch1Enable == 1)

    return Pt100Ch1.RtdOutOfRange;

#else

    return 0;

#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////

// Read Channel 2 RTD Out Of Range flag
unsigned char Pt100Ch2RtdStatusRead(void)
{
#if (Pt100Ch2Enable == 1)

    return Pt100Ch2.RtdOutOfRange;

#else

    return 0;

#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////

// Read Channel 3 RTD Out Of Range flag
unsigned char Pt100Ch3RtdStatusRead(void)
{
#if (Pt100Ch3Enable == 1)

    return Pt100Ch3.RtdOutOfRange;

#else

    return 0;

#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////

// Read Channel 4 RTD Out Of Range flag
unsigned char Pt100Ch4RtdStatusRead(void)
{
#if (Pt100Ch4Enable == 1)

    return Pt100Ch4.RtdOutOfRange;

#else

    return 0;

#endif
}

/////////////////////////////////////////////////////////////////////////////////////////////

// Try to clear Channel 1 Error flag
void Pt100Ch1Clear(void)
{
    Pt100ChannelClear(&Pt100Ch1);
}

/////////////////////////////////////////////////////////////////////////////////////////////

// Try to clear Channel 2 Error flag
void Pt100Ch2Clear(void)
{
    Pt100ChannelClear(&Pt100Ch2);
}

/////////////////////////////////////////////////////////////////////////////////////////////

// Try to clear Channel 3 Error flag
void Pt100Ch3Clear(void)
{
    Pt100ChannelClear(&Pt100Ch3);
}

/////////////////////////////////////////////////////////////////////////////////////////////

// Try to clear Channel 4 Error flag
void Pt100Ch4Clear(void)
{
    Pt100ChannelClear(&Pt100Ch4);
}

/////////////////////////////////////////////////////////////////////////////////////////////

// Try to reset Ch1
void Pt100Ch1Reset(void)
{
    Pt100InitChannel(&Pt100Ch1);
}

/////////////////////////////////////////////////////////////////////////////////////////////

// Try to reset Ch2
void Pt100Ch2Reset(void)
{
    Pt100InitChannel(&Pt100Ch2);
}

/////////////////////////////////////////////////////////////////////////////////////////////

// Try to reset Ch3
void Pt100Ch3Reset(void)
{
    Pt100InitChannel(&Pt100Ch3);
}

/////////////////////////////////////////////////////////////////////////////////////////////

// Try to reset Ch4
void Pt100Ch4Reset(void)
{
    Pt100InitChannel(&Pt100Ch4);
}

/////////////////////////////////////////////////////////////////////////////////////////////



