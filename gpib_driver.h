/* gpib_driver.h */

#ifndef _GPIB_DRIVER_H
#define _GPIB_DRIVER_H

#define MAX_GPIB_ADDR 30
#define MAX_CONTROLLERS 5

/*--------------------------------------------------------------*/
/* keep track of GPIB devices on each controller		*/
/*--------------------------------------------------------------*/

typedef struct
{
   int   controller_id;		/* controller_id = -1 uninitialized record */
   int   device_id;		/* GPIB address */
   int   flags;			/* flags needed to talk to device properly */
} gpib_record;

extern Tcl_HashTable linkHash;	/* Store records indexed by device ID# */

/*--------------------------------------------------------------*/
/* keep track of the status and parameters of each controller	*/
/*--------------------------------------------------------------*/

typedef struct per_controller_state_st
{
  int    initialized;
  int    active;
  int    dev;
  int    fd;
  short  revision;
  int    error;
  int    count;
  void  *data;		// GPIB or DIO device record

} per_controller_state_t;

extern per_controller_state_t controller[MAX_CONTROLLERS];

#define BUF_SIZE  (393216) /* size of input buffer */

#define                 ON              1
#define                 OFF             0
#define                 OK              0
#define                 True            1
#define                 False           0

/*------------------------------------------------------*/
/* Valid bits for flags (expand as needed)		*/
/*------------------------------------------------------*/
/*							*/
/*  READ_STB --- This 8-bit flag indicates the status	*/
/*		 byte returned by serially polling the	*/
/*		 device indicating when data are	*/
/*		 available for reading (STB & READ_STB) */
/*  READ_EOI --- Expect the EOI line to be raised to	*/
/*		 indicate the end of received data.	*/
/*  READ_LF  --- Ignore the EOI line and watch for the	*/
/*		 linefeed character as the terminator	*/
/*		 for received data.			*/
/*  READ_CR  --- Ignore the EOI line and watch for the	*/
/*		 carriage return character as the	*/
/*		 terminator for received data.		*/
/*  TERM_EOI --- Raise the EOI signal on the last byte	*/
/*		 of outbound data.			*/
/*  TERM_LF  --- Generate linefeed on outbound commands	*/
/*		 to this device.			*/
/*  TERM_CR  --- Generate carriage return on outbound	*/
/*		 commands to this device.		*/
/*  STB_RDY  --- If set, bit 0x10 of the status		*/
/*		 register indicates "device ready".	*/
/*		 Use when bit is not cleared by a read.	*/
/*  HAS_SPOLL -- Device responds to a serial poll.	*/
/*							*/
/*  READ_AUTO -- This is a temporary flag set by the	*/
/*		 contoller and cannot be set as an 	*/
/*		 option in the map file.  It is used	*/
/*		 when initiating a "write" with an	*/
/*		 expected result to avoid losing data	*/
/*  IS_BINARY -- Temporary flag set by the controller	*/
/*		 to indicate a binary transmission	*/
/*							*/
/*  The default setting is:				*/
/*  0x10 | READ_EOI | TERM_EOI | TERM_LF | TERM_CR	*/
/*	| SPOLL						*/
/*  (READ_STB = 0x10, or STAT_MESSAGE)			*/
/*------------------------------------------------------*/

// Standard GPIB status bit positions (as much as anything
// in GPIB is standard. . .).  Older devices (esp. HP) tend
// to use 0x01, or even an inverted sense bit.

#define STAT_MESSAGE	0x10

// Status word bit positions for internal use only
#define STAT_DONE	0x1000

// User-provided flags

#define READ_STB	0x00ff
#define READ_EOI	0x0100
#define READ_LF		0x0200
#define READ_CR		0x0400
#define TERM_EOI	0x0800
#define TERM_LF		0x1000
#define TERM_CR		0x2000
#define STB_RDY		0x4000
#define HAS_SPOLL	0x8000

#define DEFAULT_FLAGS \
	(STAT_MESSAGE | READ_EOI | TERM_EOI | TERM_LF | TERM_CR | HAS_SPOLL)

// Flags for internal use only

#define READ_AUTO	0x010000
#define IS_BINARY	0x020000
#define NO_NEWLINE	0x040000

/* Forward references */

extern int open_usb_serial(char *);
extern int open_gpib_interfaces(Tcl_Interp *);
extern int poll_and_read(int, char *, int, int);
extern int controller_set_data(int, void *);
extern void *controller_get_data(int);
extern void gpib_init_controller(int);
extern void gpib_map_address(char *, int, int);
extern int gpib_get_address(char *);
extern int gpib_close_all_links(int);
extern int gpib_close(Tcl_Interp *, int);
extern int gpib_close_link(Tcl_Interp *, char *);
extern int gpib_find_controller(char *, gpib_record **);
extern int gpib_address_device(char *, gpib_record **);
extern int gpib_local(char *);
extern int gpib_trigger(char *);
extern int gpib_clear(char *);
extern int gpib_status_byte(char *, int *);
extern int gpib_send(Tcl_Interp *, char *, void *, long, int);
extern int gpib_receive(Tcl_Interp *, char *, unsigned char *, long, int *,
		gpib_record **);
extern int gpib_buffered_read(Tcl_Interp *, char *, unsigned char **, long *);
extern void gpib_list_links(Tcl_Interp *);
extern int gpib_find_listeners(Tcl_Interp *, int *, int, int);
extern int gpib_init(Tcl_Interp *, char *, int);
extern void gpib_global_init();

extern int gpib_command_init(Tcl_Interp *);
extern int support_command_init(Tcl_Interp *);

#endif /* _GPIB_DRIVER_H */
