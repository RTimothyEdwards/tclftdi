#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <unistd.h>

#include <tcl.h>

#include "gpib_driver.h"

/*--------------------------------------------------------------*/
/*--------------------------------------------------------------*/

typedef struct {
   const char *cmdstr;
   void	      (*func)();
} cmdstruct;

/*--------------------------------------------------------------*/
/* Forward references						*/
/*--------------------------------------------------------------*/

int gpibtcl_mapid();
int gpibtcl_listid();
int gpibtcl_findid();
int gpibtcl_openid();
int gpibtcl_closeid();
int gpibtcl_remote();
int gpibtcl_local();
int gpibtcl_clearid();
int gpibtcl_trigger();
int gpibtcl_status();
int gpibtcl_binblock();
int gpibtcl_write();
int gpibtcl_read();

int parse_flag_list(Tcl_Interp *, Tcl_Obj *, int *);

/*--------------------------------------------------------------*/
/* Command data							*/
/*--------------------------------------------------------------*/

static cmdstruct gpib_commands[] =
{
   {"gpib::mapid", (void *)gpibtcl_mapid},
   {"gpib::listid", (void *)gpibtcl_listid},
   {"gpib::findid", (void *)gpibtcl_findid},
   {"gpib::openid", (void *)gpibtcl_openid},
   {"gpib::closeid", (void *)gpibtcl_closeid},
   {"gpib::remote", (void *)gpibtcl_remote},
   {"gpib::local", (void *)gpibtcl_local},
   {"gpib::clearid", (void *)gpibtcl_clearid},
   {"gpib::trigger", (void *)gpibtcl_trigger},
   {"gpib::status", (void *)gpibtcl_status},
   {"gpib::binblock", (void *)gpibtcl_binblock},
   {"gpib::write", (void *)gpibtcl_write},
   {"gpib::read", (void *)gpibtcl_read},
   {"", NULL} /* sentinel */
};

/*--------------------------------------------------------------*/
/*								*/
/*--------------------------------------------------------------*/

int
gpib_command_init(Tcl_Interp *interp)
{
   char command[256];
   int cmdidx;

   if (interp == NULL) return TCL_ERROR;

   for (cmdidx = 0; gpib_commands[cmdidx].func != NULL; cmdidx++) {
      sprintf(command, "%s", gpib_commands[cmdidx].cmdstr);
      Tcl_CreateObjCommand(interp, command,
		(Tcl_ObjCmdProc *)gpib_commands[cmdidx].func,
		(ClientData)NULL, (Tcl_CmdDeleteProc *)NULL);
   }
   Tcl_Eval(interp, "namespace eval gpib namespace export *");
}

/*--------------------------------------------------------------*/
/* Maintain a record of which device names correspond to which	*/
/* GPIB addresses.						*/
/*--------------------------------------------------------------*/

int
gpibtcl_mapid(ClientData clientData,
	Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   int result, addr, flags;

   if (objc == 2) { 
      addr = gpib_get_address(Tcl_GetString(objv[1]));
      Tcl_SetObjResult(interp, Tcl_NewIntObj(addr));
      return TCL_OK;
   }
   if (objc < 3 || objc > 4) {
      Tcl_WrongNumArgs(interp, 1, objv, "<device_name> <gpib_address> [{flags}]");
      return TCL_ERROR;
   }

   if (objc == 4)
      result = parse_flag_list(interp, objv[3], &flags);
   else
      result = parse_flag_list(interp, NULL, &flags);
   if (result != TCL_OK) return result;

   result = Tcl_GetIntFromObj(interp, objv[2], &addr);
   if (result != TCL_OK) return result;
   if (addr < 0 || addr > 31) {
      Tcl_SetResult(interp, "Invalid address", NULL);
      return TCL_ERROR;
   }

   gpib_map_address(Tcl_GetString(objv[1]), addr, flags);
   return TCL_OK;
}

/*--------------------------------------------------------------*/
/*								*/
/*--------------------------------------------------------------*/

int
gpibtcl_listid(ClientData clientData,
	Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   if (objc > 1) {
      Tcl_WrongNumArgs(interp, 1, objv, "(no arguments)");
      return TCL_ERROR;
   }
   gpib_list_links(interp);
   return TCL_OK;
}

/*--------------------------------------------------------------*/
/*								*/
/*--------------------------------------------------------------*/

int
gpibtcl_findid(ClientData clientData,
	Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   int nresp, i, list[MAX_GPIB_ADDR + 1], begin, end, result;
   Tcl_Obj *lobj, *tobj;

   if (objc > 3) {
      Tcl_WrongNumArgs(interp, 1, objv, "[begin [end]]");
      return TCL_ERROR;
   }
   if (objc == 1) {
      begin = 0;
      end = MAX_GPIB_ADDR;
   }
   else {
      result = Tcl_GetIntFromObj(interp, objv[1], &begin);
      if (result != TCL_OK) return result;

      if (objc == 2)
	 end = begin;
      else {
         result = Tcl_GetIntFromObj(interp, objv[2], &end);
         if (result != TCL_OK) return result;
      }
   }

   nresp = gpib_find_listeners(interp, &list[0], begin, end);

   if (nresp > 0) {
      lobj = Tcl_NewListObj(0, NULL);
      for (i = 0; i <= MAX_GPIB_ADDR; i++) {
	 if (list[i] >= 0) {
	    tobj = Tcl_NewIntObj(i);
            Tcl_ListObjAppendElement(interp, lobj, tobj);
	 }
      }
      Tcl_SetObjResult(interp, lobj);
   }

   return TCL_OK;
}

/*--------------------------------------------------------------*/
/*								*/
/*--------------------------------------------------------------*/

int
parse_flag_list(Tcl_Interp *interp, Tcl_Obj *lobj, int *flags)
{
   Tcl_Obj *tobj;
   char *flagstr;
   int llen, i, result, stbbit;

   if (flags == NULL) return TCL_ERROR;
   *flags = DEFAULT_FLAGS;
   if (lobj == NULL) return TCL_OK;

   /* Parse list of flags */
   Tcl_ListObjLength(interp, lobj, &llen);
   for (i = 0; i < llen; i++) {
      result = Tcl_ListObjIndex(interp, lobj, i, &tobj);
      if (result != TCL_OK) return result;
      flagstr = Tcl_GetString(tobj);
      if (!strcasecmp(flagstr, "no_read_stb")) *flags &= ~READ_STB;
      else if (!strcasecmp(flagstr, "no_stb")) *flags &= ~READ_STB;
      else if (!strcasecmp(flagstr, "no_read_eoi")) *flags &= ~READ_EOI;
      else if (!strcasecmp(flagstr, "no_term_eoi")) *flags &= ~TERM_EOI;
      else if (!strcasecmp(flagstr, "no_eoi")) *flags &= ~(TERM_EOI | READ_EOI);
      else if (!strcasecmp(flagstr, "no_term"))
	 *flags &= ~(TERM_EOI | TERM_LF | TERM_CR);
      else if (!strcasecmp(flagstr, "read_cr")) {
	  *flags &= ~READ_EOI;
	  *flags |= READ_CR;
      }
      else if (!strcasecmp(flagstr, "read_lf")) {
	  *flags &= ~READ_EOI;
	  *flags |= READ_LF;
      }
      else if (!strcasecmp(flagstr, "no_term_lf")) *flags &= ~TERM_LF;
      else if (!strcasecmp(flagstr, "no_term_cr")) *flags &= ~TERM_CR;
      else if (!strcasecmp(flagstr, "no_spoll")) *flags &= ~HAS_SPOLL;
      else if (!strcasecmp(flagstr, "stb_rdy")) *flags |= STB_RDY;
      else if (!strncasecmp(flagstr, "stb_bit", 7)) {
	 i++;
         result = Tcl_ListObjIndex(interp, lobj, i, &tobj);
         if (result != TCL_OK) return result;
         result = Tcl_GetIntFromObj(interp, tobj, &stbbit);
         if (result != TCL_OK) return result;
	 if (stbbit < 0 || stbbit > 255) {
	    Tcl_SetResult(interp, "STB bit out of range 0-255\n", NULL);
	    return TCL_ERROR;
	 }
	 *flags &= ~READ_STB;
	 *flags |= stbbit;
      }
      else {
	 Tcl_AppendResult(interp, "unknown flag ", flagstr, NULL); 
	 result = TCL_ERROR;
      }
   }
   return TCL_OK;
}

/*--------------------------------------------------------------*/
/*								*/
/*--------------------------------------------------------------*/

int
gpibtcl_openid(ClientData clientData,
	Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   int result, flags;
   char *flagstr, *devname;
   gpib_record *link;

   if (objc < 2) {
      Tcl_WrongNumArgs(interp, objc, objv, "<device> [{flags}]");
      return TCL_ERROR;
   }
   
   devname = Tcl_GetString(objv[1]);
   if (objc == 3) {
      result = parse_flag_list(interp, objv[2], &flags);
      if (result != TCL_OK) return result;
   }
   else
      flags = -1;

   /* Check first if device is already open */
   gpib_find_controller(devname, &link);
   if (link == NULL) {
      result = gpib_init(interp, devname, flags);
      if (result < 0) {
	 if (result == -2)
	    Tcl_SetResult(interp, "No controllers detected", NULL);
	 else if (result == -3)
	    Tcl_SetResult(interp, "No mapping for device", NULL);
 	 return TCL_ERROR;
      }
   }
   return TCL_OK;
}

/*--------------------------------------------------------------*/
/* Close and free the record associated with a GPIB device ID.	*/
/* With no arguments, frees all records and closes all GPIB	*/
/* controllers.							*/
/*--------------------------------------------------------------*/

int
gpibtcl_closeid(ClientData clientData,
	Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   int i, result;
   char *devname;

   if (objc < 2) {
      for (i = 0; i < MAX_CONTROLLERS; i++)
	 gpib_close(interp, i);
   }
   else {
      devname = Tcl_GetString(objv[1]);
      result = gpib_close_link(interp, devname);
      if (result < 0) {
	 Tcl_SetResult(interp, "No such device open", NULL);
	 return TCL_ERROR;
      }
   }
   return TCL_OK;
}

/*--------------------------------------------------------------*/
/*								*/
/*--------------------------------------------------------------*/

int
gpibtcl_remote(ClientData clientData,
	Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *devname;

   if (objc < 2) {
      Tcl_WrongNumArgs(interp, objc, objv, "<device>");
      return TCL_ERROR;
   }
   else {
      devname = Tcl_GetString(objv[1]);
      gpib_address_device(devname, NULL);
   }
   return TCL_OK;
}

/*--------------------------------------------------------------*/
/*								*/
/*--------------------------------------------------------------*/

int
gpibtcl_local(ClientData clientData,
	Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *devname;

   if (objc < 2) {
      Tcl_WrongNumArgs(interp, objc, objv, "<device>");
      return TCL_ERROR;
   }
   else {
      devname = Tcl_GetString(objv[1]);
      gpib_local(devname);
   }
   return TCL_OK;
}

/*--------------------------------------------------------------*/
/*								*/
/*--------------------------------------------------------------*/

int
gpibtcl_clearid(ClientData clientData,
	Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *devname;

   if (objc < 2) {
      Tcl_WrongNumArgs(interp, objc, objv, "<device>");
      return TCL_ERROR;
   }
   else {
      devname = Tcl_GetString(objv[1]);
      gpib_clear(devname);
   }
   return TCL_OK;
}

/*--------------------------------------------------------------*/
/*								*/
/*--------------------------------------------------------------*/

int
gpibtcl_trigger(ClientData clientData,
	Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   char *devname;

   if (objc < 2) {
      Tcl_WrongNumArgs(interp, objc, objv, "<device>");
      return TCL_ERROR;
   }
   else {
      devname = Tcl_GetString(objv[1]); 
      gpib_trigger(devname);
   }
   return TCL_OK;
}

/*--------------------------------------------------------------*/
/*								*/
/*--------------------------------------------------------------*/

int
gpibtcl_status(ClientData clientData,
	Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   int status, result;
   char *devname;

   if (objc < 2) {
      Tcl_WrongNumArgs(interp, objc, objv, "<device>");
      return TCL_ERROR;
   }
   else {
      devname = Tcl_GetString(objv[1]);
      result = gpib_status_byte(devname, &status);
      if (result < 0) {
	 Tcl_SetResult(interp, "No status available\n", NULL);
	 return TCL_ERROR;
      }
      Tcl_SetObjResult(interp, Tcl_NewIntObj(status));
   }
   return TCL_OK;
}

/*--------------------------------------------------------------*/
/*								*/
/*--------------------------------------------------------------*/

int
gpibtcl_binblock(ClientData clientData,
	Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   return TCL_OK;
}

/*--------------------------------------------------------------*/
/*								*/
/*--------------------------------------------------------------*/

int
gpibtcl_write(ClientData clientData,
	Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   int locflags = 0, argstart = 1;
   int status;
   char *sendstr, *rcvstr, *devname;
   long count;
   Tcl_Obj *tobj;

   if (objc < 2) {
      Tcl_WrongNumArgs(interp, objc, objv, "[-nonewline] <device> <command string>");
      return TCL_ERROR;
   }
   else if (objc < 3) {
      Tcl_WrongNumArgs(interp, 1, objv, "[-nonewline] <device> <command string>");
      return TCL_ERROR;
   }
   if (objc == 4) {
      if (!strcmp(Tcl_GetString(objv[1]), "-nonewline")) {
	 locflags = NO_NEWLINE;
	 argstart = 2;
      }
      else {
         Tcl_WrongNumArgs(interp, 1, objv, "[-nonewline] <device> <command string>");
         return TCL_ERROR;
      }
   }

   devname = Tcl_GetString(objv[argstart]);
   sendstr = Tcl_GetString(objv[argstart + 1]);

   status = gpib_send(interp, devname, sendstr, strlen(sendstr), locflags);
   status = gpib_buffered_read(interp, devname, (unsigned char **)(&rcvstr), &count);
   if (count > 0) {
      tobj = Tcl_NewStringObj(rcvstr, count);
      Tcl_SetObjResult(interp, tobj);
   }
   return TCL_OK;
}

/*--------------------------------------------------------------*/
/*								*/
/*--------------------------------------------------------------*/

int
gpibtcl_read(ClientData clientData,
	Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[])
{
   int status;
   char *rcvstr, *devname;
   long count, bcnt;
   Tcl_Obj *tobj;

   if (objc < 2) {
      Tcl_WrongNumArgs(interp, objc, objv, "<device>");
      return TCL_ERROR;
   }
   else
      devname = Tcl_GetString(objv[1]);

   status = gpib_buffered_read(interp, devname, (unsigned char **)(&rcvstr), &count);
   if (count > 0) {
      tobj = Tcl_NewStringObj(rcvstr, count);
      Tcl_SetObjResult(interp, tobj);
   }
   return TCL_OK;
}

