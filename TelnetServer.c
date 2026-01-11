
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/timer.h"
#include "hardware/clocks.h"
#include "pico/cyw43_arch.h"

#include "tcp_interface.h"
#include "user_datatypes.h"
#include "ff.h"
#include "f_util.h"
#include "hw_config.h"
#include "sd_card.h"
#include "user.h"
#include "telnetserver.h"

static const uint8_t telnet_default_options[] = {
	IAC, TELNET_WILL, TO_ECHO,							// this turns off the echo from the telnet client
	IAC, TELNET_WILL, TO_SUP_GA,						// we will suppress go ahead which also turn on character mode must be this order	
	IAC, TELNET_WILL, TO_NAWS,
	IAC, TELNET_DO, TO_NAWS
};

int init_telnet_server(char *ssid, char *password)
{
// Initialise the Wi-Fi chip
    if (cyw43_arch_init()) {
        printf("Wi-Fi init failed\n");
        return -1;
        }
    //printf("Wi-Fi init succeeded\n");

    // Enable wifi station
    cyw43_arch_enable_sta_mode();

    //printf("Connecting to Wi-Fi...'%s','%s'\n",ssid,password);

    if (cyw43_arch_wifi_connect_timeout_ms(ssid, password, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("failed to connect.\n");
        return -1;
    } else {
    	//    printf("Connected.\n");
        // Read the ip address in a human readable way
        uint8_t *ip_address = (uint8_t*)&(cyw43_state.netif[0].ip_addr.addr);
        printf("IP address %d.%d.%d.%d\n", ip_address[0], ip_address[1], ip_address[2], ip_address[3]);
    }

    // Start the root tcp connection
    user_context_t *user = tcp_server_init();
    if (!user) {
        printf("root TCP init failed\n");
    // } else {
    //    printf("Starting root TCP server\n");
    return -1;
    }

    if (!tcp_server_open(user)) {
        printf("root TCP server open failed\n");
    //   tcp_server_result(user, -1,"init_telnet_server : open failed");
    //} else {
    //    printf("root TCP server open succeeded\n");
    }
    return 0;
}

void telnet_no_echo_request(user_context_t * user) {
    tcp_server_send_msg_len(user,(char *)telnet_default_options,sizeof(telnet_default_options));
    tcp_server_flush(user);
}

void telnet_process_cmd(user_context_t * user)
{
	int resp = -1;

#if TELNET_DEBUG==1
	printf("telnet command recieved IAC %2X %2X\r\n",user->telnet_cmd,user->telnet_opt);
#endif

	switch(user->telnet_cmd) {
	case TELNET_SB:
		switch(user->telnet_opt) {
		case TO_NAWS:
			// we can process NAWS here if we want to know window size
			// but for now we just ignore it

#if TELNET_DEBUG==1
			printf("telnet NAWS subnegotiation recieved %02X %02X%02X%02X%02X%02X%02X\r\n",
					user->telnet_opt,
					user->telnet_sb_buffer[0],
					user->telnet_sb_buffer[1],
					user->telnet_sb_buffer[2],
					user->telnet_sb_buffer[3],
					user->telnet_sb_buffer[4],
					user->telnet_sb_buffer[5]
					);
#endif
			user->term.cols = (user->telnet_sb_buffer[0] << 8) | user->telnet_sb_buffer[1];
			user->term.rows = (user->telnet_sb_buffer[2] << 8) | user->telnet_sb_buffer[3];
			//printf("telnet NAWS cols=%d rows=%d\r\n",user->term.cols, user->term.rows);		
			break;
		default:
			printf("telnet Unknown subnegotiation option %2X\r\n",user->telnet_opt);
		}

		break;

	case TELNET_IP:
	case TELNET_BRK:
		if(user->level == user_basic) {
			user->i_Broken = true;         // break basic program running
		}
		break;

	case TELNET_DO:
		switch (user->telnet_opt) {
		case TO_ECHO:
			resp = TELNET_WILL;       		// we will support echo
			user->telnet_will_echo = true; // set that the server will echo characters
			break;

		case TO_BINARY:
		case TO_SUP_GA:
			resp = TELNET_WILL;
			break;

		default:
			resp = TELNET_WONT;
			break;
		}
		break;

	case TELNET_WILL:
		switch (user->telnet_opt) {
		case TO_SUP_GA:
			// do nothing since we sent DO for these...
			break;

		case TO_NAWS:
			 resp = TELNET_DO;      // we will support NAWS
			 break;

		case TO_TSPEED:
		case TO_RFLOWCTRL:
		case TO_LINEMODE:
		case TO_XDISPLOC:
		case TO_ENV:
		case TO_AUTH:
		case TO_ENCRYPT:
		case TO_NEWENV:
			resp = TELNET_DONT;
			break;

		default:
			resp = TELNET_DO;
		}
		break;

	case TELNET_DONT:
	case TELNET_WONT:
		// ignore these
		break;

	default:
        printf("telnet Unknown command %2X %2X\r\n",user->telnet_cmd, user->telnet_opt);
	}

	if (resp >= 0) {
		uint8_t buf[3] = { IAC, resp, user->telnet_opt };

#if TELNET_DEBUG==1
		printf("telnet sending response %2X %2X %2X\r\n",buf[0],buf[1],buf[2]);
#endif
		tcp_server_send_msg_len(user, buf, 3);
        tcp_server_flush(user);
	}

	user->telnet_cmd_count++;
}

void telnet_process_state(user_context_t *user, int value)
{

    int c = value;


	telnet_state_macine:
    switch(user->telnet_state) {
        case 1:                             	//  IAC seen 
			    if (c == IAC) {                 // escaped 0xff 
				    user->telnet_state = 0;
                } else {                        // Telnet command
			        user->telnet_cmd = c;
			        user->telnet_opt = 0;
            	    if (c == TELNET_WILL || c == TELNET_WONT
					        || c == TELNET_DO || c == TELNET_DONT
					        || c== TELNET_SB) {
						    user->telnet_state = 2;
				    } else {
					    telnet_process_cmd(user);
						user->telnet_state = 0;
				    }
			    }
                return;
		case 2: // Telnet option 
				user->telnet_opt = c;
				if (user->telnet_cmd == TELNET_SB) {
					user->telnet_state = 3;
					user->telnet_sb_index = 0;
				} else {
					telnet_process_cmd(user);
					user->telnet_state = 0;
				}
				return;

		case 3:                     			// Subnegotiation data being recieved
				if (user->telnet_sb_index < TELNET_SB_BUFFER_LEN -1) {
					user->telnet_sb_buffer[user->telnet_sb_index++] = (char)c;
					user->telnet_sb_buffer[user->telnet_sb_index] = '\0';
				} else {
					printf("telnet subnegotiation buffer overflow\r\n");
				}
				if (c == IAC)					// possible end of subnegotiation
					user->telnet_state = 4;
				return;

        case 4:                     			// subnegotiation end recieved, 2 IAC in a row mean escape the previous meaning
				if (c== IAC) {                	// escaped 0xff
					user->telnet_state = 3;
					return;
				}

				if (c == TELNET_SE) {
					if (user->telnet_sb_index < TELNET_SB_BUFFER_LEN -1) {
						user->telnet_sb_buffer[user->telnet_sb_index++] = (char)c;
						user->telnet_sb_buffer[user->telnet_sb_index] = '\0';
					} else {
						printf("telnet subnegotiation buffer overflow\r\n");
					}
					user->telnet_state = 0;      // end of subnegotiation
					telnet_process_cmd(user);
					return;
				} else {
					user->telnet_state = 1;
					goto telnet_state_macine;
				}
                return;

		default:
				user->telnet_state = 0;
    }

    return ;
}

	