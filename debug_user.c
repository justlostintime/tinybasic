#include "user.h"
extern char *status_text[];
user_context_t *debug_session;

user_context_t *init_debug_session(struct tcp_pcb *server) {
    debug_session = create_user_context(server,NULL,false);
    strcpy(debug_session->username,"debug");
    strcpy(debug_session->password,"pwiqzad");
    debug_session->level = user_new_connect;
    debug_session->persist = true;
    return debug_session;
}

void debugger_message(user_context_t * user,char * msg) {
    char buffer[256];
    char longname[64];
    unsigned int TotalUserMemory = 0;
 
    if(debug_session == NULL || debug_session->state.client_pcb == NULL ) return;

    snprintf(longname,sizeof(longname),"%10s",user->username ? user->username : "Wait Login");
    if(user->state.client_pcb == NULL) {
            snprintf(longname+strlen(longname),sizeof(longname)-strlen(longname),"(Local Console)");
    } else {
            snprintf(longname+strlen(longname),sizeof(longname)-strlen(longname),"(%d.%d.%d.%d:%d)",
                ip4_addr1_16(&user->state.client_pcb->remote_ip),
                ip4_addr2_16(&user->state.client_pcb->remote_ip),
                ip4_addr3_16(&user->state.client_pcb->remote_ip),
                ip4_addr4_16(&user->state.client_pcb->remote_ip),
                user->state.client_pcb->remote_port);
    }

    snprintf(buffer,sizeof(buffer),"%-35s, '%-15s' , State(%-12s), %-10s, Last seen:%9llu, %4s, Mem:%6u\n\r",
                        longname,
                        msg,
                        status_text[user->level], 
                        user->logged_in ? "Logged On " : "Logged Off",
                        user->last_active_time ,
                        user->SystemUser ? "Root" : "User",
                        (user->i_Core ? USER_MEMORY_SIZE: 0)+sizeof(user_context_t)
            );

    user_write(debug_session,buffer);
    
}
