//**************************************************************************
// FreeRtos on Samd21
// By Scott Briscoe
//
// Project is a simple example of how to get FreeRtos running on a SamD21 processor
// Project can be used as a template to build your projects off of as well
//
//**************************************************************************

#include <FreeRTOS_SAMD21.h>
#include <csp/csp.h>
#include <csp/interfaces/csp_if_can.h>
#include <csp/drivers/usart.h>
#include <csp/drivers/can_socketcan.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <time.h>

//**************************************************************************
// Type Defines and Constants
//**************************************************************************

#define  ERROR_LED_PIN  13 //Led Pin: Typical Arduino Board
//#define  ERROR_LED_PIN  2 //Led Pin: samd21 xplained board

#define ERROR_LED_LIGHTUP_STATE  HIGH // the state that makes the led light up on your board, either low or high

// Select the serial port the project should use and communicate over
// Some boards use SerialUSB, some use Serial
#define SERIAL          SerialUSB //Sparkfun Samd21 Boards
//#define SERIAL          Serial //Adafruit, other Samd21 Boards

#define CAN_CS_PIN      7 // Chip select pin for the CAN bus controller
#define CAN_INT_PIN     2 // Interrupt pin for the CAN bus controller

// Server port, the port the server listens on for incoming connections from the client
#define CSP_SERVER_PORT 10 // Port number for the CSP server

//**************************************************************************
// global variables
//**************************************************************************
TaskHandle_t Handle_aTask;
TaskHandle_t Handle_bTask;
TaskHandle_t Handle_cspTask;
TaskHandle_t Handle_monitorTask;

int router_start(void);

/* Commandline options */
static uint8_t server_address = 0;
static uint8_t client_address = 0;

/* Test mode, check that server & client can exchange packets */
static bool test_mode = false;
static unsigned int successful_ping = 0;

enum DeviceType {
	DEVICE_UNKNOWN,
	DEVICE_CAN,
	DEVICE_KISS,
	DEVICE_ZMQ,
};

#define __maybe_unused __attribute__((__unused__))

//**************************************************************************
// Can use these function for RTOS delays
// Takes into account processor speed
// Use these instead of delay(...) in rtos tasks
//**************************************************************************
void myDelayUs(int us)
{
  vTaskDelay( us / portTICK_PERIOD_US );  
}

void myDelayMs(int ms)
{
  vTaskDelay( (ms * 1000) / portTICK_PERIOD_US );  
}

void myDelayMsUntil(TickType_t *previousWakeTime, int ms)
{
  vTaskDelayUntil( previousWakeTime, (ms * 1000) / portTICK_PERIOD_US );  
}

//*****************************************************************
// Create a thread that prints out A to the screen every two seconds
// this task will delete its self after printing out afew messages
//*****************************************************************
static void threadA( void *pvParameters ) 
{
  
  SERIAL.println("Thread A: Started");
  for(int x=0; x<100; ++x)
  {
    SERIAL.print("A");
    SERIAL.flush();
    myDelayMs(500);
  }
  
  // delete ourselves.
  // Have to call this or the system crashes when you reach the end bracket and then get scheduled.
  SERIAL.println("Thread A: Deleting");
  vTaskDelete( NULL );
}

//*****************************************************************
// Create a thread that prints out B to the screen every second
// this task will run forever
//*****************************************************************
static void threadB( void *pvParameters ) 
{
  SERIAL.println("Thread B: Started");

  while(1)
  {
    SERIAL.println("B");
    SERIAL.flush();
    myDelayMs(2000);
  }

}

//*****************************************************************
// Create a thread that prints "Hello, CSP!" to the CAN bus every second
// this task will run forever
//*****************************************************************
void csp_task(void *pvParameters) {
		myDelayUs(test_mode ? 200000 : 1000000);

		/* Send ping to server, timeout 1000 mS, ping size 100 bytes */
		int result = csp_ping(server_address, 1000, 100, CSP_O_NONE);
		csp_print("Ping address: %u, result %d [mS]\n", server_address, result);
        // Increment successful_ping if ping was successful
        if (result >= 0) {
            ++successful_ping;
        }

		/* Send reboot request to server, the server has no actual implementation of csp_sys_reboot() and fails to reboot */
		csp_reboot(server_address);
		csp_print("reboot system request sent to address: %u\n", server_address);

		/* Send data packet (string) to server */

		/* 1. Connect to host on 'server_address', port SERVER_PORT with regular UDP-like protocol and 1000 ms timeout */
		csp_conn_t * conn = csp_connect(CSP_PRIO_NORM, server_address, CSP_SERVER_PORT, 1000, CSP_O_NONE);
		if (conn == NULL) {
			/* Connect failed */
			csp_print("Connection failed\n");
			return;
		}

		/* 2. Get packet buffer for message/data */
		csp_packet_t * packet = csp_buffer_get(0);
		if (packet == NULL) {
			/* Could not get buffer element */
			csp_print("Failed to get CSP buffer\n");
			return;
		}

		/* 3. Copy data to packet */
		memcpy(packet->data, "Hello world ", 12);
		memset(packet->data + 13, 0, 1);

		/* 4. Set packet length */
		packet->length = (strlen((char *) packet->data) + 1); /* include the 0 termination */

		/* 5. Send packet */
		csp_send(conn, packet);

		/* 6. Close connection */
		csp_close(conn);
	}

//*****************************************************************
// Task will periodically print out useful information about the tasks running
// Is a useful tool to help figure out stack sizes being used
// Run time stats are generated from all task timing collected since startup
// No easy way yet to clear the run time stats yet
//*****************************************************************
static char ptrTaskList[400]; //temporary string buffer for task stats

void taskMonitor(void *pvParameters)
{
    int measurement;
    
    SERIAL.println("Task Monitor: Started");

    // run this task afew times before exiting forever
    while(1)
    {
    	myDelayMs(10000); // print every 10 seconds

    	SERIAL.flush();
		SERIAL.println("");			 
    	SERIAL.println("****************************************************");
    	SERIAL.print("Free Heap: ");
    	SERIAL.print(xPortGetFreeHeapSize());
    	SERIAL.println(" bytes");

    	SERIAL.print("Min Heap: ");
    	SERIAL.print(xPortGetMinimumEverFreeHeapSize());
    	SERIAL.println(" bytes");
    	SERIAL.flush();

    	SERIAL.println("****************************************************");
    	SERIAL.println("Task            ABS             %Util");
    	SERIAL.println("****************************************************");

    	vTaskGetRunTimeStats(ptrTaskList); //save stats to char array
    	SERIAL.println(ptrTaskList); //prints out already formatted stats
    	SERIAL.flush();

		SERIAL.println("****************************************************");
		SERIAL.println("Task            State   Prio    Stack   Num     Core" );
		SERIAL.println("****************************************************");

		vTaskList(ptrTaskList); //save stats to char array
		SERIAL.println(ptrTaskList); //prints out already formatted stats
		SERIAL.flush();

		SERIAL.println("****************************************************");
		SERIAL.println("[Stacks Free Bytes Remaining] ");

		measurement = uxTaskGetStackHighWaterMark( Handle_aTask );
		SERIAL.print("Thread A: ");
		SERIAL.println(measurement);

		measurement = uxTaskGetStackHighWaterMark( Handle_bTask );
		SERIAL.print("Thread B: ");
		SERIAL.println(measurement);

		measurement = uxTaskGetStackHighWaterMark( Handle_monitorTask );
		SERIAL.print("Monitor Stack: ");
		SERIAL.println(measurement);

		SERIAL.println("****************************************************");
		SERIAL.flush();

    }

    // delete ourselves.
    // Have to call this or the system crashes when you reach the end bracket and then get scheduled.
    SERIAL.println("Task Monitor: Deleting");
    vTaskDelete( NULL );

}

//*****************************************************************
// 
//*****************************************************************
csp_iface_t * add_interface(enum DeviceType device_type, const char * device_name)
{
    csp_iface_t * default_iface = NULL;

	if (device_type == DEVICE_KISS) {
        csp_usart_conf_t conf = {
			.device = device_name,
            .baudrate = 115200, /* supported on all platforms */
            .databits = 8,
            .stopbits = 1,
            .paritysetting = 0,
		};
        int error = csp_usart_open_and_add_kiss_interface(&conf, CSP_IF_KISS_DEFAULT_NAME, client_address, &default_iface);
        if (error != CSP_ERR_NONE) {
            csp_print("failed to add KISS interface [%s], error: %d\n", device_name, error);
            exit(1);
        }
        default_iface->is_default = 1;
    }

	if (CSP_HAVE_LIBSOCKETCAN && (device_type == DEVICE_CAN)) {
		int error = csp_can_socketcan_open_and_add_interface(device_name, CSP_IF_CAN_DEFAULT_NAME, client_address, 1000000, true, &default_iface);
        if (error != CSP_ERR_NONE) {
			csp_print("failed to add CAN interface [%s], error: %d\n", device_name, error);
            exit(1);
        }
        default_iface->is_default = 1;
    }

	return default_iface;
}


//*****************************************************************

void setup() 
{

  SERIAL.begin(115200);

  delay(1000); // prevents usb driver crash on startup, do not omit this
  while (!SERIAL);  // Wait for serial terminal to open port before starting program
  SERIAL.println("Serial Port Opened");

  SERIAL.println("");
  SERIAL.println("******************************");
  SERIAL.println("        Program start         ");
  SERIAL.println("******************************");
  SERIAL.flush();

  // Initialize the csp stack
  const char * device_name = NULL;
	enum DeviceType device_type = DEVICE_UNKNOWN;
	const char * rtable __maybe_unused = NULL;
	csp_iface_t * default_iface;

  /* Init CSP */
  csp_init();

  /* Start router */
  router_start();

  /* Add interface(s) */
	default_iface = add_interface(device_type, device_name);

	/* Setup routing table */
	if (CSP_USE_RTABLE) {
		if (rtable) {
			int error = csp_rtable_load(rtable);
			if (error < 1) {
				csp_print("csp_rtable_load(%s) failed, error: %d\n", rtable, error);
				exit(1);
			}
		} else if (default_iface) {
			csp_rtable_set(0, 0, default_iface, CSP_NO_VIA_ADDRESS);
		}
	}

    csp_print("Connection table\r\n");
    csp_conn_print_table();

    csp_print("Interfaces\r\n");
    csp_iflist_print();

	if (CSP_USE_RTABLE) {
		csp_print("Route table\r\n");
		csp_rtable_print();
	}

    /* Start client work */
	csp_print("Client started\n");


  // Set the led the rtos will blink when we have a fatal rtos error
  // RTOS also Needs to know if high/low is the state that turns on the led.
  // Error Blink Codes:
  //    3 blinks - Fatal Rtos Error, something bad happened. Think really hard about what you just changed.
  //    2 blinks - Malloc Failed, Happens when you couldn't create a rtos object. 
  //               Probably ran out of heap.
  //    1 blink  - Stack overflow, Task needs more bytes defined for its stack! 
  //               Use the taskMonitor thread to help gauge how much more you need
  vSetErrorLed(ERROR_LED_PIN, ERROR_LED_LIGHTUP_STATE);

  // sets the serial port to print errors to when the rtos crashes
  // if this is not set, serial information is not printed by default
  vSetErrorSerial(&SERIAL);

  // Create the threads that will be managed by the rtos
  // Sets the stack size and priority of each task
  // Also initializes a handler pointer to each task, which are important to communicate with and retrieve info from tasks
  xTaskCreate(threadA,     "Task A",       256,  NULL, tskIDLE_PRIORITY + 3, &Handle_aTask);
  xTaskCreate(threadB,     "Task B",       256,  NULL, tskIDLE_PRIORITY + 2, &Handle_bTask);
  xTaskCreate(csp_task,    "CSP Task",     1024, NULL, 1,                    &Handle_cspTask);
  xTaskCreate(taskMonitor, "Task Monitor", 256,  NULL, tskIDLE_PRIORITY + 1, &Handle_monitorTask);

  // Start the RTOS, this function will never return and will schedule the tasks.
  vTaskStartScheduler();

  // error scheduler failed to start
  // should never get here
  while(1)
  {
	  SERIAL.println("Scheduler Failed! \n");
	  SERIAL.flush();
	  delay(1000);
  }

}

//*****************************************************************
// This is now the rtos idle loop
// No rtos blocking functions allowed!
//*****************************************************************
void loop() 
{
    // Optional commands, can comment/uncomment below
    SERIAL.print("."); //print out dots in terminal, we only do this when the RTOS is in the idle state
    SERIAL.flush();
    delay(100); //delay is interrupt friendly, unlike vNopDelayMS
}


//*****************************************************************

