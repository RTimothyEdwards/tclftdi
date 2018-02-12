/*
 *------------------------------------------------------------
 * gpib_controller.c
 *------------------------------------------------------------
 * Code for opening and managing one or more Prologix GPIB
 * controller devices.
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

/*--------------------------------------------------------------*/

per_controller_state_t controller[MAX_CONTROLLERS] = {
  {0, 0, -1, -1, 0, 0, 0, NULL},
  {0, 0, -1, -1, 0, 0, 0, NULL},
  {0, 0, -1, -1, 0, 0, 0, NULL},
  {0, 0, -1, -1, 0, 0, 0, NULL},
  {0, 0, -1, -1, 0, 0, 0, NULL}
};

/*--------------------------------------------------------------*/
/* Poll a device for input with a timeout.  This complements	*/
/* the use of read() where the inter-character timer is not	*/
/* started until the first character has been seen.		*/
/*								*/
/* Use timeout = 0 to skip the initial poll and begin an	*/
/* immediate read.						*/
/*--------------------------------------------------------------*/

int
poll_and_read(int fd, char *buffer, int length, int timeout)
{
   int result, nbytes;
   struct pollfd mypoll;

   mypoll.fd = fd;
   mypoll.events = POLLIN | POLLPRI | POLLERR | POLLHUP | POLLNVAL;
   mypoll.revents = 0;

   if (timeout > 0)
      result = poll(&mypoll, 1, timeout); 
   else {
      result = 1;
      mypoll.revents = POLLIN;
   }

   if (result < 0) return -2;	/* Error */
   else if (result == 0) return -1;	/* Timed out */
   else if (((mypoll.revents & (POLLIN || POLLPRI)) != 0) &&
		((mypoll.revents & (POLLERR || POLLHUP || POLLNVAL)) != 0)) {
      nbytes = read(fd, buffer, length);
      while (1) {
	 if ((length - nbytes) <= 0) return nbytes;
	 result = poll(&mypoll, 1, 100);	/* short intercharacter delay */
	 if (result < 0) return -2;		/* Error */
	 else if (result == 0) return nbytes;	/* No more data */
	 else if ((mypoll.revents & (POLLIN || POLLPRI)) == 0) return -2;
	 else if ((mypoll.revents & (POLLERR || POLLHUP || POLLNVAL)) != 0) return -2;
         nbytes += read(fd, buffer + nbytes, length - nbytes);
      }
   }
   else
      return -2;	/* error, hangup, or closed file descriptor */
}

/*--------------------------------------------------------------*/
/*--------------------------------------------------------------*/

int
open_usb_serial(char *devname)
{
   int fd;
   struct termios tios;

   if ((fd = open(devname, O_RDWR)) >= 0) {

      /* Set terminal attributes (baud, parity, non-blocking) */

      tcgetattr(fd, &tios);

      cfmakeraw(&tios);
      cfsetospeed(&tios, B115200);
      cfsetispeed(&tios, B115200);

      tios.c_cflag |= CRTSCTS;		/* Assert hardware flow control */
      tios.c_iflag |= IGNCR;		/* Ignore carriage return */

      /* Set nonblocking (poll() handles timeouts) */
      tios.c_cc[VTIME] = 1;
      tios.c_cc[VMIN] = 0;

      tcsetattr(fd, TCSANOW, &tios);
      tcflush(fd, TCIOFLUSH);
   }
   return fd;
}

/*--------------------------------------------------------------*/
/*--------------------------------------------------------------*/

int
controller_set_data(int cid, void *ptr)
{
   if (cid >= MAX_CONTROLLERS) return -2;
   if (controller[cid].initialized == 0) return -2;
   if (controller[cid].data != NULL) return -1;
   controller[cid].data = ptr;
   return 0; 
}

/*--------------------------------------------------------------*/
/*--------------------------------------------------------------*/

void *
controller_get_data(int cid)
{
   if (cid >= MAX_CONTROLLERS) return NULL;
   if (controller[cid].initialized == 0) return NULL;
   return controller[cid].data;
}

/*--------------------------------------------------------------*/
/* Open all available prologix/USB interfaces.			*/
/*--------------------------------------------------------------*/

int
open_gpib_interfaces(Tcl_Interp *interp)
{
   int status = 0, scnt, fd, i, tty_used, board, foundone, nbytes;
   char buffer[60], *endofline;
   char line[200];
   FILE *mf;
   unsigned char hasrootaccess = 1;
   int devtty;
   DIR *cwd = NULL;
   struct direct *dp;

   foundone = 0;

   /* Prologix USB devices are based on an FTDI USB-serial	*/
   /* controller located at /dev/ttyUSB*.  We query /proc to	*/
   /* find all such FTDI devices, then we reject all those	*/
   /* belonging to already-opened controllers.  Then we poll	*/
   /* each controller until we find an available one of the	*/
   /* correct type.						*/

   /* Check /proc/tty/driver/usbserial to (1) make sure we are running	*/
   /* as root and (2) check which device the FTDI is connected to	*/ 

   mf = fopen("/proc/tty/driver/usbserial", "r");

   if (mf == NULL) { 	/* Alternative method for non-root access */

      devtty = 0;
      hasrootaccess = 0;

      /* Get directory information from /dev/ on all known ttyUSB devices */
      cwd = opendir("/dev");
   }

   buffer[0] = '\0';
   while (1) {
      if (hasrootaccess) {
         if (fgets(line, 199, mf) == NULL) break;
         if (strstr(line, "FTDI") == NULL) continue;
	 if (sscanf(line, "%d:", &devtty) == 1)
	    sprintf(buffer, "/dev/ttyUSB%d", devtty);

      }
      else if (cwd == NULL) {
	 sprintf(buffer, "/dev/ttyUSB%d", devtty);
	 devtty++;
      }
      else {
	 while ((dp = readdir(cwd)) != NULL) {
	    if (!strncmp(dp->d_name, "ttyUSB", 6)) {
	       sscanf(dp->d_name + 6, "%d", &devtty);
	       sprintf(buffer, "/dev/%s", dp->d_name);
	       break;
	    }
         }
	 if (dp == NULL) break;
      }

      tty_used = 0;
      for (i = 0; i < MAX_CONTROLLERS; i++) {
	 if (controller[i].initialized &&
			(controller[i].dev == devtty)) {
	    tty_used = 1;
	    break;
	 }
      }
      if (tty_used) continue;

      /* Find the first available controller slot */

      for (board = 0; board < MAX_CONTROLLERS; board++) {
	 if (!controller[board].initialized)
	    break;
      }
      if (board == MAX_CONTROLLERS) {
	 Fprintf(interp, stderr, "No available slots for controller!\n");
	 return -2;
      }

      /* Open the device and query its version string	*/
      /* Leave any valid devices open.			*/

      if ((fd = open_usb_serial(buffer)) >= 0) {
	 int firmrev;
	 int btotal;

	 status = 0;

	 // Read 12 bytes to test for unit name

         write(fd, "++ver\r", 6);
         nbytes = poll_and_read(fd, buffer, (size_t)12, 250);

	 if (nbytes < 12) {	/* Give it a second shot. . . */
	    tcflush(fd, TCIOFLUSH);
            write(fd, "++ver\r", 6);
            nbytes = poll_and_read(fd, buffer, (size_t)12, 250);
	 }
	 if (nbytes <= 0)
	    Fprintf(interp, stderr, "Error on device read (%d)!\n", nbytes);
	 else if (nbytes < 12)
	    Fprintf(interp, stderr, "Received only %d bytes!\n", nbytes);

	 if (nbytes < 12) continue;

	 buffer[12] = '\0';

	 if (!strncmp(buffer, "Prologix", 8)) {
	    poll_and_read(fd, buffer + 12, (size_t)37, 250);
	    if ((endofline = strchr(buffer, '\n')) != NULL)
		*endofline = '\0';
	    status = 1;
	    controller[board].revision = 0;
	 }
      }
      else {
	 struct stat sbuf;

	 if (stat(buffer, &sbuf) < 0) break;	// No such device; stop the loop
	 else if ((sbuf.st_mode & 07) == 0) {
	    Fprintf(interp, stderr, "Warning: file permissions of %s set to %3o\n",
			buffer, sbuf.st_mode & 0777);
         }
      }

      if (status) {
	 controller[board].initialized = 2;	// Denotes new
	 controller[board].fd = fd;
	 controller[board].dev = devtty;
	 controller[board].error = 0;
	 controller[board].active = -1;
	 controller[board].data = NULL;
	 foundone = 1;
         Fprintf(interp, stderr, "Initializing controller \"%s\" for IO\n", buffer);
      }
      else {
	 close(fd);
      }
   }

   if (hasrootaccess) fclose(mf);
   if (cwd) closedir(cwd);

   if (foundone == 0 && tty_used == 0) {
      Fprintf(interp, stderr, "No compatible controller device found!\n");
      return -1;
   }
   
   /* Initialize all newly opened controllers */

   for (i = 0; i < MAX_CONTROLLERS; i++) {
      if (controller[i].initialized == 2) {
	 controller[i].initialized = 1;
	 status = 0;
      }
      fd = controller[i].fd;
      gpib_init_controller(fd);
   }
   return status;
}

