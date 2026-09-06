/* Services the driver provides to every peer instance. */
#ifndef NETRIG_API_H
#define NETRIG_API_H

#include <stdarg.h>

extern unsigned int gRigCurrentIp;	/* net_bus.c: whose code is running */
extern int          gRigDropAll;
extern int          gRigDropFromIp;

void rig_vlog(int peer, const char* fmt, va_list ap);
void rig_note_menu(int peer, int menuId);
void rig_note_load(int peer, int sceneId);
void rig_note_reload(int peer);
void rig_set_paused(int peer, int paused);
int  rig_is_paused(int peer);
unsigned int rig_ip_of(int peer);
void rig_gk_send(int peer, const void* data, int len, int reliable);
void rig_note_notice(int peer, const char* text);
void rig_note_death(int peer, int seat);		/* v2.0.9: P_ApplyDeath ran for `seat` on `peer` */

void rig_bus_reset(void);

#endif
