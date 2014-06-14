#include "ns.h"

void
timer(envid_t ns_envid, uint32_t initial_to) {
    int r;
    uint32_t stop = sys_time_msec() + initial_to;
   // cprintf("\n guest timer 1\n");

    binaryname = "ns_timer";
 //   cprintf("\n guest timer 2\n");

    while (1) {
        while((r = sys_time_msec()) < stop && r >= 0) {
 //   cprintf("\ntimer 3\n");
            sys_yield();
        }
//    cprintf("\n guest timer 4\n");
        if (r < 0)
            panic("sys_time_msec: %e", r);
 //   cprintf("\n guest timer 5\n");

        ipc_send(ns_envid, NSREQ_TIMER, 0, 0);
  //  cprintf("\n guest timer 6\n");

        while (1) {
 //   cprintf("\n guest timer 7\n");
            uint32_t to, whom;
            to = ipc_recv((int32_t *) &whom, 0, 0);

  //  cprintf("\n guest timer 8\n");
            if (whom != ns_envid) {
                cprintf("NS TIMER: timer thread got IPC message from env %x not NS\n", whom);
  //  cprintf("\n guest timer 9\n");
                continue;
            }
   // cprintf("\n guest timer 10\n");

            stop = sys_time_msec() + to;
   // cprintf("\n guest timer 11\n");
            break;
        }
    }
}
