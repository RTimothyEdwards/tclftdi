/*
 *------------------------------------------------------------
 * gpib_driver.c
 *------------------------------------------------------------
 * Standalone driver for the Prologix GPIB/USB digital I/O interface
 *------------------------------------------------------------
 */

#include <stdio.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <sys/dir.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>
#include <malloc.h>
#include <string.h>

#include <tcl.h>

#include "gpib_driver.h"

Tcl_HashTable linkHash;
Tcl_HashTable nameHash;

/*--------------------------------------------------------------*/
/* Prologix GPIB routines					*/
/*--------------------------------------------------------------*/

void
gpib_map_address(char *devname, int addr, int flags)
{
   int new;
   Tcl_HashEntry *he;
   gpib_record *link, *dlink;

   he = Tcl_CreateHashEntry(&nameHash, (CONST char *)devname, &new);
   if (new > 0) {
      link = (gpib_record *)malloc(sizeof(gpib_record));
      Tcl_SetHashValue(he, link);
   }
   else {
      link = Tcl_GetHashValue(he);
      if (link->device_id != addr || link->flags != flags) {
	 /* Update characteristics on open device (if any) */
	 he = Tcl_FindHashEntry(&linkHash, devname);
	 if (he != NULL) {
	    dlink = Tcl_GetHashValue(he);
	    dlink->device_id = addr;
	    dlink->flags = flags;
	 }
      }
   }

   link->controller_id = -1;
   link->device_id = addr;
   link->flags = flags;
}

/*--------------------------------------------------------------*/
/*								*/
/*--------------------------------------------------------------*/

int
gpib_get_address(char *devname)
{
   int new, addr;
   Tcl_HashEntry *he;
   gpib_record *link;

   he = Tcl_FindHashEntry(&nameHash, (CONST char *)devname);
   if (he != NULL) {
      link = Tcl_GetHashValue(he);
      if (link != NULL) {
         addr = link->device_id;
         return addr;
      }
      else return -1;
   }
   else return -1;
}

/*--------------------------------------------------------------*/
/*								*/
/*--------------------------------------------------------------*/

void
close_unused_controllers(Tcl_Interp *interp)
{
   int i;
   int cused[MAX_CONTROLLERS];
   Tcl_HashEntry *he;
   Tcl_HashSearch hs;
   gpib_record *link;

   for (i = 0; i < MAX_CONTROLLERS; i++) cused[i] = 0;

   he = Tcl_FirstHashEntry(&linkHash, &hs);
   while (he != NULL) {
      link = Tcl_GetHashValue(he);
      if (link != NULL)
	 cused[link->controller_id] = 1;
      he = Tcl_NextHashEntry(&hs);
   }

   for (i = 0; i < MAX_CONTROLLERS; i++) {
      if (cused[i] == 0) {
	 if (controller[i].fd >= 0) {
	    Fprintf(interp, stderr, "Closing controller %d\n", i);
	    close(controller[i].fd);
	    controller[i].fd = -1;
	    controller[i].initialized = 0;
	 }
      }
   }
}
  
/*--------------------------------------------------------------*/
/*--------------------------------------------------------------*/

int
gpib_close_all_links(int cid)
{
   gpib_record *link;
   Tcl_HashEntry *he;
   Tcl_HashSearch hs;
   int numlinks = 0;

   he = Tcl_FirstHashEntry(&linkHash, &hs);
   while (he != NULL) {
      link = Tcl_GetHashValue(he);
      if (link != NULL) {
	 if (link->controller_id == cid) {
	    Tcl_DeleteHashEntry(he);
	    free(link);
	 }
	 numlinks++;
      }
      he = Tcl_NextHashEntry(&hs);
   }
   return numlinks;
}

/*--------------------------------------------------------------*/
/*--------------------------------------------------------------*/

int
gpib_close(Tcl_Interp *interp, int cid)
{
   if (controller[cid].fd < 0) return -1;	/* No such device */
   close(controller[cid].fd);
   controller[cid].fd = -1;
   controller[cid].initialized = 0;
   gpib_close_all_links(cid);

   Fprintf(interp, stderr, "Closing interface\n");
   return 0;
}

/*--------------------------------------------------------------*/
/*--------------------------------------------------------------*/

int
gpib_close_link(Tcl_Interp *interp, char *devname)
{
   int cid, result, ndevs;
   Tcl_HashEntry *he;
   Tcl_HashSearch hs;
   gpib_record *link;

   /* Check for record */
   he = Tcl_FindHashEntry(&linkHash, devname);
   if (he != NULL) {
      link = (gpib_record *)Tcl_GetHashValue(he);
      cid = link->controller_id;
      Tcl_DeleteHashEntry(he);
      free(link);

      /* Was the controller addressing the device? */
      if (controller[cid].active == link->device_id)
	 controller[cid].active = -1;

      /* Was that the last or only device on cid? */
      ndevs = 0;
      he = Tcl_FirstHashEntry(&linkHash, &hs);
      while (he != NULL) {
         link = Tcl_GetHashValue(he);
         if (link != NULL) {
	    if (link->controller_id == cid) {
	       ndevs++;
	       break;
	    }
         }
         he = Tcl_NextHashEntry(&hs);
      }
      if (ndevs == 0) {
	 gpib_close(interp, cid);	/* Close the controller */
      }
      return 0;
   }
   return -1;
}

/*--------------------------------------------------------------*/
/* Find a GPIB controller accessing the GPIB device "devname"	*/
/* If there is a unique controller and device, that controller	*/
/* ID will be returned.  If a device is unassigned, but there 	*/
/* is only one controller, then it will be returned.		*/
/* A return value of -2 indicates that there were no		*/
/* controllers found.  A return value of -3 indicates that	*/
/* there are multiple controllers that might be controlling	*/
/* the device (result ambiguous).				*/
/*--------------------------------------------------------------*/

int
gpib_find_controller(char *devname, gpib_record **rlink)
{
   Tcl_HashEntry *he;
   gpib_record *link;
   int i, j, cid = -2;

   /* Check for existing record */
   he = Tcl_FindHashEntry(&linkHash, devname);
   if (he != NULL) {
      link = (gpib_record *)Tcl_GetHashValue(he);
      if (link != NULL) {
	 if (rlink != NULL) *rlink = link;
         return link->controller_id;
      }
   }

   /* If there is only one Prologix controller, return its	*/
   /* index.  Otherwise, the result is ambiguous and we must	*/
   /* poll for a device.					*/

   for (i = 0; i < MAX_CONTROLLERS; i++) {
      if (controller[i].initialized) {
	 if (cid == i)
	    cid = -3;
	 else
	    cid = i;
      }
   }
   if (rlink != NULL) *rlink = NULL;
   return cid;
}

/*--------------------------------------------------------------*/
/* gpib_address_device tells the Prologix controller to		*/
/* address the given device.  This routine can be called in	*/
/* two ways:  (1) With a device name, in which the record for	*/
/* the device is found and returned in the record pointer, or	*/
/* (2) with a valid record pointer, in which case the search	*/
/* is skipped.							*/
/*--------------------------------------------------------------*/

int
gpib_address_device(char *devname, gpib_record **link)
{
   int cid, addr;
   char sendstr[20];

   if (*link == NULL)
      cid = gpib_find_controller(devname, link);
   else
      cid = (*link)->controller_id;

   if ((cid < 0) || (*link == NULL)) return -1;

   addr = (*link)->device_id;

   if (controller[cid].active != addr) {
      controller[cid].active = addr;
      sprintf(sendstr, "++addr %d\r", addr);
      write(controller[cid].fd, sendstr, strlen(sendstr));
   }
   return cid;
}

/*--------------------------------------------------------------*/
/*--------------------------------------------------------------*/

int
gpib_local(char *devname)
{
   int cid;
   gpib_record *link = NULL;

   cid = gpib_address_device(devname, &link);
   if (cid < 0 || link == NULL) return -1;
   write(controller[cid].fd, "++loc\r", 6);
   controller[cid].active = -1;
   return 0;
}

/*--------------------------------------------------------------*/
/*--------------------------------------------------------------*/

int
gpib_trigger(char *devname)
{
   int cid;
   gpib_record *link = NULL;
   char sendstr[20];

   cid = gpib_address_device(devname, &link);
   if (cid < 0 || link == NULL) return -1;
   write(controller[cid].fd, "++loc\r", 6);
   return 0;
}

/*--------------------------------------------------------------*/
/*--------------------------------------------------------------*/

int
gpib_clear(char *devname)
{
   int cid;
   gpib_record *link = NULL;

   cid = gpib_address_device(devname, &link);
   if (cid < 0 || link == NULL) return -1;
   write(controller[cid].fd, "++clr\r\n", 7);
   return 0;
}

/*--------------------------------------------------------------*/
/* Query GPIB device status from the currently addressed 	*/
/* device by initiating a serial poll.  This routine makes no	*/
/* assumptions about what device is currently addressed, or	*/
/* whether or not a device exists at that addressed.  It is	*/
/* used by other routines in this source file.			*/
/*								*/
/* Return value:  Positive when a status byte was received,	*/
/* negative if no response was received or an error occurred.	*/
/* The status byte itself is returned in "status".		*/
/*--------------------------------------------------------------*/

int
gpib_status(int cid, int timeout, int *status)
{
   char buffer[20];
   int result, locstat;

   write(controller[cid].fd, "++spoll\r", 8);

   result = poll_and_read(controller[cid].fd, buffer, (size_t)19, timeout);
   if (result > 0) {
      buffer[result] = '\0';
      result = sscanf(buffer, "%d", &locstat);
      if (result <= 0) controller[cid].error = 2;	/* No listener */
      else {
	 controller[cid].error = 0;
	 if (status) *status = locstat;
      }
   }
   else {
      controller[cid].error = 15;	/* Serial poll response lost */
   }
   return result;
}

/*--------------------------------------------------------------*/
/*--------------------------------------------------------------*/

int
gpib_status_byte(char *devname, int *status)
{
   int cid, result = -1, locstat;
   gpib_record *link = NULL;

   if ((cid = gpib_address_device(devname, &link)) < 0) return cid;
   if (link && !(link->flags & HAS_SPOLL)) return -1;
   result = gpib_status(link->controller_id, 250, &locstat);
   if (status) *status = locstat;
   return result;
}

/*--------------------------------------------------------------*/
/* Blind poll (read device status on a device which is not	*/
/* necessarily known to exist)					*/
/*--------------------------------------------------------------*/

int
gpib_poll_status(int addr, int cid)
{
   char buffer[20];
   int result = -1, status;

   if (cid < 0) return -1;

   if (controller[cid].active != addr) {
      controller[cid].active = addr;
      sprintf(buffer, "++addr %d\r", addr);
      write(controller[cid].fd, buffer, strlen(buffer));
   }
   result = gpib_status(cid, 250, &status);
   if (result < 0)
      return result;
   else
      return status;
}

/*--------------------------------------------------------------*/
/*--------------------------------------------------------------*/

int
gpib_send(Tcl_Interp *interp, char *devname, void *databuf, long datacnt, int locflags)
{
   int cid, flags, eotbits, tpos, fd;
   gpib_record *link = NULL;
   char dc, terminator[3] = "\r\n";
   char *outbuf;
   static char *locbuf = (char *)NULL;
   static int loccnt = 0;
   int esccnt = 0;
   int outcnt = datacnt;

   cid = gpib_address_device(devname, &link);
   if (cid < 0 || link == NULL) return -1;

   flags = link->flags;
   fd = controller[cid].fd;
   outbuf = (locbuf != NULL) ? locbuf : (char *)databuf;

   if (locflags & NO_NEWLINE)
      eotbits = flags & ~(TERM_LF | TERM_CR | TERM_EOI);
   else
      eotbits = flags & (TERM_LF | TERM_CR);

   /* In binary block data, all CR and NL characters must be escaped */
   esccnt = 0;
   if (locflags & IS_BINARY) {
      for (tpos = 0; tpos < datacnt; tpos++) {
	 dc =  *((char *)databuf + tpos);
	 if (dc == '\r' || dc == '\n' || dc == '\033')
	    datacnt--;
	 else if (dc != '\0')
	    break;
      }
   }

   if (esccnt > 0) {
      if (locbuf != NULL)
	 locbuf = (char *)realloc(locbuf, loccnt + esccnt + datacnt);
      else
	 locbuf = (char *)malloc(esccnt + datacnt);

      /* Escape character processing */

      esccnt = 0;
      for (tpos = 0; tpos < datacnt; tpos++) {
	 dc = *((char *)databuf + tpos);
	 if (dc == '\r' || dc == '\n' || dc == '\033') {
	    *((char *)locbuf + loccnt + tpos + esccnt) = '\033';
	    esccnt++;
	 }
	 *((char *)locbuf + loccnt + tpos + esccnt) = dc;
      }
      loccnt += datacnt + esccnt;
      outbuf = locbuf;
      outcnt = loccnt;
   }
   else {
      if (locbuf != NULL) {
	 locbuf = (char *)realloc(locbuf, loccnt + datacnt);
	 memcpy(locbuf + loccnt, databuf, datacnt);
	 loccnt += datacnt;
	 outbuf = locbuf;
	 outcnt = loccnt;
      }
      else if (!eotbits) {	// Save the string for transmission later
	 locbuf = (char *)malloc(datacnt);
	 memcpy(locbuf, databuf, datacnt);
	 loccnt = datacnt;
      }
   }

   if (!eotbits) return;	// punt on writing if no termination

   if (outcnt < 499) {
      // No buffer overflow---simple setup and write
      if (flags & TERM_EOI)
	 write(fd, "++eoi 1\r", 8);	/* Enable EOI assertion */
      else
	 write(fd, "++eoi 0\r", 8);	/* Disable EOI assertion */

      if (flags & IS_BINARY) {
	 write(fd, "++eos 3\r", 8);
      }
      else {
	 switch (eotbits) {
	    case TERM_LF:
	       write(fd, "++eos 2\r", 8);
	       break;
	    case TERM_CR:
	       write(fd, "++eos 1\r", 8);
	       break;
	    case TERM_CR | TERM_LF:
	       write(fd, "++eos 0\r", 8);
	       break;
	 }
      }

      // Set auto read-back mode, if requested
      if ((flags & READ_AUTO) || (locflags & READ_AUTO)) {
	 Fprintf(interp, stderr, "Auto read-back\n");
	 write(fd, "++auto 1\r", 9);
      }

      write(fd, outbuf, (size_t)outcnt);
      write(fd, terminator, (size_t)2);
   }
   else {

      // Avoid terminating data early
      write(fd, "++eos 3\r", 8);
      write(fd, "++eoi 0\r", 8);

      write(fd, outbuf, (size_t)outcnt);
      write(fd, terminator, (size_t)2);

      switch (eotbits) {
	 case TERM_LF:
	    write(fd, "++eos 2\r", 8);
	    break;
	 case TERM_CR:
	    write(fd, "++eos 1\r", 8);
	    break;
	 case TERM_LF | TERM_CR:
	    write(fd, "++eos 0\r", 8);
	    break;
      }

      if (flags & TERM_EOI)
	 write(fd, "++eoi 1\r", 8);	/* Enable EOI assertion */
   }

   if (locbuf != (char *)NULL) {
      free(locbuf);
      locbuf = NULL;
      loccnt = 0;
   }    
   return 0;
}

// May want to replace the following with a delay passed to the
// gpib_receive() routine

#define DELAY_COUNT 5
#define STAT_DELAY 10000

/*--------------------------------------------------------------*/
/*--------------------------------------------------------------*/

int
gpib_receive(Tcl_Interp *interp, char *devname, unsigned char *buffer, long cnt,
		int *rstatus, gpib_record **rlink)
{
   int cid, flags, count, fd, i, status, stbbit, result;
   char sendstr[20];
   gpib_record *link = NULL;

   if (rlink) link = *rlink;
   if (link == NULL) {
      cid = gpib_find_controller(devname, &link);
      if (rlink) *rlink = link;
      if (cid < 0 || link == NULL) return -1;
   }
   else
      cid = link->controller_id;

   flags = link->flags;
   fd = controller[cid].fd;
   stbbit = flags & READ_STB;

   if (!(flags & READ_AUTO)) {

      if (stbbit) {
         for (i = 0; i < DELAY_COUNT; i++) {
	    gpib_address_device(NULL, &link);
	    result = gpib_status(cid, 250, &status);
	    // Fprintf(interp, stderr, "pre-read result = %d, status = %d\n", result, status);
	    if (result < 0) break;
	    if ((status & stbbit) != 0) break;
	    usleep(STAT_DELAY);	  /* 1/100 second delay between status readings */
	 }
	 if ((result < 0) || ((status & stbbit) == 0)) {
	    controller[cid].error = 15;	/* device has nothing to send */
	    if (rstatus) *rstatus = status;
	    return -1;
	 }
      }
      else {
	 usleep(STAT_DELAY * DELAY_COUNT);   /* 1/10 second delay before reading */
         gpib_address_device(NULL, &link);
      }

      if ((flags & READ_EOI) != 0)
	 write(fd, "++read eoi\r", 11);
      else if (flags & READ_LF)
         write(fd, "++read 10\r", 10);
      else if (flags & READ_CR)
         write(fd, "++read 13\r", 10);
      else
         write(fd, "++read\r", 7);
   }

   usleep(STAT_DELAY * DELAY_COUNT);   /* 1/10 second delay before reading */
   count = poll_and_read(fd, buffer, (size_t)cnt, (stbbit) ? 0 : 300);

   switch(count) {
      case -1:
	 controller[cid].error = 6;
	 break;
      case 0:
	 controller[cid].error = 2;	/* timeout; no listener detected */
 	 break;
      default:
	 controller[cid].error = 0;
	 break;
   }
   buffer[(count > 0) ? count : 0] = '\0';

   if (count < cnt) {
      if (stbbit) {
         result = gpib_status(cid, 250, &status);
         // Fprintf(interp, stderr, "post-read result = %d, status = %d count = %d\n",
      	 // 		result, status, count);
      }
   }
   else
      controller[cid].error = 1;	/* buffer full */

   if (rstatus) *rstatus = status;
   return count;
}

#define BUFSIZE 1024
static char *locmessage;

/*--------------------------------------------------------------*/
/* Read into, and return, an allocated buffer.  Read until the	*/
/* device reports having nothing more to send.			*/
/*--------------------------------------------------------------*/

int gpib_buffered_read(Tcl_Interp *interp, char *devname, unsigned char **buffer,
		long *nret)
{
   int bufsize = (2 * BUFSIZE), nread = 0, status, r;
   char *msgptr = locmessage;
   gpib_record *link = NULL;

   *msgptr = '\0';	// Make sure previous contents are gone
   while (1) {
      r = gpib_receive(interp, devname, msgptr, BUFSIZE, &status, &link);
      if ((r < 0) || (link == NULL)) break;
      else if (r == BUFSIZE) link->flags |= READ_AUTO;
      nread += r;
      if (r < BUFSIZE) {
         if (link->flags & STB_RDY) break;
         else if (!(status & link->flags & READ_STB)) break;
      }
      if (nread + BUFSIZE >= bufsize) {
	 // The following line is meant to avoid hanging the program
	 // when a device is configured to continuously send data.
	 // Note that it may be better to pass a flag indicating that
	 // this may be a long read. . .
	 if (!(link->flags & READ_EOI)) break;

	 bufsize += BUFSIZE;
	 locmessage = realloc(locmessage, bufsize + 1);
      }
      msgptr = locmessage + nread;
   }
   if (link != NULL) link->flags &= ~READ_AUTO;
   *buffer = (unsigned char *)locmessage;
   *nret = (long)nread;
   return 0;
}

/*--------------------------------------------------------------*/
/* List all open GPIB devices					*/
/*--------------------------------------------------------------*/

void gpib_list_links(Tcl_Interp *interp)
{
   Tcl_HashEntry *he;
   Tcl_HashSearch hs;
   gpib_record *link;
   int result;
   Tcl_Obj *tobj, *lobj;

   if (interp)
      lobj = Tcl_NewListObj(0, NULL);
   else
      Fprintf(interp, stdout, "Open links:\n");
   he = Tcl_FirstHashEntry(&linkHash, &hs);
   while (he != NULL) {
      link = Tcl_GetHashValue(he);
      if (link != NULL) {
	 gpib_address_device(NULL, &link);
	 /* Find listener */
	 result = (link->flags & HAS_SPOLL) ?
			gpib_status(link->controller_id, 250, NULL) : 0;
	 if (interp) {
	    tobj = Tcl_NewListObj(0, NULL);
	    Tcl_ListObjAppendElement(interp, tobj, Tcl_NewIntObj(link->device_id));
	    Tcl_ListObjAppendElement(interp, tobj, Tcl_NewIntObj(result));
	    Tcl_ListObjAppendElement(interp, lobj, tobj);
         }
	 else
	    Fprintf(interp, stdout, "ID %d: (%slistening)\n", link->device_id,
		(result > 0) ? "" : "not ");
      }
      he = Tcl_NextHashEntry(&hs);
   }
   if (interp)
      Tcl_SetObjResult(interp, lobj);
   else
      Fprintf(interp, stdout, "\n");
}

/*--------------------------------------------------------------*/
/* Poll for listeners on a controller.  This only works for	*/
/* devices that have serial poll capability.			*/
/* Returns the number of listeners on the controller, with an	*/
/* allocated array of values passed back in "list"		*/
/*--------------------------------------------------------------*/

int
gpib_find_listeners(Tcl_Interp *interp, int *list, int start, int end)
{
   int i, fd, addr, nresp = 0;
   gpib_record *link;

   if (start < 0) start = 0;
   if (end > MAX_GPIB_ADDR) end = MAX_GPIB_ADDR;
   if (end < start) end = start;

   /* Make sure all available controllers are open */
   open_gpib_interfaces(interp);

   for (addr = 0; addr <= MAX_GPIB_ADDR; addr++) *(list + addr) = -1;

   for (i = 0; i < MAX_CONTROLLERS; i++) {
      if (controller[i].fd >= 0) {
	 fd = controller[i].fd;

         // Fprintf(interp, stderr, "Querying controller %d\n", i);

         write(fd, "++read_tmo_ms 200\r", 18);

         for (addr = start; addr <= end; addr++) {
	    if (*(list + addr) == -1) {
	       Fprintf(interp, stderr, "Listening for device %d", addr);
               *(list + addr) = gpib_poll_status(addr, i);
               if (*(list + addr) >= 0) {
		  nresp++;
		  Fprintf(interp, stderr, " status=%d\n", *(list + addr));
	       }
	       else
		  Fprintf(interp, stderr, " (no response)\n");
	    }
         }
         write(fd, "++read_tmo_ms 280\r", 18);
      }
   }

   close_unused_controllers(interp);
   return nresp;
}

/*--------------------------------------------------------------*/
/*--------------------------------------------------------------*/

void gpib_global_init()
{
    Tcl_InitHashTable(&linkHash, TCL_STRING_KEYS);
    Tcl_InitHashTable(&nameHash, TCL_STRING_KEYS);
    locmessage = malloc((2 * BUFSIZE) + 1);
}

/*--------------------------------------------------------------*/
/* Configuration to send to the Prologix controller when it is	*/
/* first opened.						*/
/*--------------------------------------------------------------*/

void gpib_init_controller(int fd)
{
    write(fd, "++mode 1\r", 9);
    write(fd, "++ifc\r", 6);
    usleep(150);
    write(fd, "++read_tmo_ms 280\r", 18);
    write(fd, "++auto 0\r", 9);
}

/*--------------------------------------------------------------*/
/* gpib_init: called each time a link is created   		*/
/* Return the GPIB id number (index into array of		*/
/* controllers; multiple devices may be accessed,		*/
/* possibly on multiple Prologix devices)			*/
/*								*/
/* If "flags" includes HAS_SPOLL, then we poll for the device	*/
/* address, and close all other unused controllers.  If not,	*/
/* then we keep all GPIB addresses open (future---maybe 	*/
/* provide a command to check for a response)			*/
/*								*/
/* Return value is either the controller ID number (>= 0), or	*/
/* an error code (-2 -> no controllers, -3 -> no address	*/
/* mapping for name)						*/
/*--------------------------------------------------------------*/

int gpib_init(Tcl_Interp *interp, char *devname, int flags)
{
   int i, result, cid, isopen, status, device_id, locflags;
   long laddr;
   gpib_record *link;
   Tcl_HashEntry *he;

   /* Determine the GPIB address of this device.  It must either have	*/
   /* been entered in the device map using the "mapid" command, or else	*/
   /* the name must be able to be parsed as an integer address.		*/

   he = Tcl_FindHashEntry(&nameHash, (CONST char *)devname);
   if (he != NULL) {
      link = Tcl_GetHashValue(he);
      device_id = link->device_id;
      locflags = link->flags;
      if (flags != -1) locflags |= flags;
   }
   else {
      result = sscanf(devname, "%d", &device_id);
      if (result != 1) {
	 if (strncasecmp(devname, "gpib", 4)) {
            result = sscanf(devname + 4, "%d", &device_id);
	 }
	 if (result != 1) return -3;
      }
      locflags = (flags != -1) ? flags : DEFAULT_FLAGS;
   }

   /* Find if any controller has already been opened */
   isopen = 0;

   for (i = 0; i < MAX_CONTROLLERS; i++) {
      if (controller[i].initialized) {
	 isopen = 1;
	 break;
      }
   }

   /* Always check for new boards, since USB is hot-pluggable */

   result = open_gpib_interfaces(interp);

   if (!isopen && result != 0) {
      Fprintf(interp, stderr, "Initialize Controller Failed (result code %i)\n", result);
      return result;
   }

   cid = gpib_find_controller(devname, NULL);

   /* If the controller ID is ambiguous (i.e., more than one controller),	*/
   /* we will poll each controller to find the device, which must in this	*/
   /* case respond to a serial poll.						*/

   if (cid < 0) {
      char buffer[20];

      if (cid == -2) {
	 return cid;
      }
      else if (flags & HAS_SPOLL) {
         for (cid = 0; cid < MAX_CONTROLLERS; cid++) {
	    if (controller[cid].fd >= 0) {
	       sprintf(buffer, "++addr %d\r", device_id);
	       write(controller[cid].fd, buffer, strlen(buffer));
	       write(controller[cid].fd, "++spoll\r", 8);
	       status = poll_and_read(controller[cid].fd, buffer, (size_t)19, 500);
	       if (status > 0) {
	          buffer[status] = '\0';
	          status = sscanf(buffer, "%d", &result);
	          if (status > 0) break;
	       }
	    }
         }
         if (cid == MAX_CONTROLLERS) cid = -1;
      }
   }

   if (cid >= 0) {
      /* Create an entry for the device */
      link = (gpib_record *)malloc(sizeof(gpib_record));
      link->controller_id = cid;
      link->device_id = device_id;
      link->flags = locflags;
      he = Tcl_CreateHashEntry(&linkHash, (CONST char *)devname, &i);
      Tcl_SetHashValue(he, link);
   }

   /* Close any devices not being used */

   close_unused_controllers(interp);

   return cid;
}

/*--------------------------------------------------------------*/
