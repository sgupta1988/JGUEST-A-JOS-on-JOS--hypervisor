#include "ns.h"

extern union Nsipc nsipcbuf;

    void
output(envid_t ns_envid)
{
    binaryname = "ns_output";

    // LAB 6: Your code here:
    // 	- read a packet from the network server
    //	- send the packet to the device driver
	uint32_t req, whom;
	int perm, r;
	void *pg;

	while (1)
	{
		perm = 0;
		req = ipc_recv((int32_t *) &whom, &nsipcbuf, &perm);

		// All requests must contain an argument page
		if (!(perm & PTE_P)) {
			cprintf("Invalid request from %08x: no argument page\n",
					whom);
			continue; // just leave it hanging...
		}

		pg = NULL;
		if (req == NSREQ_OUTPUT) {
			struct jif_pkt *pk = (struct jif_pkt *)&nsipcbuf;

			if ((r = sys_env_transmit_packet(0, pk->jp_data, pk->jp_len)) < 0)
				cprintf("error: sys_env_transmit_packet failed in output()\n");
		} else {
			cprintf("Invalid request code %d from %08x\n", whom, req);
			r = -E_INVAL;
		}
	}
}
