// User-level IPC library routines

#include <inc/lib.h>
#ifdef VMM_GUEST
#include <inc/vmx.h>
#endif

// Receive a value via IPC and return it.
// If 'pg' is nonnull, then any page sent by the sender will be mapped at
//	that address.
// If 'from_env_store' is nonnull, then store the IPC sender's envid in
//	*from_env_store.
// If 'perm_store' is nonnull, then store the IPC sender's page permission
//	in *perm_store (this is nonzero iff a page was successfully
//	transferred to 'pg').
// If the system call fails, then store 0 in *fromenv and *perm (if
//	they're nonnull) and return the error.
// Otherwise, return the value sent by the sender
//
// Hint:
//   Use 'thisenv' to discover the value and who sent it.
//   If 'pg' is null, pass sys_ipc_recv a value that it will understand
//   as meaning "no page".  (Zero is not the right value, since that's
//   a perfectly valid place to map a page.)
    int32_t
ipc_recv(envid_t *from_env_store, void *pg, int *perm_store)
{
	int tmp=0;
	//	cprintf("\n tmp=[%x],from_env_store=[%x],",&tmp,&from_env_store);      
	// LAB 4: Your code here.
	//If pg is NULL, then set pg to something that sys_ipc_rev can decode
//	cprintf("\n ipc_recv 1 \n");
	if (pg == NULL)
		pg=(void*)UTOP;

//	cprintf("\n ipc_recv 2 \n");
	//Try receiving value
	int r = sys_ipc_recv(pg);
//	cprintf("\n ipc_recv 3 \n");
	if (r < 0) {
		if (from_env_store)
			*from_env_store = 0;
		if (perm_store)
			*perm_store = 0;
//		cprintf("\n ipc_recv 4 \n");
		return r;
	}
	else {
		if (from_env_store != NULL)
			*from_env_store = thisenv->env_ipc_from;
		if (thisenv->env_ipc_dstva && perm_store != NULL)
			*perm_store = thisenv->env_ipc_perm;

//		cprintf("\n ipc_recv 5 \n");
		tmp= thisenv->env_ipc_value; //return the received value
//		cprintf("\n ipc_recv 6 \n");
	//	cprintf("\n tmp=*%x*",&tmp);      
		return tmp; //return the received value
		// return thisenv->env_ipc_value; //return the received value
	}
	panic("ipc_recv not implemented");
	return 0;
}

// Send 'val' (and 'pg' with 'perm', if 'pg' is nonnull) to 'toenv'.
// This function keeps trying until it succeeds.
// It should panic() on any error other than -E_IPC_NOT_RECV.
//
// Hint:
//   Use sys_yield() to be CPU-friendly.
//   If 'pg' is null, pass sys_ipc_recv a value that it will understand
//   as meaning "no page".  (Zero is not the right value.)
    void
ipc_send(envid_t to_env, uint32_t val, void *pg, int perm)
{
    // LAB 4: Your code here.
    //If pg is NULL, then set pg to something that sys_ipc_rev can decode
        if (pg == NULL)
                pg=(void*)UTOP;

        //Loop until succeeded/
        while (1) {
                //Try sending the value to dst

                
                int r = sys_ipc_try_send(to_env, val, pg, perm);

                if (r == 0)
                        break;
                if (r < 0 && r != -E_IPC_NOT_RECV) //Receiver is not ready to receive.
                        panic("error in sys_ipc_try_send %e\n", r);
                else if (r == -E_IPC_NOT_RECV){
			//cprintf("Calling sys_yield");
                        sys_yield();

        	}
	}
}

#ifdef VMM_GUEST

// Access to host IPC interface through VMCALL.
// Should behave similarly to ipc_recv, except replacing the system call with a vmcall.
int32_t
ipc_host_recv(void *pg) {
    // LAB 4: Your code here.
    //If pg is NULL, then set pg to something that sys_ipc_rev can decode
        if (pg == NULL)
                pg=(void*)UTOP;

        //Try receiving value
                int num = VMX_VMCALL_IPCRECV;
        int r;


      pte_t pte = 0;

       uint64_t addr = 0;

        if((vpml4e[VPML4E(pg)] & PTE_P) && (vpde[VPDPE(pg)] & PTE_P)
                        && (vpd[VPD(pg)] & PTE_P) && (vpt[VPN(pg)] & PTE_P))
       {}
	else
        {
           if((r = sys_page_alloc(0, pg , PTE_P|PTE_W|PTE_U|PTE_SHARE)) < 0)
		return r;

	 }

          pte = (vpt[((uint64_t) pg/ PGSIZE)]);     
          addr = PTE_ADDR(pte);

         int a1 = (uint64_t) addr ;

        
        asm volatile("vmcall \n\t"
                     : "=a"(r)
                     :"a"(num),
                     "d"(a1)
            : "cc", "memory");

  
     return r;
}

// Access to host IPC interface through VMCALL.
// Should behave similarly to ipc_send, except replacing the system call with a vmcall.
void
ipc_host_send(envid_t to_env, uint32_t val, void *pg, int perm)
{
    // LAB 8: Your code here.
    if (pg == NULL)
        pg=(void*)UTOP;

     //volatile struct Env *env;

   // env = &envs[ENVX(to_env)];

    pte_t pte = (vpt[((uint64_t) pg/ PGSIZE)]);       
    uint64_t addr = PTE_ADDR(pte );


    while (1) {
        int num = VMX_VMCALL_IPCSEND;
        int r;
        int a1 = (uint64_t) to_env;
        int a2 = (uint64_t) val;
        int a3 = (uint64_t) addr;
        int a4 = (uint64_t) perm;
        
        asm volatile("vmcall \n\t"
                     : "=a"(r)
                     :"a"(num),
                     "d"(a3),
                     "c"(a2),
                     "b"(a1),
                     "D" (a4)
            : "cc", "memory");

        if (r == 0)
	  {
	     	// env->env_ipc_recving = 0;
              // env->env_ipc_value = val;
               //env->env_ipc_from = ipc_find_env(ENV_TYPE_FS);
               //env->env_ipc_perm = perm;

		//int r = sys_ipc_try_send(to_env, val, pg, perm);

               break;
         }
        if (r < 0 && r != -E_IPC_NOT_RECV)
            panic("error in vmcall_ipc_try_send %e\n", r);
        else if (r == -E_IPC_NOT_RECV){
            sys_yield();
        }
    }

    //panic("ipc_send not implemented in VM guest");
}

#endif

// Find the first environment of the given type.  We'll use this to
// find special environments.
// Returns 0 if no such environment exists.
    envid_t
ipc_find_env(enum EnvType type)
{
    int i;
    for (i = 0; i < NENV; i++) {
        if (envs[i].env_type == type)
            return envs[i].env_id;
    }
    return 0;
}
