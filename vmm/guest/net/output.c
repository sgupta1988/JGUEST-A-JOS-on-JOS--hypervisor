#include "ns.h"
#include <inc/vmx.h>

extern union Nsipc nsipcbuf;


int sys_host_transmit_packet(envid_t envid, const char *data, size_t len){
	int r,i=0;

	void *pg=(void *)data;
	if((vpml4e[VPML4E(pg)] & PTE_P) && (vpde[VPDPE(pg)] & PTE_P)
			&& (vpd[VPD(pg)] & PTE_P) && (vpt[VPN(pg)] & PTE_P))
	{//cprintf("\nGUEST output ADDRESS IS MAPPED\n");
	}
	else
	{//cprintf("\nGUEST output ADDRESS IS NOT  MAPPED\n");
	}

	pte_t pte = (vpt[((uint64_t) pg/ PGSIZE)]);       
	uint64_t addr = PTE_ADDR(pte);

	int num = VMX_VMCALL_NS_PKT_OUTPUT;
	int a1 = (uint64_t) envid;
	int a2 = (uint64_t) len;
	int a3 = (uint64_t) addr;

	asm volatile("vmcall \n\t"
			: "=a"(r)
			:"a"(num),
			"d"(a3),
			"c"(a2),
			"b"(a1)
			: "cc", "memory");

	return r;

}
	void
output(envid_t ns_envid)
{
	binaryname = "ns_output";

	char *buf=(char *)malloc(sizeof(char)*2048);
	char *tmp;
	int len, r, i;
	for(i=0;i<2048;i++)
		buf[i]='\0';
	while(1) {
		r = sys_ipc_recv(&nsipcbuf);
		if ( (thisenv->env_ipc_from != ns_envid) || (thisenv->env_ipc_value != NSREQ_OUTPUT)) {
			continue;
		}


		tmp=(char *)nsipcbuf.pkt.jp_data;
		len=nsipcbuf.pkt.jp_len;

		memmove(buf,nsipcbuf.pkt.jp_data,nsipcbuf.pkt.jp_len);

		while ((r = sys_host_transmit_packet(0,buf, len)) != 0);
	}

}
