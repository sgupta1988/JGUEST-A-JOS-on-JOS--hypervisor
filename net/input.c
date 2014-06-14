#include "ns.h"

extern union Nsipc nsipcbuf;
static struct jif_pkt *pkt = (struct jif_pkt*)&nsipcbuf;

	void
static msleep(int msec)
{
	unsigned now = sys_time_msec();
	unsigned end = now + msec;

	if ((int)now < 0 && (int)now > -MAXERROR)
		panic("sys_time_msec: %e", (int)now);
	if (end < now)
		panic("sleep: wrap");

	while (sys_time_msec() < end)
		sys_yield();
}

    void
input(envid_t ns_envid)
{
    binaryname = "ns_input";

	int r;
	if ((r = sys_page_alloc(0, pkt, PTE_P|PTE_U|PTE_W)) < 0)
		panic("sys_page_map: %e", r);

	while(1) {
		while( (r = sys_env_receive_packet(0, (char*)(pkt->jp_data), (size_t*)(&pkt->jp_len)) < 0) ) {
			msleep(10);
		}
		ipc_send(ns_envid, NSREQ_INPUT, pkt, PTE_P|PTE_W|PTE_U);
		msleep(10);
	}
}
