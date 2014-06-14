#ifndef JOS_KERN_SYSCALL_H
#define JOS_KERN_SYSCALL_H
#ifndef JOS_KERNEL
# error "This is a JOS kernel header; user programs should not #include it"
#endif

#include <inc/syscall.h>

int64_t syscall(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5);

int sys_ipc_try_send(envid_t envid, uint32_t value, void *srcva, unsigned perm);
int sys_ipc_recv(void *dstva);
	int
sys_env_transmit_packet(envid_t envid, const char *data, size_t len);
	int
sys_env_receive_packet(envid_t envid, char *data, size_t *len);
int sys_time_msec(void);
#endif /* !JOS_KERN_SYSCALL_H */
