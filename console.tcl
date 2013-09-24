slave alias ftdi::consoledown wm withdraw .
slave alias ftdi::consoleup wm deiconify .
slave alias ftdi::consoleontop raise .

wm protocol . WM_DELETE_WINDOW {tkcon slave slave ftdi::lowerconsole}
