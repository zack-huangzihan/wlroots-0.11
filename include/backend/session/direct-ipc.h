#ifndef BACKEND_SESSION_DIRECT_IPC_H
#define BACKEND_SESSION_DIRECT_IPC_H

#include <sys/types.h>

int direct_ipc_open(int sock, const char *path);
void direct_ipc_setmaster(int sock, int fd);
void direct_ipc_dropmaster(int sock, int fd);
void direct_ipc_finish(int sock, pid_t pid);
int direct_ipc_init(pid_t *pid_out);

#endif
