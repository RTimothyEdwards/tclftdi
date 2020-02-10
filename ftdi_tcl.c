/*--------------------------------------------------------------*/
/* This file to be used with the libftdi API			*/
/*--------------------------------------------------------------*/

#ifdef HAVE_LIBFTDI

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <dlfcn.h>

#include <ftdi.h>
#include <tcl.h>

/* Forward declarations */

extern void Fprintf(Tcl_Interp *interp, FILE *f, char *format, ...);

/*--------------------------------------------------------------*/
/* Structure to manage device handles				*/
/* Each device record contains the device handle and the	*/
/* device description.  To do:  Add the dbus read-back value	*/
/* and use this as an alternative device identifier.		*/
/*--------------------------------------------------------------*/

typedef struct _ftdi_record {
   struct ftdi_context *ftContext;
   char *description;
   unsigned char flags;
   unsigned char cmdwidth;	// Number bits for command word
   unsigned char wordwidth;	// Bits per word for bit-bang mode
   unsigned char sigpins[8];	// Signal pin assignments for bit-bang mode
} ftdi_record;

/* Flag definitions */
#define CS_INVERT    0x01	// CS is sense-positive
#define MIXED_MODE   0x03	// Mixed mode has SDI and SDO on
				// different SCK edges.
#define BITBANG_MODE 0x04	// Set bit-bang mode
#define CSB_NORAISE  0x08	// CSB is not raised after read or write
#define LEGACY_MODE  0x10	// Legacy mode has fixed values for
				// opcode and supports 16 registers.
#define SERIAL_MODE  0x20	// FTDI in default serial mode.

Tcl_HashTable handletab;
Tcl_Interp *ftdiinterp;

/*--------------------------------------------------------------*/
/* Miscellaneous stuff						*/
/*--------------------------------------------------------------*/

typedef unsigned char bool;

#define false 0
#define true 1

static int verbose = 1;
static int ftdinum = -1;

static int usb_vid = 0x0403;
static int usb_pid = 0x60ff;

/*--------------------------------------------------------------*/
/* Support function "find_handle"				*/
/*								*/
/* Return the handle associated with the device string in the	*/
/* hash table.							*/
/*								*/
/* Return the value of the device flags in "flagptr" (u_char).	*/
/* Might be preferable to simply have a routine that returns	*/
/* just the ftdi_record pointer.					*/
/*--------------------------------------------------------------*/

struct ftdi_context *
find_handle(char *devstr, unsigned char *flagptr)
{
   Tcl_HashEntry *h;
   struct ftdi_context * ftContext;
   ftdi_record *ftRecordPtr;

   h = Tcl_FindHashEntry(&handletab, devstr);
   if (h != NULL) {
      ftRecordPtr = (ftdi_record *)Tcl_GetHashValue(h);
      if (flagptr != NULL) *flagptr = ftRecordPtr->flags;
      return ftRecordPtr->ftContext;
   }
   if (flagptr != NULL) *flagptr = 0x0;
   return (struct ftdi_context *)NULL;
}

/*--------------------------------------------------------------*/
/* Similar to the above, but returns the record			*/
/*--------------------------------------------------------------*/

ftdi_record *
find_record(char *devstr, struct ftdi_context **handleptr)
{
   Tcl_HashEntry *h;
   ftdi_record *ftRecordPtr;

   h = Tcl_FindHashEntry(&handletab, devstr);
   if (h != NULL) {
      ftRecordPtr = (ftdi_record *)Tcl_GetHashValue(h);
      if (handleptr != NULL) *handleptr = ftRecordPtr->ftContext;
      return ftRecordPtr;
   }
   if (handleptr != NULL) *handleptr = NULL;
   return (ftdi_record *)NULL;
}

/*--------------------------------------------------------------*/
/* Tcl function "ftdi_setid"					*/
/* Set the product and vendor IDs used by "ftdi_open".		*/
/*--------------------------------------------------------------*/

int
ftditcl_setid(ClientData clientData,
	Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   int vid_val, pid_val;
   Tcl_Obj *lobj;

   if (objc > 1) {

      Tcl_GetIntFromObj(interp, objv[1], &pid_val);
      usb_pid = pid_val & 0xffff;

      if (objc > 2) {
         Tcl_GetIntFromObj(interp, objv[2], &vid_val);
         usb_vid = vid_val & 0xffff;
      }
   }

   lobj = Tcl_NewListObj(0, NULL);
   Tcl_ListObjAppendElement(interp, lobj, Tcl_NewIntObj(usb_pid));
   Tcl_ListObjAppendElement(interp, lobj, Tcl_NewIntObj(usb_vid));
   Tcl_SetObjResult(interp, lobj);
   return TCL_OK;
}
 
/*--------------------------------------------------------------*/
/* Tcl function "ftdi_get"					*/
/* Read status of byte values on device cbus			*/
/*--------------------------------------------------------------*/

int
ftditcl_get(ClientData clientData,
	Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   int val;
   unsigned char flags;
   unsigned char tbuffer[4];
   unsigned char rbuffer[4];

   long numWritten;
   long numRead;
   ftdi_record *ftRecord;
   struct ftdi_context * ftContext;
   int ftStatus;
 
   if (objc <= 1) {
     Tcl_SetResult(interp, "get: Need device name\n", NULL);
     return TCL_ERROR;
   }

   ftRecord = find_record(Tcl_GetString(objv[1]), &ftContext);
   if (ftContext == (struct ftdi_context *)NULL) {
      Tcl_SetResult(interp, "get:  No such device\n", NULL);
      return TCL_ERROR;
   }
   else flags = ftRecord->flags;

   tbuffer[0] = 0x83;		// Read high byte (i.e., Cbus)

   if (verbose > 1) {
      int i;
      Fprintf(interp, stderr, "ftdi_get: Writing: ");
      for (i = 0; i < 1; i++) {
         Fprintf(interp, stderr, "0x%02x ", tbuffer[i]);
      }
      Fprintf(interp, stderr, "\n");
   }

   ftStatus = ftdi_write_data(ftContext, tbuffer, 1);
   if (ftStatus < 0)
      Tcl_SetResult(interp, "Received error while reading Dbus\n", NULL);
   else if (ftStatus != 1)
      Tcl_SetResult(interp, "get:  short write error.\n", NULL);

   ftStatus = ftdi_read_data(ftContext, rbuffer, 1);
   if (ftStatus < 0)
      Tcl_SetResult(interp, "Received error while reading Dbus\n", NULL);
   else if (ftStatus != 1)
      Tcl_SetResult(interp, "get:  short read error.\n", NULL);

   Tcl_SetObjResult(interp, Tcl_NewIntObj((int)rbuffer[0]));
   return TCL_OK;
}

/*--------------------------------------------------------------*/
/* Tcl function "ftdi_verbose"					*/
/*--------------------------------------------------------------*/

int
ftditcl_verbose(ClientData clientData,
	Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
  int level, result;
 
  if (objc <= 1) {
     Tcl_SetObjResult(interp, Tcl_NewIntObj((int)verbose));
     return TCL_OK;
  }

  result = Tcl_GetIntFromObj(interp, objv[1], &level);
  if (result != TCL_OK) return result;
  if (level < 0) {
      Tcl_SetResult(interp, "verbose: Bad verbose level\n", NULL);
      return TCL_ERROR;
  }

  verbose = level;
  return TCL_OK;
}

/*--------------------------------------------------------------*/
/* Tcl function "ftdi::spi_command":  Set the word length of	*/
/* the SPI command word, which can accomodate lengths other	*/
/* than the default 8.	In bit-bang mode, the bit length is	*/
/* arbitrary.  In MPSSE mode, the bit length must be a multiple	*/
/* of 8.							*/
/*--------------------------------------------------------------*/

int
ftditcl_spi_command(ClientData clientData,
	Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   ftdi_record *ftRecord;
   unsigned char flags;
   int cmdwidth, result;

   if (objc != 3) {
      Tcl_SetResult(interp, "spi_command: Need handle and integer value.\n", NULL);
      return TCL_ERROR;
   }
   ftRecord = find_record(Tcl_GetString(objv[1]), NULL);
   if (ftRecord == (ftdi_record *)NULL) {
      Tcl_SetResult(interp, "spi_command:  No such device\n", NULL);
      return TCL_ERROR;
   }
   else flags = ftRecord->flags;

   if (flags & LEGACY_MODE) {
      Tcl_SetResult(interp, "spi_command:  Cannot change command word "
		"length in legacy mode\n", NULL);
      return TCL_ERROR;
   }

   result = Tcl_GetIntFromObj(interp, objv[2], &cmdwidth);
   if (result != TCL_OK) return result;

   if (cmdwidth > 64) {
      Tcl_SetResult(interp, "spi_command:  Command word "
		"maximum length is 64 bits.\n", NULL);
      return TCL_ERROR;
   }

   ftRecord->cmdwidth = cmdwidth;
   return TCL_OK;
}

/*--------------------------------------------------------------*/
/* Tcl function "ftdi::disable":  Disable a channel by	*/
/* putting it into bitbang mode and setting all bits to input	*/
/* so that they are all undriven.				*/
/*--------------------------------------------------------------*/

int
ftditcl_disable(ClientData clientData,
	Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   ftdi_record *ftRecord;
   struct ftdi_context * ftContext;
   int ftStatus;

   long numWritten;

   int result, i, j, k;
   unsigned char *values;
   unsigned char flags;
   unsigned char sigio;
   unsigned char *sigpins;
   int len, loclen, bangmode;

   if (objc != 2) {
      Tcl_SetResult(interp, "disable: Need device name.\n", NULL);
      return TCL_ERROR;
   }
   ftRecord = find_record(Tcl_GetString(objv[1]), &ftContext);
   if (ftRecord == (ftdi_record *)NULL) {
      Tcl_SetResult(interp, "disable:  No such device\n", NULL);
      return TCL_ERROR;
   }
   flags = ftRecord->flags;
   sigpins = &(ftRecord->sigpins[0]);

   ftRecord->flags |= BITBANG_MODE;
   ftRecord->wordwidth = 8;
   sigio = 0x00;		// Everything is an input

   // Mark all signal pins as unassigned.
   for (i = 0; i < 8; i++) sigpins[i] = 0x00;

   // Reset the FTDI device
   ftStatus = ftdi_usb_reset(ftContext);
   if (ftStatus < 0)
      Tcl_SetResult(interp, "Received error while resetting device.\n", NULL);

   // Set baudrate to default (Note: actual bits per second is 16 times the value)
   // So 62500 baud = 1Mbps.  However, SCK clock takes two transmissions (up, down)
   // so double this value to get a 1Mpbs SCK, or 125000.
   ftStatus = ftdi_set_baudrate(ftContext, (long)125000);
   if (ftStatus < 0)
      Tcl_SetResult(interp, "Received error while setting baud rate.\n", NULL);

   // Set device to Synchronous bit-bang mode.

   ftStatus = ftdi_set_bitmode(ftContext, (unsigned char)sigio,
		(unsigned char)BITMODE_SYNCBB);
   if (ftStatus < 0)
      Tcl_SetResult(interp, "Received error while setting bit mode.\n", NULL);

   ftStatus = ftdi_usb_purge_tx_buffer(ftContext);
   if (ftStatus < 0)
      Tcl_SetResult(interp, "Received error while purging transmit buffer.\n", NULL);

   ftStatus = ftdi_usb_purge_rx_buffer(ftContext);
   if (ftStatus < 0)
      Tcl_SetResult(interp, "Received error while purging receive buffer.\n", NULL);

   // Set latency timer (in ms) (legacy case is 16; FT2232 minimum 1)
   ftStatus = ftdi_set_latency_timer(ftContext, (unsigned char)5);
   if (ftStatus < 0)
      Tcl_SetResult(interp, "Received error while setting latency timer.\n", NULL);

   // Return
   return TCL_OK;
}



/*--------------------------------------------------------------*/
/* Tcl function "ftdi::spi_bitbang": Set or disable SPI bit-	*/
/* bang mode.  If setting, optional arguments may specify the	*/
/* order of pins.						*/
/*--------------------------------------------------------------*/

// Local indexes for bitbang signals
#define BB_CSB 0
#define BB_SDO 1
#define BB_SDI 2
#define BB_SCK 3
#define BB_USR0 4
#define BB_USR1 5
#define BB_USR2 6
#define BB_USR3 7

int
ftditcl_spi_bitbang(ClientData clientData,
	Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   ftdi_record *ftRecord;
   struct ftdi_context * ftContext;
   int ftStatus;

   long numWritten;

   int result, i, j, k;
   unsigned char *values;
   unsigned char *sigpins;
   unsigned char tbuffer[1];
   unsigned char flags;
   unsigned char sigio, sigassn;
   int len, loclen, bangmode;
   char *sigchar;
   Tcl_Obj *sigpair, *sigval;

   if (objc != 3) {
      Tcl_SetResult(interp, "spi_bitbang: Need device name and argument.\n", NULL);
      return TCL_ERROR;
   }
   ftRecord = find_record(Tcl_GetString(objv[1]), &ftContext);
   if (ftRecord == (ftdi_record *)NULL) {
      Tcl_SetResult(interp, "spi_bitbang:  No such device\n", NULL);
      return TCL_ERROR;
   }
   flags = ftRecord->flags;
   sigpins = &(ftRecord->sigpins[0]);

   // NOTE:  locally, signals will be indexed according to definitions
   // above for BB_CSB, BB_SDO, BB_SDI, BB_SCK

   result = Tcl_ListObjLength(interp, objv[2], &len);
   if (len == 1) {
      result = Tcl_GetBooleanFromObj(interp, objv[2], &bangmode);
      if (result != TCL_OK) return result;
      if (bangmode) {
	 /* Set default signals. */
	 ftRecord->flags |= BITBANG_MODE;
	 ftRecord->wordwidth = 8;
	 sigpins[7] = 0x80;	// ADBUS7 = USR3
	 sigpins[6] = 0x40;	// ADBUS6 = USR2
	 sigpins[5] = 0x20;	// ADBUS5 = USR1
	 sigpins[4] = 0x10;	// ADBUS4 = USR0
	 sigpins[3] = 0x08;	// ADBUS3 = SCK
	 sigpins[2] = 0x04;	// ADBUS2 = SDI
	 sigpins[1] = 0x02;	// ADBUS1 = SDO
	 sigpins[0] = 0x01;	// ADBUS0 = CSB
	 sigio = 0xfd;		// 1 = output, 0 = input
      }
      else {
	 // Turn off bit bang mode, return to MPSSE mode
	 ftRecord->flags &= ~BITBANG_MODE;
	 ftRecord->wordwidth = 0;
	 Tcl_SetResult(interp, "spi_bitbang:  Unimplemented option. "
		" Close and reopen device to reset mode.\n", NULL);
	 return TCL_ERROR;
      }
   }
   else {
      /* Declare SPI signals.  Each should be a list of length two */
      ftRecord->flags |= BITBANG_MODE;
      ftRecord->wordwidth = 8;
      sigio = 0xff;
      sigassn = 0x00;
      for (i = 0; i < 8; i++) sigpins[i] = 0;
      if (len < 4) {
	 Tcl_SetResult(interp, "spi_bitbang:  Must at least declare "
		"SCK, SDI, SDO, and CSB\n", NULL);
	 return TCL_ERROR;
      }
      else if (len > 8) {
	 Tcl_SetResult(interp, "spi_bitbang:  Can only define up to 8 signals\n",
			NULL);
	 return TCL_ERROR;
      }

      for (i = 0; i < len; i++) {
         result = Tcl_ListObjIndex(interp, objv[2], i, &sigpair);
	 result = Tcl_ListObjLength(interp, sigpair, &loclen);
	 if (loclen != 2) {
	    Tcl_SetResult(interp, "spi_bitbang:  List of signals must be in"
			"form {signal, bit}. bit is 0 to 7\n", NULL);
	    return TCL_ERROR;
	 }
	 result = Tcl_ListObjIndex(interp, sigpair, 0, &sigval);

	 // Recast from i (order in argument list) to j (signal
	 // canonical order, 0 = CSB, 1 = SDO, 2 = SDI, 3 = SCK

	 if (!strcasecmp(Tcl_GetString(sigval), "CSB"))
	    j = BB_CSB;
	 else if (!strcasecmp(Tcl_GetString(sigval), "SDO"))
	    j = BB_SDO;
	 else if (!strcasecmp(Tcl_GetString(sigval), "SDI"))
	    j = BB_SDI;
	 else if (!strcasecmp(Tcl_GetString(sigval), "SCK"))
	    j = BB_SCK;
	 else if (!strcasecmp(Tcl_GetString(sigval), "USR0"))
	    j = BB_USR0;
	 else if (!strcasecmp(Tcl_GetString(sigval), "USR1"))
	    j = BB_USR1;
	 else if (!strcasecmp(Tcl_GetString(sigval), "USR2"))
	    j = BB_USR2;
	 else if (!strcasecmp(Tcl_GetString(sigval), "USR3"))
	    j = BB_USR3;
	 else {
	    Tcl_SetResult(interp, "spi_bitbang:  Unknown signal name.  Must be "
			"one of CSB, SDO, SDI, SCK, or USR0 to USR3\n", NULL);
	    return TCL_ERROR;
	 }
	 result = Tcl_ListObjIndex(interp, sigpair, 1, &sigval);
	 result = Tcl_GetIntFromObj(interp, sigval, &k);
	 sigpins[j] = 0x01 << k;  // Convert bit number to bit mask

	 if (j == BB_SDO) sigio &= ~sigpins[j];
	 sigassn |= sigpins[j];
      }

      // Check that all pins are assigned.  
      j = 0;
      for (i = 0; i < 8; i++) if (sigassn & (1 << i)) j++;
      if (j != len) {
	 Tcl_SetResult(interp, "spi_bitbang:  Not all signals assigned.  Must "
			"assign all of CSB, SDO, SDI, and SCK\n", NULL);
	 return TCL_ERROR;
      }
   }

   // Reset the FTDI device
   ftStatus = ftdi_usb_reset(ftContext);
   if (ftStatus < 0)
      Tcl_SetResult(interp, "Received error while resetting device.\n", NULL);

   // Set baudrate to default (Note: actual bits per second is 16 times the value)
   // So 62500 baud = 1Mbps.  However, SCK clock takes two transmissions (up, down)
   // so double this value to get a 1Mpbs SCK, or 125000.
   ftStatus = ftdi_set_baudrate(ftContext, (long)125000);
   if (ftStatus < 0)
      Tcl_SetResult(interp, "Received error while setting baud rate.\n", NULL);

   // Set device to Synchronous bit-bang mode, with pins SCK, SDI, and CSB
   // set to output, SDO to input (bitbang mode defined as 0x01, should use
   // defines from ftdi.h).

   ftStatus = ftdi_set_bitmode(ftContext, (unsigned char)sigio,
		(unsigned char)BITMODE_SYNCBB);
   if (ftStatus < 0)
      Tcl_SetResult(interp, "Received error while setting bit mode.\n", NULL);

   ftStatus = ftdi_usb_purge_tx_buffer(ftContext);
   if (ftStatus < 0)
      Tcl_SetResult(interp, "Received error while purging transmit buffer.\n", NULL);

   ftStatus = ftdi_usb_purge_rx_buffer(ftContext);
   if (ftStatus < 0)
      Tcl_SetResult(interp, "Received error while purging receive buffer.\n", NULL);

   // Set latency timer (in ms) (legacy case is 16; FT2232 minimum 1)
   ftStatus = ftdi_set_latency_timer(ftContext, (unsigned char)5);
   if (ftStatus < 0)
      Tcl_SetResult(interp, "Received error while setting latency timer.\n", NULL);

   // Set timeouts (in ms) (no libftdi equivalent to D2XX FT_SetTimeouts()?)
   /*
   ftStatus = ftdi_set_timeouts(ftContext, 1000, 1000);
   if (ftStatus < 0)
      Tcl_SetResult(interp, "Received error while setting timeouts.\n", NULL);
   */

   // Set default values CSB = 1, SDI = 0, SCK = 0, SDO = don't care
   tbuffer[0] = sigpins[BB_CSB];

   if (verbose > 1) {
      Fprintf(interp, stderr, "spi_bitbang: Writing: ");
      for (i = 0; i < 1; i++) {
         Fprintf(interp, stderr, "0x%02x ", tbuffer[i]);
      }
      Fprintf(interp, stderr, "\n");
   }

   ftStatus = ftdi_write_data(ftContext, tbuffer, 1);
   if (ftStatus < 0)
      Tcl_SetResult(interp, "Received error while writing init data\n", NULL);
    else if (ftStatus != 1)
      Tcl_SetResult(interp, "Short write error\n", NULL);

   // Return
   return TCL_OK;
}

/*--------------------------------------------------------------*/
/* Tcl function "ftdi::bitbang_word":  Set the word length of	*/
/* the SPI in bit-bang mode, which can accomodate lengths other	*/
/* than the default 8.						*/
/*--------------------------------------------------------------*/

int
ftditcl_bang_word(ClientData clientData,
	Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   ftdi_record *ftRecord;
   unsigned char flags;
   int wordwidth, result;

   if (objc != 3) {
      Tcl_SetResult(interp, "bitbang_word: Need handle and integer value.\n", NULL);
      return TCL_ERROR;
   }
   ftRecord = find_record(Tcl_GetString(objv[1]), NULL);
   if (ftRecord == (ftdi_record *)NULL) {
      Tcl_SetResult(interp, "bitbang_word:  No such device\n", NULL);
      return TCL_ERROR;
   }
   else flags = ftRecord->flags;

   if (!(flags & BITBANG_MODE)) {
      Tcl_SetResult(interp, "bitbang_word: bit-bang mode must be set first.\n", NULL);
      return TCL_ERROR;
   }

   result = Tcl_GetIntFromObj(interp, objv[2], &wordwidth);
   if (result != TCL_OK) return result;

   ftRecord->wordwidth = wordwidth;
   return TCL_OK;
}

/*--------------------------------------------------------------*/
/* Tcl function "ftdi::bitbang_write":				*/
/*--------------------------------------------------------------*/

int
ftditcl_bang_write(ClientData clientData,
	Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   int result, nbytes;
   int wordcount, i, j, value, tidx;
   Tcl_WideInt regnum;
   unsigned char wordwidth;
   unsigned char cmdwidth;
   unsigned char flags;
   unsigned char *tbuffer;
   unsigned char *sigpins;
   Tcl_Obj *vector, *lobj;

   long numWritten;
   ftdi_record *ftRecord;
   struct ftdi_context * ftContext;
   int ftStatus;

   if (objc != 4) {
      Tcl_SetResult(interp, "bitbang_write: Need device name, "
		"register, and vector of values.\n", NULL);
      return TCL_ERROR;
   }
   ftRecord = find_record(Tcl_GetString(objv[1]), &ftContext);
   if (ftContext == (struct ftdi_context *)NULL) {
      Tcl_SetResult(interp, "bitbang_write:  No such device\n", NULL);
      return TCL_ERROR;
   }
   flags = ftRecord->flags;
   wordwidth = ftRecord->wordwidth;
   cmdwidth = ftRecord->cmdwidth;
   sigpins = &(ftRecord->sigpins[0]);

   // If we're not in bit-bang mode, use the normal spi_write.

   if (!(flags & BITBANG_MODE)) {
      result = ftditcl_spi_write(clientData, interp, objc, objv);
      return result;
   }

   // First argument is register number.  This may or may not include
   // additional information such as an opcode.  If so, it is the
   // responsibility of the end-user to make sure that the opcode
   // matches the use of routine "read" or "write".

   result = Tcl_GetWideIntFromObj(interp, objv[2], &regnum);
   if (result != TCL_OK) return result;

   vector = objv[3];
   result = Tcl_ListObjLength(interp, vector, &wordcount);
   if (result != TCL_OK) return result;

   for (i = 0; i < wordcount; i++) {
      result = Tcl_ListObjIndex(interp, vector, i, &lobj);
      if (result != TCL_OK) return result;
      result = Tcl_GetIntFromObj(interp, lobj, &value);

      if (value < 0 || value > 255) {
         Tcl_SetResult(interp, "bitbang_write:  Byte value out of range 0-255\n",
		NULL);
	 return TCL_ERROR;
      }
   }

   // Create complete vector to write in synchronous bit-bang
   // mode.

   nbytes = ((cmdwidth + wordcount * wordwidth) * 2) + 2;
   tbuffer = (unsigned char *)malloc(nbytes * sizeof(unsigned char));
   tidx = 0;
 
   // Assert CSB
   tbuffer[tidx++] = (unsigned char)0;

   // Write command/register word msb first
   for (j = cmdwidth - 1; j >= 0; j--) {
      // input changes on falling edge of SCK
      tbuffer[tidx++] = (regnum & ((Tcl_WideInt)1 << j)) ? sigpins[BB_SDI] : 0;
      tbuffer[tidx] = tbuffer[tidx - 1] | sigpins[BB_SCK];
      tidx++;
   }

   for (i = 0; i < wordcount; i++) {
      result = Tcl_ListObjIndex(interp, vector, i, &lobj);
      result = Tcl_GetIntFromObj(interp, lobj, &value);
      for (j = wordwidth - 1; j >= 0; j--) {
	 // input changes on falling edge of SCK
	 tbuffer[tidx++] = (value & (1 << j)) ? sigpins[BB_SDI] : 0;
	 tbuffer[tidx] = tbuffer[tidx - 1] | sigpins[BB_SCK];
	 tidx++;
      }
   }

   // De-assert CSB
   tbuffer[tidx++] = (flags & CSB_NORAISE) ? (unsigned char)0 :
		(unsigned char)sigpins[BB_CSB];

   if (verbose > 1) {
      Fprintf(interp, stderr, "bitbang_write: Writing: ");
      for (i = 0; i < nbytes; i++) {
         Fprintf(interp, stderr, "0x%02x ", tbuffer[i]);
      }
      Fprintf(interp, stderr, "\n");
   }

   // Purge TX buffer
   ftStatus = ftdi_usb_purge_tx_buffer(ftContext);
   if (ftStatus < 0)
      Tcl_SetResult(interp, "Received error while purging transmit buffer.\n", NULL);

   // SPI write using bit bang
   ftStatus = ftdi_write_data(ftContext, tbuffer, nbytes);
   if (ftStatus < 0)
      Tcl_SetResult(interp, "Received error while writing SPI.\n", NULL);
   else if (ftStatus != nbytes)
      Tcl_SetResult(interp, "bitbang write:  short write error.\n", NULL);

   free(tbuffer);
   return TCL_OK;
}

/*--------------------------------------------------------------*/
/* Tcl function "ftdi::bitbang_set":				*/
/* Apply individual signal changes.				*/
/* Use:  bitbang_set <device> <pinlist> ...			*/
/* Pinlist contains all pins to be set (null list if all pins	*/
/* should be cleared).  Use multiple lists for operations to be	*/
/* separated by one clock cycle.				*/
/* Ex: "bitbang_set ftdi0 {SDI SCK} {SDI} {}"			*/
/*--------------------------------------------------------------*/

int
ftditcl_bang_set(ClientData clientData,
	Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   int result, numobj, nbytes;
   int i, j, k, value, tidx;
   unsigned char flags;
   unsigned char *tbuffer;
   unsigned char *sigpins;
   Tcl_Obj *vector, *lobj;

   long numWritten;
   ftdi_record *ftRecord;
   struct ftdi_context * ftContext;
   int ftStatus;

   if (objc < 2) {
      Tcl_SetResult(interp, "bitbang_set: Need device name and at least "
		"one pin and value pair.\n", NULL);
      return TCL_ERROR;
   }
   ftRecord = find_record(Tcl_GetString(objv[1]), &ftContext);
   if (ftContext == (struct ftdi_context *)NULL) {
      Tcl_SetResult(interp, "bitbang_set:  No such device\n", NULL);
      return TCL_ERROR;
   }
   flags = ftRecord->flags;
   sigpins = &(ftRecord->sigpins[0]);

   nbytes = objc - 2;
   tbuffer = (unsigned char *)malloc(nbytes * sizeof(unsigned char));

   for (i = 0; i < objc - 2; i++) {

      /* Parse pin list and apply value */
      result = Tcl_ListObjLength(interp, objv[i + 2], &numobj);
      if (result != TCL_OK) return result;
      if (numobj > 8) {
	 Tcl_SetResult(interp, "Each entry must be a list of pins\n", NULL);
	 free(tbuffer);
	 return TCL_ERROR;
      }
      tbuffer[i] = 0;
      for (k = 0; k < numobj; k++) {
         result = Tcl_ListObjIndex(interp, objv[i + 2], k, &lobj);
         if (result != TCL_OK) {
	    free(tbuffer);
	    return result;
	 }
         if (!strcasecmp(Tcl_GetString(lobj), "CSB"))
	    j = BB_CSB;
         else if (!strcasecmp(Tcl_GetString(lobj), "SDO"))
	    j = BB_SDO;
         else if (!strcasecmp(Tcl_GetString(lobj), "SDI"))
	    j = BB_SDI;
         else if (!strcasecmp(Tcl_GetString(lobj), "SCK"))
	    j = BB_SCK;
         else if (!strcasecmp(Tcl_GetString(lobj), "USR0"))
	    j = BB_USR0;
         else if (!strcasecmp(Tcl_GetString(lobj), "USR1"))
	    j = BB_USR1;
         else if (!strcasecmp(Tcl_GetString(lobj), "USR2"))
	    j = BB_USR2;
         else if (!strcasecmp(Tcl_GetString(lobj), "USR3"))
	    j = BB_USR3;
         else {
	    Tcl_SetResult(interp, "bitbang_set:  Unknown signal name.  "
			"Must be one of CSB, SDO, SDI, or SCK\n", NULL);
	    free(tbuffer);
	    return TCL_ERROR;
         }
         tbuffer[i] |= sigpins[j];
      }
      // Fprintf(interp, stderr, "Byte %d set to %d\n", i, tbuffer[i]);
   }
   // Fprintf(interp, stderr, "Writing %d bytes\n", nbytes);

   if (verbose > 1) {
      Fprintf(interp, stderr, "bitbang_set: Writing: ");
      for (i = 0; i < nbytes; i++) {
         Fprintf(interp, stderr, "0x%02x ", tbuffer[i]);
      }
      Fprintf(interp, stderr, "\n");
   }

   // Simple bit bang write
   ftStatus = ftdi_write_data(ftContext, tbuffer, nbytes);
   if (ftStatus < 0)
      Tcl_SetResult(interp, "Received error while banging bits.\n", NULL);
   else if (ftStatus != nbytes)
      Tcl_SetResult(interp, "bitbang set:  short write error.\n", NULL);

   free(tbuffer);
   return TCL_OK;
}

/*--------------------------------------------------------------*/
/* Tcl function "ftdi::bitbang_read":				*/
/*--------------------------------------------------------------*/

int
ftditcl_bang_read(ClientData clientData,
	Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   int result, nbytes, numRead;
   int wordcount, i, j, value, tidx;
   Tcl_WideInt regnum;
   unsigned char flags;
   unsigned char *tbuffer;
   unsigned char wordwidth;
   unsigned char cmdwidth;
   unsigned char *sigpins;
   Tcl_Obj *vector, *lobj;

   long numWritten;
   ftdi_record *ftRecord;
   struct ftdi_context * ftContext;
   int ftStatus;

   if (objc != 4) {
      Tcl_SetResult(interp, "bitbang_read: Need device name, "
		"register, and word count.\n", NULL);
      return TCL_ERROR;
   }
   ftRecord = find_record(Tcl_GetString(objv[1]), &ftContext);
   if (ftContext == (struct ftdi_context *)NULL) {
      Tcl_SetResult(interp, "bitbang_read:  No such device\n", NULL);
      return TCL_ERROR;
   }
   flags = ftRecord->flags;
   wordwidth = ftRecord->wordwidth;
   cmdwidth = ftRecord->cmdwidth;
   sigpins = &(ftRecord->sigpins[0]);

   // If we're not in bit-bang mode, use the normal spi_read.

   if (!(flags & BITBANG_MODE)) {
      result = ftditcl_spi_read(clientData, interp, objc, objv);
      return result;
   }

   // First argument is register number.  This may or may not include
   // additional information such as an opcode.  If so, it is the
   // responsibility of the end-user to make sure that the opcode
   // matches the use of routine "read" or "write".

   result = Tcl_GetWideIntFromObj(interp, objv[2], &regnum);
   if (result != TCL_OK) return result;
   result = Tcl_GetIntFromObj(interp, objv[3], &wordcount);
   if (result != TCL_OK) return result;

   // Create complete vector to write in synchronous bit-bang mode.

   nbytes = ((cmdwidth + wordcount * wordwidth) * 2) + 2;
   tbuffer = (unsigned char *)malloc(nbytes * sizeof(unsigned char));
   tidx = 0;
 
   // Assert CSB
   tbuffer[tidx++] = (unsigned char)0;

   // Write command/register word msb first
   for (j = cmdwidth - 1; j >= 0; j--) {
      // input changes on falling edge of SCK
      tbuffer[tidx++] = (regnum & ((Tcl_WideInt)1 << j)) ? sigpins[BB_SDI] : 0;
      tbuffer[tidx] = tbuffer[tidx - 1] | sigpins[BB_SCK];
      tidx++;
   }

   for (i = 0; i < wordcount; i++) {
      for (j = 0; j < wordwidth; j++) {
	 // Drive clock by bit-bang
	 tbuffer[tidx++] = 0;
	 tbuffer[tidx++] = sigpins[BB_SCK];
      }
   }

   // De-assert CSB
   tbuffer[tidx++] = (flags & CSB_NORAISE) ? (unsigned char)0 :
		(unsigned char)sigpins[BB_CSB];

   // Purge read buffer
   ftStatus = ftdi_usb_purge_rx_buffer(ftContext);
   if (ftStatus < 0)
      Tcl_SetResult(interp, "Received error while purging SPI RX.\n", NULL);

   // Purge write buffer
   ftStatus = ftdi_usb_purge_tx_buffer(ftContext);
   if (ftStatus < 0)
      Tcl_SetResult(interp, "Received error while purging SPI TX.\n", NULL);

   if (verbose > 1) {
      Fprintf(interp, stderr, "bitbang_read: Writing: ");
      for (i = 0; i < nbytes; i++) {
         Fprintf(interp, stderr, "0x%02x ", tbuffer[i]);
      }
      Fprintf(interp, stderr, "\n");
   }

   // SPI write using bit bang
   ftStatus = ftdi_write_data(ftContext, tbuffer, nbytes);
   if (ftStatus < 0)
      Tcl_SetResult(interp, "Received error while writing SPI.\n", NULL);
   else if (ftStatus != nbytes)
      Tcl_SetResult(interp, "SPI write:  short write error.\n", NULL);

   // SPI read using bit bang
   ftStatus = ftdi_read_data(ftContext, tbuffer, nbytes);
   if (ftStatus < 0)
      Tcl_SetResult(interp, "Received error while reading SPI.\n", NULL);
   else if (ftStatus != nbytes)
      Tcl_SetResult(interp, "SPI read:  short read error.\n", NULL);

   vector = Tcl_NewListObj(0, NULL);

   tidx = 3 + 2 * cmdwidth;	// No readback during reg/command write
   for (i = 0; i < wordcount; i++) {
      value = 0;
      for (j = wordwidth - 1; j >= 0; j--) {
	 if (tidx >= nbytes) {
	    value = -1;
	    break;
	 }
	 if (tbuffer[tidx] & sigpins[BB_SDO]) {
	    value |= (1 << j);
	 }
	 tidx += 2;
      }
      Tcl_ListObjAppendElement(interp, vector, Tcl_NewIntObj(value));
   }

   // Should be nothing left in the read buffer, but just in case. . . 
   unsigned int x = ftContext->readbuffer_remaining;
   if (x != 0) {
       unsigned char *xbuffer;

       Fprintf(interp, stderr, "%d words still remaining in buffer\n", x);
       xbuffer = (unsigned char *)malloc(x * sizeof(unsigned char));
       ftStatus = ftdi_read_data(ftContext, xbuffer, x);

       // for (tidx = 0; tidx < x; tidx++) {
       //    value = xbuffer[tidx];
       //    Tcl_ListObjAppendElement(interp, vector, Tcl_NewIntObj(value));
       // }

       free(xbuffer);
   }

   Tcl_SetObjResult(interp, vector);
   free(tbuffer);
   return TCL_OK;
}

/*--------------------------------------------------------------*/
/* Tcl function "ftdi::spi_csb_mode":  Change the behavior of	*/
/* the SPI with respect to CSB.  This function takes one	*/
/* integer argument in addition to the device name:		*/
/*								*/
/*	0: CSB is not de-asserted after each read and write	*/
/*	1: CSB is de-asserted after each read and write		*/
/*								*/
/* NOTE: Currently this is only defined for bit-bang mode.	*/
/*--------------------------------------------------------------*/

int
ftditcl_spi_csb_mode(ClientData clientData,
	Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   ftdi_record *ftRecord;
   struct ftdi_context * ftContext;
   int ftStatus;

   long numWritten;

   int result;
   unsigned char *tbuffer;
   unsigned char flags;
   unsigned char *sigpins;
   int mode;

   if (objc != 3) {
      Tcl_SetResult(interp, "spi_csb_mode: Need device name and 0 or 1.\n", NULL);
      return TCL_ERROR;
   }
   ftRecord = find_record(Tcl_GetString(objv[1]), &ftContext);
   if (ftContext == (struct ftdi_context *)NULL) {
      Tcl_SetResult(interp, "spi_speed:  No such device\n", NULL);
      return TCL_ERROR;
   }
   else flags = ftRecord->flags;
   sigpins = &(ftRecord->sigpins[0]);

   result = Tcl_GetIntFromObj(interp, objv[2], &mode);
   if (result != TCL_OK) return result;

   switch (mode) {
      case 0:
	 ftRecord->flags &= ~CSB_NORAISE;

	 // In addition to setting flags, raise CSB immediately.
	 // So "spi_csb_mode 0" can be used to control when CSB
	 // is deasserted.

         tbuffer = (unsigned char *)malloc(sizeof(unsigned char));
         tbuffer[0] = (unsigned char)sigpins[BB_CSB];

         if (verbose > 1) {
	    int i;
            Fprintf(interp, stderr, "spi_csb_mode: Writing: ");
            for (i = 0; i < 1; i++) {
               Fprintf(interp, stderr, "0x%02x ", tbuffer[i]);
            }
            Fprintf(interp, stderr, "\n");
         }

         ftStatus = ftdi_write_data(ftContext, tbuffer, 1);
         if (ftStatus < 0)
            Tcl_SetResult(interp, "Received error while writing SPI.\n", NULL);
         else if (ftStatus != 1)
            Tcl_SetResult(interp, "bitbang write:  short write error.\n", NULL);

         free(tbuffer);
	 break;

      case 1:
	 ftRecord->flags |= CSB_NORAISE;
	 break;
   }
   return TCL_OK;
}

/*--------------------------------------------------------------*/
/* Tcl function "ftdi::spi_speed":  Set the SPI clock speed of	*/
/* the FTDI MPSSE SPI protocol.					*/
/*--------------------------------------------------------------*/

int
ftditcl_spi_speed(ClientData clientData,
	Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   ftdi_record *ftRecord;
   struct ftdi_context * ftContext;
   int ftStatus;

   long numWritten;

   int result;
   unsigned char *values;
   unsigned char tbuffer[6];
   unsigned char flags;
   double mhz;
   int ival;

   if (objc != 3) {
      Tcl_SetResult(interp, "spi_speed: Need device name and value (in MHz).\n", NULL);
      return TCL_ERROR;
   }
   ftRecord = find_record(Tcl_GetString(objv[1]), &ftContext);
   if (ftContext == (struct ftdi_context *)NULL) {
      Tcl_SetResult(interp, "spi_speed:  No such device\n", NULL);
      return TCL_ERROR;
   }
   else flags = ftRecord->flags;

   result = Tcl_GetDoubleFromObj(interp, objv[2], &mhz);
   if (result != TCL_OK) return result;

   if (flags & BITBANG_MODE) {
      ftStatus = ftdi_set_baudrate(ftContext, (long)125000);
      if (ftStatus < 0) {
	 Tcl_SetResult(interp, "Received error while setting baud rate.\n", NULL);
	 return TCL_ERROR;
      }

      // Bitbang rate calculation:  bitbang update rate is the baud rate
      // * 16, but SCK takes two transmissions (up, down), so SCK rate is
      // the baud rate * 8.

      ftStatus = ftdi_set_baudrate(ftContext, (long)((mhz / 8.0) * 1.0E6));
      if (ftStatus < 0) {
         Tcl_SetResult(interp, "Received error while setting baud rate.\n", NULL);
	 return TCL_ERROR;
      }
      return TCL_OK;
   }

   /* Convert double into two bytes in tbuffer */

   ival = (int)((30.0 / mhz) - 1.0);

   tbuffer[0] = 0x8a;        // Enable high-speed clock (x5, up to 30MHz)
   tbuffer[1] = 0x86;        // Set clock divider
   tbuffer[2] = ival & 0xff;
   tbuffer[3] = (ival >> 8) & 0xff;

   if (verbose > 1) {
      int i;
      Fprintf(interp, stderr, "spi_speed: Writing: ");
      for (i = 0; i < 4; i++) {
         Fprintf(interp, stderr, "0x%02x ", tbuffer[i]);
      }
      Fprintf(interp, stderr, "\n");
   }

   ftStatus = ftdi_write_data(ftContext, tbuffer, 4);
   if (ftStatus < 0)
      Tcl_SetResult(interp, "Received error while setting SPI"
		" clock speed.\n", NULL);
   else if (ftStatus != 4)
      Tcl_SetResult(interp, "SPI read:  short write error.\n", NULL);

   return TCL_OK;
}

/*--------------------------------------------------------------*/
/* Tcl function "ftdi::spi_read":				*/
/*--------------------------------------------------------------*/

int
ftditcl_spi_read(ClientData clientData,
	Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   int result;
   int bytecount, i, j;
   int cmdcount;
   Tcl_WideInt regnum;
   unsigned char *values;
   unsigned char tbuffer[13];
   unsigned char flags;
   Tcl_Obj *vector;

   long numWritten;
   long numRead;
   ftdi_record *ftRecord;
   struct ftdi_context * ftContext;
   int ftStatus;

   if (objc != 4) {
      Tcl_SetResult(interp, "spi_read: Need device name, command, "
		"and byte count.\n", NULL);
      return TCL_ERROR;
   }
   ftRecord = find_record(Tcl_GetString(objv[1]), &ftContext);
   if (ftContext == (struct ftdi_context *)NULL) {
      Tcl_SetResult(interp, "spi_read:  No such device\n", NULL);
      return TCL_ERROR;
   }
   else flags = ftRecord->flags;

   if (flags & BITBANG_MODE) {
      result = ftditcl_bang_read(clientData, interp, objc, objv);
      return result;
   }

   result = Tcl_GetWideIntFromObj(interp, objv[2], &regnum);
   if (result != TCL_OK) return result;
   result = Tcl_GetIntFromObj(interp, objv[3], &bytecount);
   if (result != TCL_OK) return result;
   values = (unsigned char *)malloc(bytecount * sizeof(unsigned char));

   // Write values to MPSSE to generate the SPI read command

   tbuffer[0] = 0x80;        // Set Dbus
   tbuffer[1] = (flags & CS_INVERT) ? 0x08 : 0x00; // Assert CS
   tbuffer[2] = 0x0b;     // SCK, SDI, and CS are outputs
   tbuffer[3] = 0x11;     // Simple write command
   cmdcount = ftRecord->cmdwidth >> 3;
   if (flags & LEGACY_MODE)
      tbuffer[4] = 0x00;     // Length = 1;
   else
      tbuffer[4] = (unsigned char)(cmdcount - 1);
   tbuffer[5] = 0x00;     // (High byte is zero)
   // Command to send is "read register" + register no.
   if (flags & LEGACY_MODE)
      tbuffer[6] = ((flags & MIXED_MODE) ? 0x20 : 0x80) + (unsigned char)regnum;
   else {
      for (i = 0; i < cmdcount; i++) {
	 j = cmdcount - i - 1;
	 tbuffer[6 + i] = (unsigned char)((regnum >> (j << 3)) & 0xff);
      }
   }

   if (verbose > 1) {
      Fprintf(interp, stderr, "spi_read: Writing: ");
      for (i = 0; i < 6 + cmdcount; i++) {
         Fprintf(interp, stderr, "0x%02x ", tbuffer[i]);
      }
      Fprintf(interp, stderr, "\n");
   }

   ftStatus = ftdi_write_data(ftContext, tbuffer, 6 + cmdcount);
   if (ftStatus < 0)
      Tcl_SetResult(interp, "Received error while preparing SPI"
		" read command.\n", NULL);
   else if (ftStatus != 7)
      Tcl_SetResult(interp, "SPI read:  short write error.\n", NULL);

   /* This hack applies only to the DPLL demo board---SPI registers	*/
   /* require time to access!  Write the first 10 bytes, pause, then	*/
   /* do the rest.							*/

   if ((flags & LEGACY_MODE) && (regnum < 16)) {
      usleep(10);		// 10us delay for SPI transmission
   }

   tbuffer[0] = (flags & MIXED_MODE) ? 0x24 : 0x20;     // Simple read command
   // Number bytes to read (less one)
   tbuffer[1] = (unsigned char)((bytecount - 1) & 0xff);
   tbuffer[2] = (unsigned char)(((bytecount - 1) >> 8) & 0xff);
 
   tbuffer[3] = 0x80;	// Set Dbus
   tbuffer[4] = (flags & CS_INVERT) ? 0x00 : 0x08; // De-assert CS
   tbuffer[5] = 0x0b;	// SCK, SDI, and CS are outputs

   if (verbose > 1) {
      Fprintf(interp, stderr, "spi_read: Writing: ");
      for (i = 0; i < 6; i++) {
         Fprintf(interp, stderr, "0x%02x ", tbuffer[i]);
      }
      Fprintf(interp, stderr, "\n");
   }

   ftStatus = ftdi_write_data(ftContext, tbuffer, 6);
   if (ftStatus < 0)
      Tcl_SetResult(interp, "Received error while preparing SPI"
		" read command.\n", NULL);
   else if (ftStatus != 6)
      Tcl_SetResult(interp, "SPI read:  short write error.\n", NULL);

   // SPI read using MPSSE

   ftStatus = ftdi_read_data(ftContext, values, bytecount);
   if (ftStatus < 0)
      Tcl_SetResult(interp, "Received error in SPI read.\n", NULL);
   else if (ftStatus != bytecount)
      Tcl_SetResult(interp, "SPI short read error.\n", NULL);

   vector = Tcl_NewListObj(0, NULL);
   for (i = 0; i < bytecount; i++) {
      Tcl_ListObjAppendElement(interp, vector, Tcl_NewIntObj((int)values[i]));
   }
   Tcl_SetObjResult(interp, vector);
   free(values);
   return TCL_OK;
}

/*--------------------------------------------------------------*/
/* Tcl function "ftdi::spi_write":				*/
/*--------------------------------------------------------------*/

int
ftditcl_spi_write(ClientData clientData,
	Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   int result;
   int bytecount, i, j, value;
   int cmdcount, allcount;
   Tcl_WideInt regnum;
   unsigned char *values;
   unsigned char flags;
   Tcl_Obj *vector, *lobj;

   long numWritten;
   ftdi_record *ftRecord;
   struct ftdi_context * ftContext;
   int ftStatus;

   if (objc != 4) {
      Tcl_SetResult(interp, "spi_write: Need device name, "
		"command, and vector of values.\n", NULL);
      return TCL_ERROR;
   }
   ftRecord = find_record(Tcl_GetString(objv[1]), &ftContext);
   if (ftContext == (struct ftdi_context *)NULL) {
      Tcl_SetResult(interp, "spi_read:  No such device\n", NULL);
      return TCL_ERROR;
   }
   else flags = ftRecord->flags;

   if (flags & BITBANG_MODE) {
      result = ftditcl_bang_write(clientData, interp, objc, objv);
      return result;
   }

   result = Tcl_GetWideIntFromObj(interp, objv[2], &regnum);
   if (result != TCL_OK) return result;

   vector = objv[3];
   result = Tcl_ListObjLength(interp, vector, &bytecount);
   if (result != TCL_OK) return result;

   for (i = 0; i < bytecount; i++) {
      result = Tcl_ListObjIndex(interp, vector, i, &lobj);
      if (result != TCL_OK) return result;
      result = Tcl_GetIntFromObj(interp, lobj, &value);

      if (value < 0 || value > 255) {
         Tcl_SetResult(interp, "spi_write:  Byte value out of range 0-255\n",
		NULL);
	 return TCL_ERROR;
      }
   }

   cmdcount = ftRecord->cmdwidth >> 3;
   values = (unsigned char *)malloc((6 + cmdcount + bytecount) * sizeof(unsigned char));

   values[0] = 0x80;        // Set Dbus
   values[1] = (flags & CS_INVERT) ? 0x08 : 0x00; // Assert CS
   values[2] = 0x0b;        // SCK, SDI, and CS are outputs

   values[3] = 0x11;        // Simple write command
   // Number of bytes to write (less 1)
   allcount = bytecount + cmdcount - 1;
   values[4] = (unsigned char)(allcount & 0xff);
   values[5] = (unsigned char)((allcount >> 8) & 0xff);
   if (flags & LEGACY_MODE)
      // Command to send is "write register" + register no.
      values[6] = ((flags & MIXED_MODE) ? 0x10 : 0x40) + (unsigned char)regnum;
   else {
      for (i = 0; i < cmdcount; i++) {
	 j = cmdcount - i - 1;
	 values[6 + i] = (unsigned char)((regnum >> (j << 3)) & 0xff);
      }
   }

   if (verbose > 1) {
      Fprintf(interp, stderr, "spi_write: Writing: ");
      for (i = 0; i < 6 + cmdcount; i++) {
         Fprintf(interp, stderr, "0x%02x ", values[i]);
      }
      Fprintf(interp, stderr, "\n");
   }

   ftStatus = ftdi_write_data(ftContext, values, 6 + cmdcount);

   for (i = 0; i < bytecount; i++) {
      result = Tcl_ListObjIndex(interp, vector, i, &lobj);
      result = Tcl_GetIntFromObj(interp, lobj, &value);
      values[i + 6 + cmdcount] = (unsigned char)(value & 0xff);
   }

   // SPI write using MPSSE

   if (verbose > 1) {
      Fprintf(interp, stderr, "spi_write: Writing: ");
      for (i = 0; i < 6 + bytecount + cmdcount; i++) {
         Fprintf(interp, stderr, "0x%02x ", values[i]);
      }
      Fprintf(interp, stderr, "\n");
   }

   ftStatus = ftdi_write_data(ftContext, values, bytecount + cmdcount + 6);
   if (ftStatus < 0)
      Tcl_SetResult(interp, "Received error in SPI write.\n", NULL);
   else if (ftStatus != (bytecount + cmdcount + 6))
      Tcl_SetResult(interp, "SPI short write error.\n", NULL);

   values[0] = 0x80;        // Set Dbus
   values[1] = (flags & CS_INVERT) ? 0x00 : 0x08; // De-assert CS
   values[2] = 0x0b;        // SCK, SDI, and CS are outputs

   if (verbose > 1) {
      Fprintf(interp, stderr, "spi_write: Writing: ");
      for (i = 0; i < 3; i++) {
         Fprintf(interp, stderr, "0x%02x ", values[i]);
      }
      Fprintf(interp, stderr, "\n");
   }

   ftStatus = ftdi_write_data(ftContext, values, 3);
   if (ftStatus < 0)
      Tcl_SetResult(interp, "Received error in SPI write.\n", NULL);
   else if (ftStatus != 3)
      Tcl_SetResult(interp, "SPI short write error.\n", NULL);

   free(values);
   return TCL_OK;
}

/*--------------------------------------------------------------*/
/* Tcl function "ftdi::spi_readwrite":	Combined read and write	*/
/* (Note:  readwrite function untested, not sure how to do a	*/
/* readback of data without sending more clocks.  FT functions	*/
/* do not appear to be truly bidirectional/full duplex.)	*/
/*--------------------------------------------------------------*/

int
ftditcl_spi_readwrite(ClientData clientData,
	Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   int result;
   int bytecount, i, j;
   int cmdcount;
   Tcl_WideInt regnum;
   Tcl_Obj *lobj;
   int value;
   unsigned char *values;
   unsigned char tbuffer[13];
   unsigned char flags;
   Tcl_Obj *vector;

   long numWritten;
   long numRead;
   ftdi_record *ftRecord;
   struct ftdi_context * ftContext;
   int ftStatus;

   if (objc != 4) {
      Tcl_SetResult(interp, "spi_readwrite: Need device name, command, "
		"and byte list.\n", NULL);
      return TCL_ERROR;
   }
   ftRecord = find_record(Tcl_GetString(objv[1]), &ftContext);
   if (ftContext == (struct ftdi_context *)NULL) {
      Tcl_SetResult(interp, "spi_readwrite:  No such device\n", NULL);
      return TCL_ERROR;
   }
   else flags = ftRecord->flags;

   result = Tcl_GetWideIntFromObj(interp, objv[2], &regnum);
   if (result != TCL_OK) return result;

   vector = objv[3];
   result = Tcl_ListObjLength(interp, vector, &bytecount);
   if (result != TCL_OK) return result;

   for (i = 0; i < bytecount; i++) {
      result = Tcl_ListObjIndex(interp, vector, i, &lobj);
      if (result != TCL_OK) return result;
      result = Tcl_GetIntFromObj(interp, lobj, &value);

      if (value < 0 || value > 255) {
         Tcl_SetResult(interp, "spi_write:  Byte value out of range 0-255\n",
		NULL);
	 return TCL_ERROR;
      }
   }

   cmdcount = ftRecord->cmdwidth >> 3;
   values = (unsigned char *)malloc((6 + cmdcount + bytecount) * sizeof(unsigned char));

   // Write values to MPSSE to generate the SPI read command

   values[0] = 0x80;        // Set Dbus
   values[1] = (flags & CS_INVERT) ? 0x08 : 0x00; // Assert CS
   values[2] = 0x0b;        // SCK, SDI, and CS are outputs
   values[3] = 0x11;     // Simple write command
   // Command to send is "read register" + register no.
   cmdcount = ftRecord->cmdwidth >> 3;
   if (flags & LEGACY_MODE)
      values[4] = 0x00;     // Length = 1;
   else
      values[4] = (unsigned char)(cmdcount - 1);
   values[5] = 0x00;     // (High byte is zero)
   if (flags & LEGACY_MODE)
      values[6] = ((flags & MIXED_MODE) ? 0x20 : 0x80) + (unsigned char)regnum;
   else {
      for (i = 0; i < cmdcount; i++) {
	 j = cmdcount - i - 1;
	 values[6 + i] = (unsigned char)((regnum >> (j << 3)) & 0xff);
      }
   }

   if (verbose > 1) {
      Fprintf(interp, stderr, "spi_readwrite: Writing: ");
      for (i = 0; i < 6 + cmdcount; i++) {
         Fprintf(interp, stderr, "0x%02x ", values[i]);
      }
      Fprintf(interp, stderr, "\n");
   }

   ftStatus = ftdi_write_data(ftContext, values, 6 + cmdcount);
   if (ftStatus < 0)
      Tcl_SetResult(interp, "Received error while preparing SPI"
		" read command.\n", NULL);
   else if (ftStatus != 7)
      Tcl_SetResult(interp, "SPI readwrite:  short write error.\n", NULL);

   values[0] = (flags & MIXED_MODE) ? 0x24 : 0x20;     // Simple read command
   // Number bytes to read (less one)
   values[1] = (unsigned char)((bytecount - 1) && 0xff);
   values[2] = (unsigned char)(((bytecount - 1) >> 8) & 0xff);
 
   values[3] = 0x80;	// Set Dbus
   values[4] = (flags & CS_INVERT) ? 0x00 : 0x08; // De-assert CS
   values[5] = 0x0b;	// SCK, SDI, and CS are outputs

   if (verbose > 1) {
      Fprintf(interp, stderr, "spi_readwrite: Writing: ");
      for (i = 0; i < 6; i++) {
         Fprintf(interp, stderr, "0x%02x ", values[i]);
      }
      Fprintf(interp, stderr, "\n");
   }

   ftStatus = ftdi_write_data(ftContext, values, 6);
   if (ftStatus < 0)
      Tcl_SetResult(interp, "Received error while preparing SPI"
		" read command.\n", NULL);
   else if (ftStatus != 6)
      Tcl_SetResult(interp, "SPI readwrite:  short write error.\n", NULL);

   // SPI read using MPSSE

   ftStatus = ftdi_read_data(ftContext, values, bytecount);
   if (ftStatus < 0)
      Tcl_SetResult(interp, "Received error in SPI read.\n", NULL);
   else if (ftStatus != bytecount)
      Tcl_SetResult(interp, "SPI short read error.\n", NULL);

   vector = Tcl_NewListObj(0, NULL);
   for (i = 0; i < bytecount; i++) {
      Tcl_ListObjAppendElement(interp, vector, Tcl_NewIntObj((int)values[i]));
   }
   Tcl_SetObjResult(interp, vector);
   free(values);
   return TCL_OK;
}

/*--------------------------------------------------------------*/
/* Tcl function "ftdi_list":					*/
/*								*/
/* Create a list of all of the known interfaces (boards)	*/
/*--------------------------------------------------------------*/

int
ftditcl_list(ClientData clientData,
	Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   Tcl_Obj *lobj, *lobj2;
   Tcl_Obj *sobj;
   Tcl_HashSearch hs;
   Tcl_HashEntry *h;
   char *dname;

   struct ftdi_context * ftContext;
   ftdi_record *ftRecordPtr;
 
   lobj = Tcl_NewListObj(0, NULL);
   h = Tcl_FirstHashEntry(&handletab, &hs);
   while (h != NULL) {
      ftRecordPtr = (ftdi_record *)Tcl_GetHashValue(h);
      if (ftRecordPtr != (ftdi_record *)NULL) {
	 lobj2 = Tcl_NewListObj(0, NULL);
         dname = Tcl_GetHashKey(&handletab, h);
	 sobj = Tcl_NewStringObj(dname, -1);
	 Tcl_ListObjAppendElement(interp, lobj2, sobj);
	 sobj = Tcl_NewStringObj(ftRecordPtr->description, -1);
	 Tcl_ListObjAppendElement(interp, lobj2, sobj);
	 Tcl_ListObjAppendElement(interp, lobj, lobj2);
      }
      h = Tcl_NextHashEntry(&hs);
   }
   Tcl_SetObjResult(interp, lobj);
   return TCL_OK;
}

//--------------------------------------------------------------
// Function "ftdi_open"
//
// Open an ftdi-usb device
//
// Usage:  ftdi_open [-invert|-mixed_mode|-legacy|-serial] [<descriptor_string>]
//
// This routine will parse through the USB device entries for
// one matching either "<descriptor_string>", if supplied, or
// the default string "Dual RS232-HS B".  If there is a match,
// the device will be opened, and the routine will return a
// handle (name) "ftdi<X>", where <X> is an integer value, that
// can be passed to subsequent routines.
//
// If the option switch "-invert" is present, use a negative-
// sense chip select (i.e., CSB instead of CS).  If the option
// switch "-mixed_mode" is present, then assume a system in
// which SDI and SDO are valid on opposite edges of SCK.
//
// Option switch "-serial" keeps the FTDI in the default serial
// mode instead of switching to MPSSE mode, and is appropriate
// to communicate with any FTDI serial device (e.g., Prologix
// GPIB bus controller).
//
// In conjunction with the Open Circuit Design testbench
// project, the code defines a device name "TestBench" and
// a product ID code of 0x60fe.  This is programmed into an
// EEPROM adjacent to the FTDI chip and prevents the
// TestBench hardware from identifying itself with the
// default code for an FTDI chip, and that in turn prevents
// Linux systems from automatically loading the FTDI serial
// USB driver and configuring the device for serial
// communication.
//--------------------------------------------------------------

int
ftditcl_open(ClientData clientData,
	Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   Tcl_HashEntry *h;
   Tcl_Obj *tobj;
   static char devdflt0[] = "TestBench";
   static char devdflt1[] = "Dual RS232-HS";

   struct ftdi_context *ftContext, *ftLink;
   int ftStatus;
   ftdi_record *ftRecordPtr;
   struct ftdi_device_list *infonode = NULL, *snode;
   long numBytes;
   long numWritten, numRead;
   int devnum, new, argstart, result, channel;
   unsigned char flags = 0x0;
   char tclhandle[32], *devstr, *swstr, *chanstr;
   unsigned char tbuffer[12], rbuffer[12];
   char descr[100];
   bool dolist = false;

   // Check for "-invert", "-mixed_mode", "-legacy", or "-serial" switches
   // These must be at the beginning of the command.
   flags = 0;
   argstart = 1;
   while (objc > 1) {
      swstr = Tcl_GetString(objv[1]);
      if (!strncmp(swstr, "-inv", 4)) {
	 objc--;
	 argstart++;
	 flags |= CS_INVERT;
      }
      else if (!strncmp(swstr, "-mixed", 6)) {
	 objc--;
	 argstart++;
	 flags |= MIXED_MODE;
      }
      else if (!strncmp(swstr, "-legacy", 7)) {
	 objc--;
	 argstart++;
	 flags |= LEGACY_MODE;
      }
      else if (!strncmp(swstr, "-serial", 7)) {
	 objc--;
	 argstart++;
	 flags |= SERIAL_MODE;
      }
      else if (!strncmp(swstr, "-list", 5)) {
	 objc--;
	 argstart++;
	 dolist = true;
      }
      else
	 break;
   }

   // Create and initialize a new context
   ftContext = ftdi_new();

   // Assume device (devdflt0) unless otherwise specified
   if (objc < 2)
      devstr = devdflt0;
   else
      devstr = Tcl_GetString(objv[argstart]);

   channel = INTERFACE_ANY;
   if (objc < 3)
      chanstr = NULL;	/* Assume INTERFACE_ALL */
   else
      chanstr = Tcl_GetString(objv[argstart + 1]);

   if (objc == 2) {
      /* Check if 2nd argument was a single letter A/B/C/D, */
      /* in which case interpret 2nd argument as the device */
      /* channel and assume a default device name.	    */

      if (strlen(devstr) == 1)
	 if ((*devstr >= 'A') && (*devstr <= 'D')) {
	     chanstr = devstr;
	     devstr = devdflt0;
	 }
   }

   // If devstr ends in a space followed by "A" to "D", then set the
   // device channel here (must be done between ftdi_() and ftdi_open(),
   // and given an uninitialized context).

   if (chanstr == NULL)
      channel = INTERFACE_ANY;
   else if (!strcmp(chanstr, "any"))
      channel = INTERFACE_ANY;
   else if (!strcmp(chanstr, "A"))
      channel = INTERFACE_A;
   else if (!strcmp(chanstr, "B"))
      channel = INTERFACE_B;
   else if (!strcmp(chanstr, "C"))
      channel = INTERFACE_C;
   else if (!strcmp(chanstr, "D"))
      channel = INTERFACE_D;
   else {
      Tcl_SetResult(interp, "Unknown device channel.\n", NULL);
      return TCL_ERROR;
   }
   result = ftdi_set_interface(ftContext, channel);
   if (result != 0) {
      if (result == -1) {
	 Tcl_SetResult(interp, "Channel is not recognized for device.\n", NULL);
      }
      else if (result == -2) {
	 Tcl_SetResult(interp, "USB error while setting channel.\n", NULL);
      }
      else if (result == -3) {
	 Tcl_SetResult(interp, "Device is open; channel cannot be set.\n", NULL);
      }
      return TCL_ERROR;
   }


   // Generate a list of USB devices and check for match with the
   // description string.

   ftStatus = ftdi_usb_find_all(ftContext, &infonode, usb_vid, usb_pid);
   if (ftStatus < 0) {
      Tcl_SetResult(interp, "Unable to list devices.\n", NULL);
      return TCL_ERROR;
   }
   else if (infonode == NULL) {
      Tcl_SetResult(interp, "There are no FTDI devices present.\n", NULL);
      return TCL_ERROR;
   }

   for (snode = infonode; snode; snode = snode->next) {
      ftdi_usb_get_strings(ftContext, snode->dev, NULL, 0, descr, 100, NULL, 0);
      if (!strcmp(descr, devstr))
	 break;
   }

   if ((snode == NULL) && (devstr == devdflt0)) {
      // Try the other default (i.e., unprogrammed EPROM). . .
      devstr = devdflt1;

      for (snode = infonode; snode; snode = snode->next) {
         ftdi_usb_get_strings(ftContext, snode->dev, NULL, 0, descr, 100, NULL, 0);
         if (!strcmp(descr, devstr))
	    break;
      }
   }

   if (snode == NULL || dolist == true) {
      // Tcl_SetResult(interp, "No device matches description.\n", NULL);
      Tcl_Obj *lobj, *sobj;

      lobj = Tcl_NewListObj(0, NULL);
      for (snode = infonode; snode; snode = snode->next) {
         ftdi_usb_get_strings(ftContext, snode->dev, NULL, 0, descr, 100, NULL, 0);
	 sobj = Tcl_NewStringObj(descr, -1);
         Tcl_ListObjAppendElement(interp, lobj, sobj);
      }

      // If there is only one device, attempt to open it.  Otherwise,
      // return a list of the description strings so that the user can
      // try again with the one they're looking for.

      if (infonode->next == NULL && dolist == false) {
	 ftStatus = ftdi_usb_open_dev(ftContext, infonode->dev);
      }
      else {
         Tcl_SetObjResult(interp, lobj);
         ftdi_list_free(&infonode);
         return TCL_OK;
      }
   }
   else
      ftStatus = ftdi_usb_open_dev(ftContext, snode->dev);

   if (ftStatus < 0) {
      Tcl_SetResult(interp, "Unable to open device\n", NULL);
      ftdi_list_free(&infonode);
      return TCL_ERROR;
   }
   else {
      // Reset the FTDI device
      ftStatus = ftdi_usb_reset(ftContext);
      if (ftStatus < 0)
	 Tcl_SetResult(interp, "Received error while resetting device.\n", NULL);

      // Set device to MPSSE mode, with bits 0, 1, and 3 set to output
      // (SCK, SDI, and CS).  All others (SDO and Dbus) are set to type input.

      if (!(flags & SERIAL_MODE)) {
         ftStatus = ftdi_set_bitmode(ftContext, (unsigned char)0x0b,
		(unsigned char)BITMODE_MPSSE);
         if (ftStatus < 0)
	    Tcl_SetResult(interp, "Received error while setting bit mode.\n", NULL);
      }

      ftStatus = ftdi_usb_purge_tx_buffer(ftContext);
      if (ftStatus < 0)
	 Tcl_SetResult(interp, "Received error while purging transmit buffer.\n", NULL);

      ftStatus = ftdi_usb_purge_rx_buffer(ftContext);
      if (ftStatus < 0)
	 Tcl_SetResult(interp, "Received error while purging receive buffer.\n", NULL);

      // Set latency timer (in ms) (legacy case is 16; FT2232 minimum 1)
      ftStatus = ftdi_set_latency_timer(ftContext, (unsigned char)5);
      if (ftStatus < 0)
	 Tcl_SetResult(interp, "Received error while setting latency timer.\n", NULL);

      // Set timeouts (in ms)
      /*
      ftStatus = ftdi_set_timeouts(ftContext, 1000, 1000);
      if (ftStatus < 0)
	 Tcl_SetResult(interp, "Received error while setting timeouts.\n", NULL);
      */

      if (!(flags & SERIAL_MODE)) {
         // MPSSE setup. . .
         tbuffer[0] = 0x80;        // Set Dbus
         // De-assert CS (initial value 0 if CS, 1 if CSB)
         tbuffer[1] = (flags & CS_INVERT) ? 0x00 : 0x08;
         tbuffer[2] = 0x0b;        // SCK, SDI, and CS are outputs

         tbuffer[3] = 0x82;        // Set Cbus (rotary switch)
         tbuffer[4] = 0x00;        // Initial output values are 0
         tbuffer[5] = 0x00;        // All pins are input

         tbuffer[6] = 0x8a;        // Enable high-speed clock (x5, up to 30MHz)

         tbuffer[7] = 0x86;        // Set clock divider
         tbuffer[8] = 0x10;     	// Divide by 16
         tbuffer[9] = 0x00;        // (High byte is zero)

         if (verbose > 1) {
	    int i;
            Fprintf(interp, stderr, "ftdi_open: Writing: ");
            for (i = 0; i < 10; i++) {
               Fprintf(interp, stderr, "0x%02x ", tbuffer[i]);
            }
            Fprintf(interp, stderr, "\n");
         }

         ftStatus = ftdi_write_data(ftContext, tbuffer, 10);
         if (ftStatus < 0)
            Tcl_SetResult(interp, "Received error while writing init data\n", NULL);
         else if (ftStatus != 10)
            Tcl_SetResult(interp, "Short write error\n", NULL);
      }

      // Now assign a unique string handler to the device and associate it
      // with the ftContext in a hash table.

      devnum = ++ftdinum;
      sprintf(tclhandle, "ftdi%d", devnum);

      h = Tcl_CreateHashEntry(&handletab, (CONST char *)tclhandle, &new);
      if (new > 0) {
	 ftRecordPtr = (ftdi_record *)malloc(sizeof(ftdi_record));
	 ftRecordPtr->ftContext = ftContext;
	 ftRecordPtr->description = strdup(descr);
	 ftRecordPtr->flags = flags;
	 ftRecordPtr->cmdwidth = 8;
	 ftRecordPtr->wordwidth = 8;
	 Tcl_SetHashValue(h, ftRecordPtr);
	 result = TCL_OK;
      }
      else {
	 Tcl_SetResult(interp, "open:  Name already defined\n", NULL);
	 ftStatus = ftdi_usb_close(ftContext);
	 return TCL_ERROR;
      }
      tobj = Tcl_NewStringObj(tclhandle, -1);

      if (flags & SERIAL_MODE) return result;

      /* For everything beyond this point, the device is open	*/
      /* and the result code has been set.  All other errors	*/
      /* should be printed to stderr but not passed as a result	*/
      /* code.  It will be the responsibility of the end-user	*/
      /* to deal with an open device that is not acting normal.	*/

      /* Sanity check:  Give an error code (invalid opcode 0xff)  */
      /* and read the response "0xfa 0xff".			  */

      tbuffer[0] = 0xff;		// not a command

      if (verbose > 1) {
	 int i;
         Fprintf(interp, stderr, "ftdi_open: Writing: ");
         for (i = 0; i < 1; i++) {
            Fprintf(interp, stderr, "0x%02x ", tbuffer[i]);
         }
         Fprintf(interp, stderr, "\n");
      }

      ftStatus = ftdi_write_data(ftContext, tbuffer, 1);
      if (ftStatus < 0) {
         Fprintf(interp, stderr, "Received error while writing test data\n");
	 result = TCL_ERROR;
      }
      else if (ftStatus != 1) {
         Fprintf(interp, stderr, "Short write error on test data\n");
	 result = TCL_ERROR;
      }

      ftStatus = ftdi_read_data(ftContext, rbuffer, 2);
      if (ftStatus < 0 || ftStatus != 2) {
         Fprintf(interp, stderr, "Error message not received after invalid"
			" command.\n");
	 result = TCL_ERROR;
      }
      ftdi_list_free(&infonode);

      // Assert CS line
      tbuffer[0] = 0x80;
      tbuffer[1] = (ftRecordPtr->flags & CS_INVERT) ? 0x08 : 0x00;
      tbuffer[2] = 0x0b;

      if (verbose > 1) {
	 int i;
         Fprintf(interp, stderr, "ftdi_open: Writing: ");
         for (i = 0; i < 3; i++) {
            Fprintf(interp, stderr, "0x%02x ", tbuffer[i]);
         }
         Fprintf(interp, stderr, "\n");
      }

      ftStatus = ftdi_write_data(ftContext, tbuffer, 3);
      if (ftStatus < 0) {
         Fprintf(interp, stderr, "Received error while asserting CS.\n");
	 result = TCL_ERROR;
      }
      else if (ftStatus != 3) {
         Fprintf(interp, stderr, "Short write error while asserting CS\n");
	 result = TCL_ERROR;
      }
      Tcl_SetObjResult(interp, tobj);
   }
   return result;
}

//--------------------------------------------------------------
// Tcl function "ftdi_close"
// Close an ftdi device
//--------------------------------------------------------------

int
ftditcl_close(ClientData clientData,
	Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   Tcl_HashSearch hs;
   Tcl_HashEntry *h;
   char *devname;
   int result;

   struct ftdi_context * ftContext;
   ftdi_record *ftRecordPtr;
 
   if (objc == 1) {
      h = Tcl_FirstHashEntry(&handletab, &hs);
      while (h != NULL) {
	 ftRecordPtr = (ftdi_record *)Tcl_GetHashValue(h);
	 ftContext = ftRecordPtr->ftContext;

         devname = Tcl_GetHashKey(&handletab, h);
	 result = close_device(interp, devname);
	 if (result != TCL_OK) return result;
      }
      return TCL_OK;
   }
   else {
      int i;

      for (i = 1; i < objc; i++) {
	 devname = Tcl_GetString(objv[i]);
	 result = close_device(interp, devname);
	 if (result != TCL_OK) return result;
      }
   }
   return TCL_OK;
}

//--------------------------------------------------------------
// Graceful closure of a single device
//--------------------------------------------------------------

int close_device(Tcl_Interp *interp, char *devname)
{
   struct ftdi_context * ftContext;
   int ftStatus;
   ftdi_record *ftRecordPtr;
   long numWritten;
   Tcl_HashSearch hs;
   Tcl_HashEntry *h;
   unsigned char flags;
   unsigned char tbuffer[12];

   ftContext = find_handle(devname, &flags);
   if (ftContext == (struct ftdi_context *)NULL) return TCL_ERROR;

   tbuffer[0] = 0x80;        // Set Dbus
   tbuffer[1] = (flags & CS_INVERT) ? 0x00 : 0x08;
   tbuffer[2] = 0x00;
   tbuffer[3] = 0x82;        // Set Cbus
   tbuffer[4] = 0x00;
   tbuffer[5] = 0x00;

   if (verbose > 1) {
      int i;
      Fprintf(interp, stderr, "ftdi_close: Writing: ");
      for (i = 0; i < 6; i++) {
         Fprintf(interp, stderr, "0x%02x ", tbuffer[i]);
      }
      Fprintf(interp, stderr, "\n");
   }

   ftStatus = ftdi_write_data(ftContext, tbuffer, 6);
   if (ftStatus < 0)
      Tcl_SetResult(interp, "Received error while preparing device for close.", NULL);
   else if (ftStatus != 6)
      Tcl_SetResult(interp, "Short write to device.", NULL);

   ftStatus = ftdi_usb_purge_tx_buffer(ftContext);
   if (ftStatus < 0)
      Tcl_SetResult(interp, "Received error while purging transmit buffer.", NULL);

   ftStatus = ftdi_usb_purge_rx_buffer(ftContext);
   if (ftStatus < 0)
      Tcl_SetResult(interp, "Received error while purging receive buffer.", NULL);

   ftStatus = ftdi_usb_close(ftContext);
   if (ftStatus < 0) {
      Tcl_SetResult(interp, "Received error while closing device.", NULL);
      return TCL_ERROR;
   }
   h = Tcl_FindHashEntry(&handletab, devname);
   if (h != (Tcl_HashEntry *)NULL) {
      ftRecordPtr = (ftdi_record *)Tcl_GetHashValue(h);
      free(ftRecordPtr->description);
      free(ftRecordPtr);
      Tcl_DeleteHashEntry(h);
   }
   return TCL_OK;
}

/*--------------------------------------------------------------*/
/* Tcl commands							*/
/*--------------------------------------------------------------*/

typedef struct {
   const char	*cmdstr;
   void		(*func)();
} cmdstruct;

/*--------------------------------------------------------------*/

static cmdstruct ftdi_commands[] =
{
   {"ftdi::get", (void *)ftditcl_get},
   {"ftdi::verbose", (void *)ftditcl_verbose},
   {"ftdi::spi_read", (void *)ftditcl_spi_read},
   {"ftdi::spi_write", (void *)ftditcl_spi_write},
   {"ftdi::spi_readwrite", (void *)ftditcl_spi_readwrite},
   {"ftdi::spi_speed", (void *)ftditcl_spi_speed},
   {"ftdi::spi_command", (void *)ftditcl_spi_command},
   {"ftdi::spi_csb_mode", (void *)ftditcl_spi_csb_mode},
   {"ftdi::spi_bitbang", (void *)ftditcl_spi_bitbang},
   {"ftdi::bitbang_read", (void *)ftditcl_bang_read},
   {"ftdi::bitbang_write", (void *)ftditcl_bang_write},
   {"ftdi::bitbang_word", (void *)ftditcl_bang_word},
   {"ftdi::bitbang_set", (void *)ftditcl_bang_set},
   {"ftdi::disable", (void *)ftditcl_disable},
   {"ftdi::listdev", (void *)ftditcl_list},
   {"ftdi::opendev", (void *)ftditcl_open},
   {"ftdi::setid", (void *)ftditcl_setid},
   {"ftdi::closedev", (void *)ftditcl_close},
   {"", NULL} /* sentinel */
};

/*--------------------------------------------------------------*/
/* Stdout/Stderr redirect to Tk console				*/
/*--------------------------------------------------------------*/

void tcl_vprintf(Tcl_Interp *interp, FILE *f, const char *fmt, va_list args_in)
{
    va_list args;
    static char outstr[128] = "puts -nonewline std";
    char *outptr, *bigstr = NULL, *finalstr = NULL;
    int i, nchars, result, escapes = 0, limit;

    strcpy (outstr + 19, (f == stderr) ? "err \"" : "out \"");
    outptr = outstr;

    va_copy(args, args_in);
    nchars = vsnprintf(outptr + 24, 102, fmt, args);
    va_end(args);

    if (nchars >= 102)
    {
        va_copy(args, args_in);
        bigstr = Tcl_Alloc(nchars + 26);
        strncpy(bigstr, outptr, 24);
        outptr = bigstr;
        vsnprintf(outptr + 24, nchars + 2, fmt, args);
        va_end(args);
    }
    else if (nchars == -1) nchars = 126;

    for (i = 24; *(outptr + i) != '\0'; i++) {
        if (*(outptr + i) == '\"' || *(outptr + i) == '[' ||
                *(outptr + i) == ']' || *(outptr + i) == '\\')
            escapes++;
    }

    if (escapes > 0)
    {
        finalstr = Tcl_Alloc(nchars + escapes + 26);
        strncpy(finalstr, outptr, 24);
        escapes = 0;
        for (i = 24; *(outptr + i) != '\0'; i++)
        {
            if (*(outptr + i) == '\"' || *(outptr + i) == '[' ||
                        *(outptr + i) == ']' || *(outptr + i) == '\\')
            {
                *(finalstr + i + escapes) = '\\';
                escapes++;
            }
            *(finalstr + i + escapes) = *(outptr + i);
        }
        outptr = finalstr;
    }

    *(outptr + 24 + nchars + escapes) = '\"';
    *(outptr + 25 + nchars + escapes) = '\0';

    result = Tcl_Eval(interp, outptr);

    if (bigstr != NULL) Tcl_Free(bigstr);
    if (finalstr != NULL) Tcl_Free(finalstr);
}

/*------------------------------------------------------*/
/* Redefine the printf routine as Fprintf		*/
/*------------------------------------------------------*/

void Fprintf(Tcl_Interp *interp, FILE *f, char *format, ...)
{
  va_list ap;

  va_start(ap, format);
  tcl_vprintf(interp, f, format, ap);
  va_end(ap);
}

/*--------------------------------------------------------------*/
/* Tcl package initialization function				*/
/*--------------------------------------------------------------*/

int
Tclftdi_Init(Tcl_Interp *interp)
{
   char command[256];
   int cmdidx, i, j;

   if (interp == NULL) return TCL_ERROR;

   /* Remember the interpreter (global variable) */
   ftdiinterp = interp;

   if (Tcl_InitStubs(interp, "8.4", 0) == NULL) return TCL_ERROR;

   Tcl_Eval(interp, "namespace eval ftdi namespace export *");
   Tcl_PkgProvide(interp, "Tclftdi", "1.0");

   for (cmdidx = 0; ftdi_commands[cmdidx].func != NULL; cmdidx++) {
      sprintf(command, "%s", ftdi_commands[cmdidx].cmdstr);
      Tcl_CreateObjCommand(interp, command,
		(Tcl_ObjCmdProc *)ftdi_commands[cmdidx].func,
		(ClientData)NULL, (Tcl_CmdDeleteProc *)NULL);
   }
   gpib_command_init(interp);
   Tcl_InitHashTable(&handletab, TCL_STRING_KEYS);
   gpib_global_init(interp);
   return TCL_OK;
}

#endif	/* HAVE_LIBFTDI */
