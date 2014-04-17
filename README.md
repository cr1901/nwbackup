This backup program and directory traverser using the mTCP TCP/IP stack for DOS 
machines (http://www.brutman.com/mTCP/). While mTCP and the core application 
proper are written in C++ ("C with classes," essentially. Uses malloc() and 
free()) instead of new and deleted), the directory traverser and backup logic 
code are (should be) written in ANSI C. Perhaps if there is interested,
other more esoteric targets and communication channels (think ZMODEM backups 
over a serial line :D) could be added using wrapper functions which abstract
file sending and receiving. I may just abstract the mTCP code with a C wrapper 
as well, just for the DOS target.

Right now, there are two ways to build this- using SCons or OpenWatcom's wmake.
mTCP requires the OpenWatcom C++ compiler due to its assembler syntax. The
SConstruct provides a means to specify where the mTCP code lives, so a user 
need not place their cloned repo into the mTCP source tree. Open Watcom's wmake 
also works, but requires that a user download the mTCP source tree, make a 
directory under "APPS", and clone this repo into said directory, so that mTCP
code is found.