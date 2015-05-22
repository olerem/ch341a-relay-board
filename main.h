/* 
 * File:   main.h
 * Author: oetelaar
 *
 * Created on December 22, 2013, 12:12 AM
 */

#ifndef MAIN_H
#define	MAIN_H

#ifdef	__cplusplus
extern "C" {
#endif

    const char _helptext[] = "\nName : switch_relay - switch relays on Abacom or Elomax board on or off"
            "\nCan be run once, to set some relays or"
            "\ncan run as a directory monitor using inotify, will switch relays when files are created or removed"
            "\n"
            "\nswitch_relay [options] [relay_number] [relay_number]"
            "\noptions:"
            "\n -s : use syslog for logging instead of stderr"
            "\n -d : keep running (as a daemon) does not fork (use something like supervisord)"
            "\n -i <directory_name> : use event listing on this directory instead of /tmp"
            "\n -h : show help text"
            "\n -m <0|1> : use Abacom=0 (default) or Elmax=1 protocol and device"
            "\n -z loglevel : set loglevel (default=7) valid levels : ERR = 1, WARN =2, NOTICE=4, INFO=8, DEBUG=16 OR together"
            "\n"
            "\n"
            "\nUsage example:"
            "\n $ switch_relay  : switch all relays off"
            "\n $ switch_relay 4 : switch all relays off, but switch relay 4 on"
            "\n $ switch_relay -s -d -z 31 : use syslog, keep running, use maximum logging"
            "\n"
            "\nWhen using (-d) the program will monitor /tmp/ for creation or removal of files"
            "\n /tmp/D_OUT_1 /tmp/D_OUT_2 .. /tmp_D_OUT_8"
            "\n create a file with that name and the output will be active (on) remove the file and the output will deactivate (off)"
            "\n"
            "\nexample :"
            "\n $ touch /tmp/D_OUT_1 : will active relay no 1"
            "\n $ rm /tmp/D_OUT_1    : will switch relay off again"
            "\n\n";

#ifdef	__cplusplus
}
#endif

#endif	/* MAIN_H */

