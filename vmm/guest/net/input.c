#include "ns.h"
#include <inc/vmx.h>

extern union Nsipc nsipcbuf;

static struct jif_pkt *pkt = (struct jif_pkt*)&nsipcbuf;

int
sys_host_receive_packet(envid_t envid, char *data, size_t *len){
	int r,i=0;
	void *pg=(void *)data;
	if((vpml4e[VPML4E(pg)] & PTE_P) && (vpde[VPDPE(pg)] & PTE_P)
			&& (vpd[VPD(pg)] & PTE_P) && (vpt[VPN(pg)] & PTE_P))
	{//cprintf("\nGUEST input ADDRESS IS MAPPED\n");
	}
	else
	{//cprintf("\nGUEST inpur ADDRESS IS NOT  MAPPED\n");
	}

	pte_t pte = (vpt[((uint64_t) pg/ PGSIZE)]);       
	uint64_t addr = PTE_ADDR(pte);

	int num = VMX_VMCALL_NS_PKT_INPUT;
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
	*len=r;
	return r;
}

	void
input(envid_t ns_envid)
{
	binaryname = "ns_input";
	char *buf=(char *)malloc(sizeof(char)*2048);
	int len, r, i;
	for(i=0;i<2048;i++)
		buf[i]='\0';
	while(1) {
		while ((r = sys_host_receive_packet(0,(char *)buf,(size_t *)&len)) < 0)
			sys_yield();

		while ((r = sys_page_alloc(0, &nsipcbuf, PTE_U | PTE_P | PTE_W)) < 0);

		nsipcbuf.pkt.jp_len = len;
		memmove(nsipcbuf.pkt.jp_data, buf, len);

		while ((r = sys_ipc_try_send(ns_envid, NSREQ_INPUT,&nsipcbuf, PTE_P | PTE_W | PTE_U)) < 0);
	}
}
