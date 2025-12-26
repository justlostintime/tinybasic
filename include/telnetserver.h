#ifndef TELNETSERVER_H  
#define TELNETSERVER_H
// Telnet codes
   #define VLSUP     ((uint8_t)0)
   #define VLREQ     ((uint8_t)1)
   #define LOC       ((uint8_t)23)
   #define TTYPE     ((uint8_t)24)
   #define NAWS      ((uint8_t)31)
   #define TSPEED    ((uint8_t)32)
   #define LFLOW     ((uint8_t)33)
   #define LINEMODE  ((uint8_t)34)
   #define XDISPLOC  ((uint8_t)35)
   #define NEW_ENVIRON ((uint8_t)39)
   #define BINARY    ((uint8_t)0)
   #define ECHO      ((uint8_t)1)
   #define SUP_GA    ((uint8_t)3)
   #define SE        ((uint8_t)240)
   #define DM        ((uint8_t)242)
   #define BRK       ((uint8_t)243)
   #define AYT       ((uint8_t)246)
   #define SB        ((uint8_t)250)
   #define WILL      ((uint8_t)251)
   #define WONT      ((uint8_t)252)
   #define DO        ((uint8_t)253)
   #define DONT      ((uint8_t)254)
   #define IAC       ((uint8_t)255)

void Client_echo_request(user_context_t * user);

#endif