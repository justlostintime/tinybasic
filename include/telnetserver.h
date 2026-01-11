#ifndef TELNETSERVER_H  
#define TELNETSERVER_H

#define TELNET_DEBUG 0

/*
Telnet commands and options
Last Updated
: 2025-02-27
For information about Telnet connection negotiations, see RFC 2355. Table 1 describes the Telnet commands
  from RFC 854, when the codes and code sequences are preceded by an IAC. For more information about Telnet commands, see RFC 854.

Table 1. Telnet commands from RFC 854
Command             Code    Description
SE	                X'F0'	End of subnegotiation parameters.
NOP	                X'F1'	No operation.
Data Mark	          X'F2'	The data stream portion of a Synch. This should always be accompanied by a TCP Urgent notification.
Break	              X'F3'	NVT character BRK.
Interrupt Process	  X'F4'	The function IP.
Abort output	      X'F5'	The function AO.
Are You There	      X'F6'	The function AYT.
Erase character	    X'F7'	The function EC.
Erase Line	        X'F8'	The function EL.
Go ahead	          X'F9'	The GA signal.
SB	                X'FA'	Indicates that what follows is subnegotiation of the indicated option.
WILL (option code)	X'FB'	Indicates the want to begin performing, or confirmation that you are now performing, the indicated option.
WON'T (option code)	X'FC'	Indicates the refusal to perform, or continue performing, the indicated option.
DO (option code)	  X'FD'	Indicates the request that the other party perform, or confirmation that you are expecting the other party to perform, the indicated option.
DON'T (option code)	X'FE'	Indicates the demand that the other party stop performing, or confirmation that you are no longer expecting the other party to perform, the indicated option.
IAC	                X'FF'	Data byte 255.

Table 2 lists the options available for Telnet commands from RFC 1060. For more information about Telnet protocols, see RFC 1060 and RFC 1011.

Table 2. Telnet command options from RFC 1060
Option Option (Hex) Name

0	      0	        Binary Transmission
1	      1	        Echo
2	      2	        Reconnection
3	      3	        Suppress Go Ahead
4	      4	        Approx Message Size Negotiation
5	      5	        Status
6	      6	        Timing Mark
7	      7	        Remote Controlled Trans and Echo
8	      8	        Output Line Width
9	      9	        Output Page Size
10	    A	        Output Carriage-Return Disposition
11	    B	        Output Horizontal Tab Stops
12	    C   	    Output Horizontal Tab Disposition
13	    D	        Output Formfeed Disposition
14	    E	        Output Vertical Tabstops
15	    F	        Output Vertical Tab Disposition
16	    10	        Output Linefeed Disposition
17	    11	        Extended ASCII
18	    12	        Logout
19	    13	        Byte Macro
20	    14	        Data Entry Terminal
21	    15	        SUPDUP
22	    16	        SUPDUP Output
23	    17	        Send Location
24	    18	        Terminal Type
25	    19	        End of Record
26	    1A	        TACACS User Identification
27	    1B	        Output Marking
28	    1C	        Terminal Location Number
29	    1D	        Telnet 3270 Regime
30	    1E	        X.3 PAD
31	    1F	        Negotiate About Window Size
32	    20	        Terminal Speed
33	    21	        Remote Flow Control
34	    22	        Linemode
35	    23	        X Display Location
255	    FF	        Extended-Options-List


*/
/* Telnet commands */
#define TELNET_SE     240   //X'F0'	End of subnegotiation parameters.
#define TELNET_NOP    241   //X'F1'	No operation.
#define TELNET_DM     242   //X'F2'	The data stream portion of a Synch. This should always be accompanied by a TCP Urgent notification.
#define TELNET_BRK    243   //F3
#define TELNET_IP     244   //F4
#define TELNET_AO     245   //F5
// are you there
#define TELNET_AYT    246   //F6
#define TELNET_EC     247   //F7
#define TELNET_EL     248   //F8
#define TELNET_GA     249   //F9
#define TELNET_SB     250   //FA
#define TELNET_WILL   251   //FB
#define TELNET_WONT   252   //FC
#define TELNET_DO     253   //FD
#define TELNET_DONT   254   //FE
//interpret as command
#define IAC           255   //FF

/* Telnet options */
#define TO_BINARY     0
#define TO_ECHO       1
#define TO_RECONNECT  2
// suppress goahead in full duplex mode
#define TO_SUP_GA     3
#define TO_AMSN       4
#define TO_STATUS     5
#define TO_TM         6
#define TO_RCTE       7
#define TO_OLW        8
#define TO_OPS        9
#define TO_OCRD       10
#define TO_OHTS       11
#define TO_OHTD       12
#define TO_OFD        13
#define TO_OVTS       14
#define TO_OVTD       15
#define TO_OLD        16
#define TO_EASCII     17
#define TO_LOGOUT     18
#define TO_BYTEMACRO  19
#define TO_DET        20
#define TO_SUPDUP     21
#define TO_SUPDUPOUT  22
#define TO_SENDLOC    23
#define TO_TTYPE      24
#define TO_EOR        25
#define TO_TACACSUID  26
#define TO_OMARK      27
#define TO_TLN        28
#define TO_3270REGIME 29
#define TO_X3PAD      30
#define TO_NAWS       31
#define TO_TSPEED     32
#define TO_RFLOWCTRL  33
#define TO_LINEMODE   34
#define TO_XDISPLOC   35
#define TO_ENV        36
#define TO_AUTH       37
#define TO_ENCRYPT    38
#define TO_NEWENV     39


void telnet_no_echo_request(user_context_t * user);
int init_telnet_server(char *ssid, char *password);
void telnet_process_state(user_context_t *user, int value);

#endif