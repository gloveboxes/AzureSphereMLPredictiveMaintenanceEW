/* Copyright (c) Microsoft Corporation. All rights reserved.
 * Licensed under the MIT License.
 *
 * This example is built on the Azure Sphere DevX library.
 *   1. DevX is an Open Source community-maintained implementation of the Azure Sphere SDK samples.
 *   2. DevX is a modular library that simplifies common development scenarios.
 *        - You can focus on your solution, not the plumbing.
 *   3. DevX documentation is maintained at https://github.com/Azure-Sphere-DevX/AzureSphereDevX.Examples/wiki
 *
 * DEVELOPER BOARD SELECTION
 *
 * The following developer boards are supported.
 *
 *	 1. AVNET Azure Sphere Starter Kit.
 *   2. AVNET Azure Sphere Starter Kit Revision 2.
 *
 * ENABLE YOUR DEVELOPER BOARD
 *
 * Each Azure Sphere developer board manufacturer maps pins differently. You need to select the
 *    configuration that matches your board.
 *
 * Follow these steps:
 *
 *	 1. Open CMakeLists.txt.
 *	 2. Uncomment the set command that matches your developer board.
 *	 3. Click File, then Save to auto-generate the CMake Cache.
 *
 ************************************************************************************************/

#include "main.h"

static void set_motor_action(MOTOR_ACTION action)
{
	switch (action)
	{
		case MOTOR_FORWARD:
			dx_gpioOn(&gpio_nsl);
			dx_gpioOn(&gpio_en);
			dx_gpioOff(&gpio_ph);

			dx_gpioOff(&gpio_led_blue);
			dx_gpioOff(&gpio_led_red);
			dx_gpioOn(&gpio_led_blue);
			break;
		case MOTOR_REVERSE:
			dx_gpioOn(&gpio_nsl);
			dx_gpioOn(&gpio_en);
			dx_gpioOn(&gpio_ph);

			dx_gpioOff(&gpio_led_blue);
			dx_gpioOff(&gpio_led_red);
			dx_gpioOn(&gpio_led_red);
			break;
		case MOTOR_STOP:
			dx_gpioOn(&gpio_nsl);
			dx_gpioOff(&gpio_en);

			dx_gpioOff(&gpio_led_blue);
			dx_gpioOff(&gpio_led_red);
			break;
		case MOTOR_SLEEP:
			dx_gpioOff(&gpio_nsl);

			dx_gpioOff(&gpio_led_blue);
			dx_gpioOff(&gpio_led_red);
			break;
		default:
			break;
	}
}

/// <summary>
/// Handler to check for Button Presses
/// </summary>
static DX_TIMER_HANDLER(start_vibration_motor_handler)
{
	static int count     = 0;
	static int rotations = 14;

	if (count == 0 && azure_connected)
	{
		dx_deviceTwinReportValue(&dt_prediction, "bearings");
	}

	if (count++ % 2)
	{
		set_motor_action(MOTOR_FORWARD);
	}
	else
	{
		set_motor_action(MOTOR_REVERSE);
	}

	if (count == rotations)
	{
		count     = 0;
		rotations = rotations == 14 ? 16 : 14;

		set_motor_action(MOTOR_STOP);
		dx_gpioOff(&gpio_relay_1);
		dx_gpioOn(&gpio_relay_2);
		dx_timerOneShotSet(&tmr_reset_demo, &(struct timespec){45, 0});
	}
	else
	{
		dx_timerOneShotSet(&tmr_start_vibration_motor, &(struct timespec){0, 250 * ONE_MS});
	}
}
DX_TIMER_HANDLER_END

DX_TIMER_HANDLER(reset_demo_handler)
{
	if (azure_connected)
	{
		dx_deviceTwinReportValue(&dt_prediction, "normal");
	}

	dx_gpioOn(&gpio_relay_1);
	dx_gpioOff(&gpio_relay_2);
}
DX_TIMER_HANDLER_END

/// <summary>
/// Handler to check for Button Presses
/// </summary>
static DX_TIMER_HANDLER(control_button_handler)
{
	static GPIO_Value_Type buttonAState;
	static GPIO_Value_Type buttonBState;

	if (dx_gpioStateGet(&gpio_buttonA, &buttonAState))
	{
		reset_demo();
		dx_timerOneShotSet(&tmr_start_vibration_motor, &(struct timespec){0, 10 * ONE_MS});
	}

	if (dx_gpioStateGet(&gpio_buttonB, &buttonBState))
	{
		if (azure_connected)
		{
			dx_deviceTwinReportValue(&dt_prediction, "normal");
		}

		reset_demo();
	}
}
DX_TIMER_HANDLER_END

static void reset_demo(void)
{
	dx_timerOneShotSet(&tmr_reset_demo, &(struct timespec){0, 0});
	dx_gpioOn(&gpio_relay_1);
	dx_gpioOff(&gpio_relay_2);
}

/// <summary>
/// Callback handler for Inter-Core Messaging
/// </summary>
static void intercore_environment_receive_msg_handler(void *data_block, ssize_t message_length)
{
	INTERCORE_BLOCK *ic_data = (INTERCORE_BLOCK *)data_block;

	switch (ic_data->cmd)
	{
		case IC_PREDICTION:

			dx_Log_Debug("Predicted fault: %s\n", ic_data->PREDICTION);

			// Ignore if predicted fault is normal
			if (strncmp(ic_data->PREDICTION, "normal", sizeof(ic_data->PREDICTION)))
			{
				reset_demo();

				dx_gpioOff(&gpio_relay_1);
				dx_gpioOn(&gpio_relay_2);
				dx_timerOneShotSet(&tmr_reset_demo, &(struct timespec){45, 0});

				if (azure_connected)
				{
					dx_deviceTwinReportValue(&dt_prediction, ic_data->PREDICTION);
				}
			}

			break;
		default:
			break;
	}
}

static void ConnectionStatus(bool connected)
{
	dx_gpioStateSet(&gpio_network_led, connected);
	azure_connected = connected;
}

static void report_startup(bool connected)
{
	dx_deviceTwinReportValue(&dt_utc_startup, dx_getCurrentUtc(msgBuffer, sizeof(msgBuffer)));
	snprintf(msgBuffer, sizeof(msgBuffer), "Sample version: %s, DevX version: %s", SAMPLE_VERSION_NUMBER, AZURE_SPHERE_DEVX_VERSION);
	dx_deviceTwinReportValue(&dt_hvac_sw_version, msgBuffer);
	dx_deviceTwinReportValue(&dt_prediction, "normal");
	// now unregister this callback as we've reported startup time and sw version
	dx_azureUnregisterConnectionChangedNotification(report_startup);
}

/// <summary>
///  Initialize peripherals, device twins, direct methods, timer_binding_sets.
/// </summary>
static void InitPeripheralsAndHandlers(void)
{
	dx_Log_Debug_Init(Log_Debug_Time_buffer, sizeof(Log_Debug_Time_buffer));
	dx_azureConnect(&dx_config, NETWORK_INTERFACE, NULL);
	dx_gpioSetOpen(gpio_binding_sets, NELEMS(gpio_binding_sets));
	dx_deviceTwinSubscribe(device_twin_bindings, NELEMS(device_twin_bindings));

	dx_intercoreConnect(&intercore_environment_ctx);

	dx_azureRegisterConnectionChangedNotification(report_startup);
	dx_azureRegisterConnectionChangedNotification(ConnectionStatus);

	dx_timerSetStart(timer_binding_sets, NELEMS(timer_binding_sets));

	dx_gpioOn(&gpio_relay_1);
}

/// <summary>
///     Close peripherals and handlers.
/// </summary>
static void ClosePeripheralsAndHandlers(void)
{
	dx_deviceTwinUnsubscribe();
	dx_gpioSetClose(gpio_binding_sets, NELEMS(gpio_binding_sets));
	dx_azureToDeviceStop();
}

int main(int argc, char *argv[])
{
	dx_registerTerminationHandler();

	if (!dx_configParseCmdLineArguments(argc, argv, &dx_config))
	{
		return dx_getTerminationExitCode();
	}

	InitPeripheralsAndHandlers();

	// Main loop
	dx_eventLoopRun();

	ClosePeripheralsAndHandlers();
	Log_Debug("Application exiting.\n");
	return dx_getTerminationExitCode();
}