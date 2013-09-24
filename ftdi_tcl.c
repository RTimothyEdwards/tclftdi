#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <dlfcn.h>

#include <ftd2xx.h>
#include <tcl.h>

/* Forward declarations */

/*--------------------------------------------------------------*/
/* Structure to manage device handles				*/
/* Each device record contains the device handle and the	*/
/* device description.  To do:  Add the dbus read-back value	*/
/* and use this as an alternative device identifier.		*/
/*--------------------------------------------------------------*/

typedef struct _FT_RECORD {
   FT_HANDLE ftHandle;
   char *description;
   unsigned char flags;
} FT_RECORD;

/* Flag definitions */
#define CS_INVERT   0x01
#define MIXED_MODE  0x03	// Mixed mode has SDI and SDO on
				// different SCK edges.

Tcl_HashTable handletab;

/*--------------------------------------------------------------*/
/* Miscellaneous stuff						*/
/*--------------------------------------------------------------*/

typedef unsigned char bool;

#define false 0
#define true 1

static int verbose=1;
static int ftdinum=-1;

/*--------------------------------------------------------------*/
/* Support function "find_handle"				*/
/*								*/
/* Return the handle associated with the device string in the	*/
/* hash table.							*/
/*								*/
/* Return the value of the device flags in "flagptr" (u_char).	*/
/* Might be preferable to simply have a routine that returns	*/
/* just the FT_RECORD pointer.					*/
/*--------------------------------------------------------------*/

FT_HANDLE
find_handle(char *devstr, unsigned char *flagptr)
{
   Tcl_HashEntry *h;
   FT_HANDLE ftHandle;
   FT_RECORD *ftRecordPtr;

   h = Tcl_FindHashEntry(&handletab, devstr);
   if (h != NULL) {
      ftRecordPtr = (FT_RECORD *)Tcl_GetHashValue(h);
      if (flagptr != NULL) *flagptr = ftRecordPtr->flags;
      return ftRecordPtr->ftHandle;
   }
   if (flagptr != NULL) *flagptr = 0x0;
   return (FT_HANDLE)NULL;
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

   DWORD numWritten;
   DWORD numRead;
   FT_HANDLE ftHandle;
   FT_STATUS ftStatus;
 
   if (objc <= 1) {
     Tcl_SetResult(interp, "get: Need device name\n", NULL);
     return TCL_ERROR;
   }

   ftHandle = find_handle(Tcl_GetString(objv[1]), &flags);
   if (ftHandle == (FT_HANDLE)NULL) {
      Tcl_SetResult(interp, "get:  No such device\n", NULL);
      return TCL_ERROR;
   }

   tbuffer[0] = 0x83;		// Read high byte (i.e., Cbus)

   ftStatus = FT_Write(ftHandle, tbuffer, (DWORD)1, &numWritten);
   if (ftStatus != FT_OK)
      Tcl_SetResult(interp, "Received error while reading Dbus\n", NULL);
   else if (numWritten != (DWORD)1)
      Tcl_SetResult(interp, "get:  short write error.\n", NULL);

   ftStatus = FT_Read(ftHandle, rbuffer, (DWORD)1, &numRead);
   if (ftStatus != FT_OK)
      Tcl_SetResult(interp, "Received error while reading Dbus\n", NULL);
   else if (numRead != (DWORD)1)
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
/* Tcl function "ftdi::spi_speed":  Set the SPI clock speed of	*/
/* the FTDI MPSSE SPI protocol.					*/
/*--------------------------------------------------------------*/

int
ftditcl_spi_speed(ClientData clientData,
	Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   FT_HANDLE ftHandle;
   FT_STATUS ftStatus;

   DWORD numWritten;

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
   ftHandle = find_handle(Tcl_GetString(objv[1]), &flags);
   if (ftHandle == (FT_HANDLE)NULL) {
      Tcl_SetResult(interp, "spi_speed:  No such device\n", NULL);
      return TCL_ERROR;
   }

   result = Tcl_GetDoubleFromObj(interp, objv[2], &mhz);
   if (result != TCL_OK) return result;

   /* Convert double into two bytes in tbuffer */

   ival = (int)((30.0 / mhz) - 1.0);

   tbuffer[0] = 0x8a;        // Enable high-speed clock (x5, up to 30MHz)
   tbuffer[1] = 0x86;        // Set clock divider
   tbuffer[2] = ival & 0xff;
   tbuffer[3] = (ival >> 8) & 0xff;

   ftStatus = FT_Write(ftHandle, tbuffer, (DWORD)4, &numWritten);
   if (ftStatus != FT_OK)
      Tcl_SetResult(interp, "Received error while setting SPI"
		" clock speed.\n", NULL);
   else if (numWritten != (DWORD)4)
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
   int regnum, bytecount, i;
   unsigned char *values;
   unsigned char tbuffer[10];
   unsigned char flags;
   Tcl_Obj *vector;

   DWORD numWritten;
   DWORD numRead;
   FT_HANDLE ftHandle;
   FT_STATUS ftStatus;

   if (objc != 4) {
      Tcl_SetResult(interp, "spi_read: Need device name, register, "
		"and byte count.\n", NULL);
      return TCL_ERROR;
   }
   ftHandle = find_handle(Tcl_GetString(objv[1]), &flags);
   if (ftHandle == (FT_HANDLE)NULL) {
      Tcl_SetResult(interp, "spi_read:  No such device\n", NULL);
      return TCL_ERROR;
   }

   result = Tcl_GetIntFromObj(interp, objv[2], &regnum);
   if (result != TCL_OK) return result;
   result = Tcl_GetIntFromObj(interp, objv[3], &bytecount);
   if (result != TCL_OK) return result;
   values = (unsigned char *)malloc(bytecount * sizeof(unsigned char));

   // Write values to MPSSE to generate the SPI read command

   tbuffer[0] = 0x80;        // Set Dbus
   tbuffer[1] = (flags & CS_INVERT) ? 0x00 : 0x08; // Assert CS
   tbuffer[2] = 0x0b;        // SCK, SDI, and CS are outputs

   tbuffer[3] = 0x11;     // Simple write command
   tbuffer[4] = 0x00;     // Length = 1;
   tbuffer[5] = 0x00;     // (High byte is zero)
   // Command to send is "read register" + register no.
   tbuffer[6] = ((flags & MIXED_MODE) ? 0x20 : 0x80) + (unsigned char)regnum;

   ftStatus = FT_Write(ftHandle, tbuffer, (DWORD)7, &numWritten);
   if (ftStatus != FT_OK)
      Tcl_SetResult(interp, "Received error while preparing SPI"
		" read command.\n", NULL);
   else if (numWritten != (DWORD)7)
      Tcl_SetResult(interp, "SPI read:  short write error.\n", NULL);

   /* This hack applies only to the DPLL demo board---SPI registers	*/
   /* require time to access!  Write the first 10 bytes, pause, then	*/
   /* do the rest.							*/

   if (regnum < 16) {
      usleep(10);		// 10us delay for SPI transmission
   }

   tbuffer[0] = (flags & MIXED_MODE) ? 0x24 : 0x20;     // Simple read command
   // Number bytes to read (less one)
   tbuffer[1] = (unsigned char)(bytecount - 1);
   tbuffer[2] = 0x00;     // (High byte is zero)
 
   tbuffer[3] = 0x80;	// Set Dbus
   tbuffer[4] = (flags & CS_INVERT) ? 0x08 : 0x00; // De-assert CS
   tbuffer[5] = 0x0b;	// SCK, SDI, and CS are outputs

   ftStatus = FT_Write(ftHandle, tbuffer, (DWORD)6, &numWritten);
   if (ftStatus != FT_OK)
      Tcl_SetResult(interp, "Received error while preparing SPI"
		" read command.\n", NULL);
   else if (numWritten != (DWORD)6)
      Tcl_SetResult(interp, "SPI read:  short write error.\n", NULL);

   // SPI read using MPSSE

   ftStatus = FT_Read(ftHandle, values, (DWORD)bytecount, &numRead);
   if (ftStatus != FT_OK)
      Tcl_SetResult(interp, "Received error in SPI read.\n", NULL);
   else if (numRead != (DWORD)bytecount)
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
   int regnum, bytecount, i, value;
   unsigned char *values;
   unsigned char flags;
   Tcl_Obj *vector, *lobj;

   DWORD numWritten;
   FT_HANDLE ftHandle;
   FT_STATUS ftStatus;

   if (objc != 4) {
      Tcl_SetResult(interp, "spi_write: Need device name, "
		"register, and vector of values.\n", NULL);
      return TCL_ERROR;
   }
   ftHandle = find_handle(Tcl_GetString(objv[1]), &flags);
   if (ftHandle == (FT_HANDLE)NULL) {
      Tcl_SetResult(interp, "spi_read:  No such device\n", NULL);
      return TCL_ERROR;
   }

   result = Tcl_GetIntFromObj(interp, objv[2], &regnum);
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

   values = (unsigned char *)malloc((7 + bytecount) * sizeof(unsigned char));

   values[0] = 0x80;        // Set Dbus
   values[1] = (flags & CS_INVERT) ? 0x00 : 0x08; // Assert CS
   values[2] = 0x0b;        // SCK, SDI, and CS are outputs

   values[3] = 0x11;        // Simple write command
   // Number of bytes to write (less 1)
   values[4] = (unsigned char)bytecount;
   values[5] = 0x00;        // (High byte is zero)
   // Command to send is "write register" + register no.
   values[6] = ((flags & MIXED_MODE) ? 0x10 : 0x40) + (unsigned char)regnum;

   for (i = 0; i < bytecount; i++) {
      result = Tcl_ListObjIndex(interp, vector, i, &lobj);
      result = Tcl_GetIntFromObj(interp, lobj, &value);
      values[i + 7] = (unsigned char)(value & 0xff);
   }

   // SPI write using MPSSE

   ftStatus = FT_Write(ftHandle, values, (DWORD)(bytecount + 7), &numWritten);
   if (ftStatus != FT_OK)
      Tcl_SetResult(interp, "Received error in SPI write.\n", NULL);
   else if (numWritten != (DWORD)(bytecount + 7))
      Tcl_SetResult(interp, "SPI short write error.\n", NULL);

   values[0] = 0x80;        // Set Dbus
   values[1] = (flags & CS_INVERT) ? 0x08 : 0x00; // De-assert CS
   values[2] = 0x0b;        // SCK, SDI, and CS are outputs

   ftStatus = FT_Write(ftHandle, values, (DWORD)3, &numWritten);
   if (ftStatus != FT_OK)
      Tcl_SetResult(interp, "Received error in SPI write.\n", NULL);
   else if (numWritten != (DWORD)3)
      Tcl_SetResult(interp, "SPI short write error.\n", NULL);

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

   FT_HANDLE ftHandle;
   FT_RECORD *ftRecordPtr;
 
   lobj = Tcl_NewListObj(0, NULL);
   h = Tcl_FirstHashEntry(&handletab, &hs);
   while (h != NULL) {
      ftRecordPtr = (FT_RECORD *)Tcl_GetHashValue(h);
      if (ftRecordPtr != (FT_RECORD *)NULL) {
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
// Usage:  ftdi_open [-invert|-mixed_mode] [<descriptor_string>]
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
   static char devdflt0[] = "TestBench B";
   static char devdflt1[] = "TestBench A";
   static char devdflt2[] = "Dual RS232-HS B";
   static char devdflt3[] = "Dual RS232-HS A";

   FT_HANDLE ftHandle, *ftLink;
   FT_STATUS ftStatus;
   FT_RECORD *ftRecordPtr;
   FT_DEVICE_LIST_INFO_NODE *infonode;
   DWORD numDevs, numBytes;
   DWORD numWritten, numRead;
   int devnum, new, devidx, argstart;
   unsigned char flags = 0x0;
   char tclhandle[32], *devstr, *swstr;
   unsigned char tbuffer[12], rbuffer[12];

   // Check for "-invert" or "-mixed_mode" switches
   argstart = 1;
   if (objc > 1) {
      swstr = Tcl_GetString(objv[1]);
      if (!strncmp(swstr, "-inv", 4)) {
	 objc--;
	 argstart = 2;
	 flags = CS_INVERT;
      }
      else if (!strncmp(swstr, "-mixed", 4)) {
	 objc--;
	 argstart = 2;
	 flags = MIXED_MODE;
      }
   }

   // Assume device channel A (devdflt0) unless otherwise specified
   if (objc < 2)
      devstr = devdflt0;
   else
      devstr = Tcl_GetString(objv[argstart]);

   // Allow the driver to handle our unique product code
   ftStatus = FT_SetVIDPID((DWORD)0x0403, (DWORD)0x60ff);
   if (ftStatus != FT_OK) {
      fprintf(stderr, "Unable to set extended device ID (error %d)\n", (int)ftStatus);
      exit(1);
   }

   // Generate a list of USB devices and check for match with the
   // description string.

   ftStatus = FT_CreateDeviceInfoList(&numDevs);
   if (ftStatus != FT_OK) {
      Tcl_SetResult(interp, "Unable to list devices.\n", NULL);
      return TCL_ERROR;
   }
   else if (numDevs == 0) {
      Tcl_SetResult(interp, "There are no FTDI devices present.\n", NULL);
      return TCL_ERROR;
   }

   infonode = (FT_DEVICE_LIST_INFO_NODE *)malloc(numDevs *
		sizeof(FT_DEVICE_LIST_INFO_NODE));
   ftStatus = FT_GetDeviceInfoList(infonode, &numDevs);

   for (devidx = 0; devidx < numDevs; devidx++) {
      if (!strcmp(infonode[devidx].Description, devstr))
	 break;
   }
   if ((devidx == (int)numDevs) && ((devstr == devdflt0) || (devstr == devdflt1))) {
      // Try the other default (i.e., unprogrammed EPROM). . .
      devstr = devdflt2;

      for (devidx = 0; devidx < numDevs; devidx++) {
         if (!strcmp(infonode[devidx].Description, devstr))
	    break;
      }
   }

   if (devidx == (int)numDevs) {
      // Tcl_SetResult(interp, "No device matches description.\n", NULL);
      Tcl_Obj *lobj, *sobj;

      lobj = Tcl_NewListObj(0, NULL);
      for (devidx = 0; devidx < numDevs; devidx++) {
	 sobj = Tcl_NewStringObj(infonode[devidx].Description, -1);
         Tcl_ListObjAppendElement(interp, lobj, sobj);
      }
      Tcl_SetObjResult(interp, lobj);
      free(infonode);
      return TCL_ERROR;
   }

   ftStatus = FT_OpenEx(infonode[devidx].Description, FT_OPEN_BY_DESCRIPTION,
		&ftHandle);

   if (ftStatus != FT_OK) {
      Tcl_SetResult(interp, "Unable to open device (need to rmmod ftdi_sio?)\n",
			NULL);
      free(infonode);
      return TCL_ERROR;
   }
   else {
      // Reset the FTDI device
      ftStatus = FT_ResetDevice(ftHandle);
      if (ftStatus != FT_OK)
	 Tcl_SetResult(interp, "Received error while resetting device.\n", NULL);

      // Set device to MPSSE mode, with bits 0, 1, and 3 set to output
      // (SCK, SDI, and CS).  All others (SDO and Dbus) are set to type input.

      ftStatus = FT_SetBitMode(ftHandle, (UCHAR)0x0b, (UCHAR)0x02);
      if (ftStatus != FT_OK)
	 Tcl_SetResult(interp, "Received error while setting bit mode.\n", NULL);

      ftStatus = FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX);
      if (ftStatus != FT_OK)
	 Tcl_SetResult(interp, "Received error while purging device.\n", NULL);

      // Set latency timer (in ms) (legacy case is 16; FT2232 minimum 1)
      ftStatus = FT_SetLatencyTimer(ftHandle, 5);
      if (ftStatus != FT_OK)
	 Tcl_SetResult(interp, "Received error while setting latency timer.\n", NULL);

      // Set timeouts (in ms)
      ftStatus = FT_SetTimeouts(ftHandle, 1000, 1000);
      if (ftStatus != FT_OK)
	 Tcl_SetResult(interp, "Received error while setting timeouts.\n", NULL);

      // MPSSE setup. . .
      tbuffer[0] = 0x80;        // Set Dbus
      // De-assert CS (initial value 0 if CS, 1 if CSB)
      tbuffer[1] = (flags & CS_INVERT) ? 0x08 : 0x00;
      tbuffer[2] = 0x0b;        // SCK, SDI, and CS are outputs

      tbuffer[3] = 0x82;        // Set Cbus (rotary switch)
      tbuffer[4] = 0x00;        // Initial output values are 0
      tbuffer[5] = 0x00;        // All pins are input

      tbuffer[6] = 0x8a;        // Enable high-speed clock (x5, up to 30MHz)

      tbuffer[7] = 0x86;        // Set clock divider
      tbuffer[8] = 0x10;     	// Divide by 16
      tbuffer[9] = 0x00;        // (High byte is zero)

      ftStatus = FT_Write(ftHandle, tbuffer, (DWORD)10, &numWritten);
      if (ftStatus != FT_OK)
         Tcl_SetResult(interp, "Received error while writing init data\n", NULL);
      else if (numWritten != (DWORD)10)
         Tcl_SetResult(interp, "Short write error\n", NULL);

      // Now assign a unique string handler to the device and associate it
      // with the ftHandle in a hash table.

      devnum = ++ftdinum;
      sprintf(tclhandle, "ftdi%d", devnum);
      h = Tcl_CreateHashEntry(&handletab, (CONST char *)tclhandle, &new);
      if (new > 0) {
	 ftRecordPtr = (FT_RECORD *)malloc(sizeof(FT_RECORD));
	 ftRecordPtr->ftHandle = ftHandle;
	 ftRecordPtr->description = strdup(infonode[devidx].Description);
	 ftRecordPtr->flags = flags;
	 Tcl_SetHashValue(h, ftRecordPtr);
      }
      else {
	 Tcl_SetResult(interp, "open:  Name already defined\n", NULL);
	 ftStatus = FT_Close(ftHandle);
	 return TCL_ERROR;
      }
      tobj = Tcl_NewStringObj(tclhandle, -1);
      Tcl_SetObjResult(interp, tobj);
      // lprintf(stderr, "Opening device %s\n", tclhandle);

      /* Sanity check:  Give an error code (invalid opcode 0xff)  */
      /* and read the response "0xfa 0xff".			  */

      tbuffer[0] = 0xff;		// not a command

      ftStatus = FT_Write(ftHandle, tbuffer, (DWORD)1, &numWritten);
      if (ftStatus != FT_OK)
         Tcl_SetResult(interp, "Received error while writing init data\n", NULL);
      else if (numWritten != (DWORD)1)
         Tcl_SetResult(interp, "Short write error\n", NULL);

      ftStatus = FT_Read(ftHandle, rbuffer, (DWORD)2, &numRead);
      if (ftStatus != FT_OK || numRead != 2) {
         Tcl_SetResult(interp, "Error message not received after invalid"
			" command.\n", NULL);
	 // good luck with that. . .
      }
      free(infonode);

      // Assert CS line
      tbuffer[0] = 0x80;
      tbuffer[1] = (ftRecordPtr->flags & CS_INVERT) ? 0x00 : 0x08;
      tbuffer[2] = 0x0b;

      ftStatus = FT_Write(ftHandle, tbuffer, (DWORD)3, &numWritten);
      if (ftStatus != FT_OK)
         Tcl_SetResult(interp, "Received error while asserting CS.", NULL);
      else if (numWritten != (DWORD)3)
         Tcl_SetResult(interp, "Short write error\n", NULL);
   }
   return TCL_OK;
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
   unsigned char tbuffer[12];
   unsigned char flags;

   DWORD numWritten;
   FT_HANDLE ftHandle;
   FT_STATUS ftStatus;
   FT_RECORD *ftRecordPtr;
 
   if (objc == 1) {
      h = Tcl_FirstHashEntry(&handletab, &hs);
      while (h != NULL) {
	 ftRecordPtr = (FT_RECORD *)Tcl_GetHashValue(h);
	 ftHandle = ftRecordPtr->ftHandle;

	 /* To-do: provide same level of graceful closure as below */

	 ftStatus = FT_Close(ftHandle);
	 free(ftRecordPtr->description);
	 free(ftRecordPtr);
	 h = Tcl_NextHashEntry(&hs);
      }
      Tcl_DeleteHashTable(&handletab);
      return TCL_OK;
   }
   ftHandle = find_handle(Tcl_GetString(objv[1]), &flags);
   if (ftHandle == (FT_HANDLE)NULL) return TCL_ERROR;

   tbuffer[0] = 0x80;        // Set Dbus
   tbuffer[1] = (flags & CS_INVERT) ? 0x08 : 0x00;
   tbuffer[2] = 0x00;
   tbuffer[3] = 0x82;        // Set Cbus
   tbuffer[4] = 0x00;
   tbuffer[5] = 0x00;

   ftStatus = FT_Write(ftHandle, tbuffer, (DWORD)6, &numWritten);
   if (ftStatus != FT_OK)
      Tcl_SetResult(interp, "Received error while preparing device for close.", NULL);
   else if (numWritten != (DWORD)6)
      Tcl_SetResult(interp, "Short write to device.", NULL);

   ftStatus = FT_Purge(ftHandle, FT_PURGE_RX | FT_PURGE_TX);
   if (ftStatus != FT_OK)
      Tcl_SetResult(interp, "Received error while purging device.", NULL);

   ftStatus = FT_Close(ftHandle);
   if (ftStatus != FT_OK) {
      Tcl_SetResult(interp, "Received error while closing device.", NULL);
      return TCL_ERROR;
   }
   h = Tcl_FindHashEntry(&handletab, Tcl_GetString(objv[1]));
   if (h != (Tcl_HashEntry *)NULL) {
      ftRecordPtr = (FT_RECORD *)Tcl_GetHashValue(h);
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
   {"ftdi::spi_speed", (void *)ftditcl_spi_speed},
   {"ftdi::listdev", (void *)ftditcl_list},
   {"ftdi::opendev", (void *)ftditcl_open},
   {"ftdi::closedev", (void *)ftditcl_close},
   {"", NULL} /* sentinel */
};

/*--------------------------------------------------------------*/
/* Tcl package initialization function				*/
/*--------------------------------------------------------------*/

int
Tclftdi_Init(Tcl_Interp *interp)
{
   char command[256];
   int cmdidx, i, j;

   if (interp == NULL) return TCL_ERROR;

   if (Tcl_InitStubs(interp, "8.4", 0) == NULL) return TCL_ERROR;

   for (cmdidx = 0; ftdi_commands[cmdidx].func != NULL; cmdidx++) {
      sprintf(command, "%s", ftdi_commands[cmdidx].cmdstr);
      Tcl_CreateObjCommand(interp, command,
		(Tcl_ObjCmdProc *)ftdi_commands[cmdidx].func,
		(ClientData)NULL, (Tcl_CmdDeleteProc *)NULL);
   }
   Tcl_Eval(interp, "namespace eval ftdi namespace export *");
   Tcl_PkgProvide(interp, "Tclftdi", "1.0");
   Tcl_InitHashTable(&handletab, TCL_STRING_KEYS);
   return TCL_OK;
}
