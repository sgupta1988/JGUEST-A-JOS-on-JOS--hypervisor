/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>

#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/syscall.h>
#include <kern/console.h>
#include <kern/sched.h>
#include <kern/time.h>
#include <vmm/ept.h>
#include <kern/e1000.h>
#define debug 0

// Print a string to the system console.
// The string is exactly 'len' characters long.
// Destroys the environment on memory errors.
static void
sys_cputs(const char *s, size_t len)
{
    // Check that the user has permission to read memory [s, s+len).
    // Destroy the environment if not.

    // LAB 3: Your code here.
    user_mem_assert(curenv, s, len, PTE_U | PTE_P);
    // Print the string supplied by the user.
    cprintf("%.*s", len, s);
}

// Read a character from the system console without blocking.
// Returns the character, or 0 if there is no input waiting.
    static int
sys_cgetc(void)
{
    return cons_getc();
}

// Returns the current environment's envid.
    static envid_t
sys_getenvid(void)
{
    return curenv->env_id;
    
}

// Destroy a given environment (possibly the currently running environment).
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
    static int
sys_env_destroy(envid_t envid)
{
    int r;
    struct Env *e;

    if ((r = envid2env(envid, &e, 1)) < 0)
        return r;
	if (e == curenv)
		cprintf("[%08x] exiting gracefully\n", curenv->env_id);
	else
		cprintf("[%08x] destroying %08x\n", curenv->env_id, e->env_id);
    env_destroy(e);
    return 0;
}

// Deschedule current environment and pick a different one to run.
    static void
sys_yield(void)
{
    sched_yield();
}

// Allocate a new environment.
// Returns envid of new environment, or < 0 on error.  Errors are:
//	-E_NO_FREE_ENV if no free environment is available.
//	-E_NO_MEM on memory exhaustion.
    static envid_t
sys_exofork(void)
{
    // Create the new environment with env_alloc(), from kern/env.c.
    // It should be left as env_alloc created it, except that
    // status is set to ENV_NOT_RUNNABLE, and the register set is copied
    // from the current environment -- but tweaked so sys_exofork
    // will appear to return 0.

	struct Env *env;
	int err = env_alloc(&env, curenv->env_id);
	if (err < 0)
		return err;
	else if (!err) {
		env->env_status = ENV_NOT_RUNNABLE;
		memcpy(&(env->env_tf), &(curenv->env_tf), sizeof(struct Trapframe));

		// Parent is going to set child's status to RUNNING at some point of
		// time. On this occurance, child will expect to have the error code
		// in ax register. Thus, directly put value 0 in ax, so that child
		// reads it whe needed.
		env->env_tf.tf_regs.reg_rax = 0;

		return env->env_id; // Parent should return child's envid
	}
    panic("sys_exofork not implemented");
}

// Set envid's env_status to status, which must be ENV_RUNNABLE
// or ENV_NOT_RUNNABLE.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if status is not a valid status for an environment.
    static int
sys_env_set_status(envid_t envid, int status)
{
    // Hint: Use the 'envid2env' function from kern/env.c to translate an
    // envid to a struct Env.
    // You should set envid2env's third argument to 1, which will
    // check whether the current environment has permission to set
    // envid's status.

	if (status != ENV_RUNNABLE && status != ENV_NOT_RUNNABLE)
		return -E_INVAL;
	struct Env *env;
	int err = envid2env(envid, &env, 1);
	if (err < 0)
		return err;
	else if (err == 0) {
		env->env_status = status;
		return 0;
	}
    panic("sys_env_set_status not implemented");
}

// Set envid's trap frame to 'tf'.
// tf is modified to make sure that user environments always run at code
// protection level 3 (CPL 3) with interrupts enabled.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
    static int
sys_env_set_trapframe(envid_t envid, struct Trapframe *tf)
{
    // LAB 5: Your code here.
    // Remember to check whether the user has supplied us with a good
    // address!
    	int ret;
    	struct Env *e;
    	ret = envid2env(envid, &e, 1);
    	if (ret < 0)
    		return ret;
    	e->env_tf = *tf;
    	e->env_tf.tf_cs |= 3;
    	e->env_tf.tf_eflags |= FL_IF;
    	return 0;
}
//Transmit a packet to the network.
	int
sys_env_transmit_packet(envid_t envid, const char *data, size_t len)
{
	struct Env *env;
	int err = envid2env(envid, &env, 1);
	if (err < 0)
		return err;
	else if (err == 0) {
		user_mem_assert(curenv, (void*)data, len, PTE_P | PTE_U);
		e1000_transmit_packet(data, len);
		return 0;
	}
	panic("sys_env_set_trapframe not implemented");
}

// Receive a packet from the network.
	int
sys_env_receive_packet(envid_t envid, char *data, size_t *len)
{
	struct Env *env;
	int err = envid2env(envid, &env, 1);
	if (err < 0)
		return err;
	else if (err == 0) {
		user_mem_assert(curenv, (void*)data, 1, PTE_P | PTE_U);
		return e1000_receive_packet(data, len);
	}
	panic("sys_env_set_trapframe not implemented");
}

// Set the page fault upcall for 'envid' by modifying the corresponding struct
// Env's 'env_pgfault_upcall' field.  When 'envid' causes a page fault, the
// kernel will push a fault record onto the exception stack, then branch to
// 'func'.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
    static int
sys_env_set_pgfault_upcall(envid_t envid, void *func)
{
	struct Env *env;
        int err = envid2env(envid, &env, 1);
        if (err < 0)
                return err;
        else if (err == 0) {
		user_mem_assert(env, func, sizeof(func), PTE_P|PTE_U);
		env->env_pgfault_upcall = func;
		return 0;
	}
    panic("sys_env_set_pgfault_upcall not implemented");
}

// Allocate a page of memory and map it at 'va' with permission
// 'perm' in the address space of 'envid'.
// The page's contents are set to 0.
// If a page is already mapped at 'va', that page is unmapped as a
// side effect.
//
// perm -- PTE_U | PTE_P must be set, PTE_AVAIL | PTE_W may or may not be set,
//         but no other bits may be set.  See PTE_SYSCALL in inc/mmu.h.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
//	-E_INVAL if perm is inappropriate (see above).
//	-E_NO_MEM if there's no memory to allocate the new page,
//		or to allocate any necessary page tables.
    static int
sys_page_alloc(envid_t envid, void *va, int perm)
{
    // Hint: This function is a wrapper around page_alloc() and
    //   page_insert() from kern/pmap.c.
    //   Most of the new code you write should be to check the
    //   parameters for correctness.
    //   If page_insert() fails, remember to free the page you
    //   allocated!

	if ((uint64_t)va >= UTOP || va != ROUNDUP(va, PGSIZE))
		return -E_INVAL;
	if (!(perm & PTE_U) || !(perm & PTE_P))
		return -E_INVAL;
	struct Page *p = NULL;
        if (!(p = page_alloc(ALLOC_ZERO)))
                return -E_NO_MEM;
     	struct Env *env;
        int err = envid2env(envid, &env, 1);
        if (err < 0)
                return err;
        else if (err == 0) {
		page_remove(env->env_pml4e, va);		
		if (page_insert(env->env_pml4e, p, va, perm) < 0) {
			page_free(p);
			return -E_NO_MEM;
		}
                return 0;
        }
    panic("sys_page_alloc not implemented");
}

// Map the page of memory at 'srcva' in srcenvid's address space
// at 'dstva' in dstenvid's address space with permission 'perm'.
// Perm has the same restrictions as in sys_page_alloc, except
// that it also must not grant write access to a read-only
// page.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if srcenvid and/or dstenvid doesn't currently exist,
//		or the caller doesn't have permission to change one of them.
//	-E_INVAL if srcva >= UTOP or srcva is not page-aligned,
//		or dstva >= UTOP or dstva is not page-aligned.
//	-E_INVAL is srcva is not mapped in srcenvid's address space.
//	-E_INVAL if perm is inappropriate (see sys_page_alloc).
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in srcenvid's
//		address space.
//	-E_NO_MEM if there's no memory to allocate any necessary page tables.
    static int
sys_page_map(envid_t srcenvid, void *srcva,
        envid_t dstenvid, void *dstva, int perm)
{
    // Hint: This function is a wrapper around page_lookup() and
    //   page_insert() from kern/pmap.c.
    //   Again, most of the new code you write should be to check the
    //   parameters for correctness.
    //   Use the third argument to page_lookup() to
    //   check the current permissions on the page.

        if ((uint64_t)srcva >= UTOP || (srcva != ROUNDUP(srcva, PGSIZE) && srcva != ROUNDDOWN(srcva, PGSIZE)) ||
	    (uint64_t)dstva >= UTOP || (dstva != ROUNDUP(dstva, PGSIZE) && dstva != ROUNDDOWN(dstva, PGSIZE)))
                return -E_INVAL;

	if (!(perm & PTE_U) || !(perm & PTE_P))
                return -E_INVAL;

        struct Env *srcenv, *dstenv;
        int srcerr = envid2env(srcenvid, &srcenv, 1);
        int dsterr = envid2env(dstenvid, &dstenv, 1);
        if (srcerr < 0)
                return srcerr;
	else if (dsterr < 0)
		return dsterr;

        else if (srcerr == 0 && dsterr==0) {
       		pte_t *pte;
        	struct Page *pp = page_lookup(srcenv->env_pml4e, srcva, &pte);

        	if (pp == NULL)
                	return -E_INVAL;
	        if (!(*pte & PTE_U))
                	return -E_INVAL;

                if (page_insert(dstenv->env_pml4e, pp, dstva, perm) < 0)
                        return -E_NO_MEM;
		else
			return 0;
	}
    panic("sys_page_map not implemented");
}

// Unmap the page of memory at 'va' in the address space of 'envid'.
// If no page is mapped, the function silently succeeds.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
    static int
sys_page_unmap(envid_t envid, void *va)
{
    // Hint: This function is a wrapper around page_remove().

	if ((uint64_t)va >= UTOP || va != ROUNDUP(va, PGSIZE))
                return -E_INVAL;

	struct Env *env;
        int err = envid2env(envid, &env, 1);
        if (err < 0)
                return err;
        else if (err == 0) {
		page_remove(env->env_pml4e, va);
		return 0;
	}
    panic("sys_page_unmap not implemented");
}

// Try to send 'value' to the target env 'envid'.
// If srcva < UTOP, then also send page currently mapped at 'srcva',
// so that receiver gets a duplicate mapping of the same page.
//
// The send fails with a return value of -E_IPC_NOT_RECV if the
// target is not blocked, waiting for an IPC.
//
// The send also can fail for the other reasons listed below.
//
// Otherwise, the send succeeds, and the target's ipc fields are
// updated as follows:
//    env_ipc_recving is set to 0 to block future sends;
//    env_ipc_from is set to the sending envid;
//    env_ipc_value is set to the 'value' parameter;
//    env_ipc_perm is set to 'perm' if a page was transferred, 0 otherwise.
// The target environment is marked runnable again, returning 0
// from the paused sys_ipc_recv system call.  (Hint: does the
// sys_ipc_recv function ever actually return?)
//
// If the sender wants to send a page but the receiver isn't asking for one,
// then no page mapping is transferred, but no error occurs.
// The ipc only happens when no errors occur.
//
// Returns 0 on success, < 0 on error.
// Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist.
//		(No need to check permissions.)
//	-E_IPC_NOT_RECV if envid is not currently blocked in sys_ipc_recv,
//		or another environment managed to send first.
//	-E_INVAL if srcva < UTOP but srcva is not page-aligned.
//	-E_INVAL if srcva < UTOP and perm is inappropriate
//		(see sys_page_alloc).
//	-E_INVAL if srcva < UTOP but srcva is not mapped in the caller's
//		address space.
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in the
//		current environment's address space.
//	-E_NO_MEM if there's not enough memory to map srcva in envid's
//		address space.
	int
sys_ipc_try_send(envid_t envid, uint32_t value, void *srcva, unsigned perm)
{
	struct Env *env;
	pte_t *pte;

	if (envid2env(envid, &env, 0) < 0) {
		cprintf("sys_ipc_try_send failed: Bad env\n");
		return -E_BAD_ENV;
	}

	if ((env->env_status != ENV_NOT_RUNNABLE) || (env->env_ipc_recving != 1))
		return -E_IPC_NOT_RECV;

	if ((uint64_t)srcva < UTOP){
		//Check if the page in alligned.
		if ((uint64_t)srcva % PGSIZE != 0) {
			cprintf("sys_ipc_try_send failed: Page is not alligned\n");
			return -E_INVAL;
		}

		//Check if srcva is mapped to in caller's address space.
		if(curenv->env_type == ENV_TYPE_GUEST){
			int output=	ept_lookup_gpa(curenv->env_pml4e, srcva,0 ,&pte);
		//	cprintf("sys_ipc_try_send :: ept_lookup_gpa=[%d] :: -E_INVAL[%d] ::-E_NO_ENT[%d],-E_NO_MEM=[%d] *pte=[%x]",output,-E_INVAL,-E_NO_ENT,-E_NO_MEM,*pte);
			if(output < 0 || pte==NULL){
				cprintf("\n sys_ipc_try_send :: ept_lookup_gpa :: Failed :: output=[%d]\n",output);
				return -E_INVAL;
			}
		}
		else{
			pte = pml4e_walk(curenv->env_pml4e, srcva, 0);
		}
		if (!pte || !((*pte) & PTE_P)) {
			cprintf("\nsys_ipc_try_send failed: Page is not mapped to srcva\n");
			return -E_INVAL;
		}

		//Check for permissions
		if (!(perm & PTE_U) || !(perm & PTE_P)) {
			cprintf("\nsys_try_ipc_send failed: Invalid permissions\n");
			return -E_INVAL;
		}
		if (perm &(!PTE_USER)) {
			cprintf("sys_try_ipc_send failed: Invalid permissions\n");
			return - E_INVAL;
		}
		if (!((*pte) & PTE_W) && (perm & PTE_W)) {
			cprintf("\nsys_try_ipc_send failed: Writing on read-only page\n");
			return -E_INVAL; 
		}

	}

	env->env_ipc_recving = 0;
	env->env_ipc_value = value;
	env->env_ipc_from = curenv->env_id;
	env->env_ipc_perm = 0;

	if ((uint64_t)srcva < UTOP) {
		pte_t *pite;
		struct Page *pp;
		if(curenv->env_type == ENV_TYPE_GUEST){
			uint64_t *phy_page_gpa;
			int output = ept_lookup_gpa(curenv->env_pml4e, srcva, 1,&pte);
		//	cprintf("sys_ipc_try_send :: ept_lookup_gpa=[%d] :: -E_INVAL[%d] ::-E_NO_ENT[%d],-E_NO_MEM=[%d],*pte=[%x]",output,-E_INVAL,-E_NO_ENT,-E_NO_MEM,*pte);
			if(!epte_present(*pte)) {
				cprintf("\nsys_try_ipc_send: Page not found in VMGUEST\n");
				return -1;
			}
			phy_page_gpa=(uint64_t *)epte_addr(*pte);
			pp=pa2page((uint64_t)phy_page_gpa);
			int val= ept_page_insert(env->env_pml4e, pp, env->env_ipc_dstva, __EPTE_FULL);
			if(val<0)
				return val;
		}
		else	{   
			pp = page_lookup(curenv->env_pml4e, srcva, &pte);
			if (!pp) {
				cprintf("\nsys_try_ipc_send: Page not found\n");
				return -1;
			}

			if (page_insert(env->env_pml4e, pp, env->env_ipc_dstva, perm) < 0)
				return -E_NO_MEM;
		}
		env->env_ipc_perm = perm;
	}

	env->env_status = ENV_RUNNABLE;
	return 0;
	panic("sys_ipc_try_send not implemented");
}

// Block until a value is ready.  Record that you want to receive
// using the env_ipc_recving and env_ipc_dstva fields of struct Env,
// mark yourself not runnable, and then give up the CPU.
//
// If 'dstva' is < UTOP, then you are willing to receive a page of data.
// 'dstva' is the virtual address at which the sent page should be mapped.
//
// This function only returns on error, but the system call will eventually
// return 0 on success.
// Return < 0 on error.  Errors are:
//	-E_INVAL if dstva < UTOP but dstva is not page-aligned.
    int
sys_ipc_recv(void *dstva)
{
        //cprintf("\nsys_ipc_recv 1\n");
	if (!curenv)
		return -E_INVAL;
        //cprintf("\nsys_ipc_recv 2\n");
	if ((uint64_t)dstva > UTOP || (ROUNDDOWN(dstva,PGSIZE)!=dstva && ROUNDUP(dstva,PGSIZE)!=dstva)) {
		cprintf("error dstva is not valid dstva=%x\n",dstva);
		return -E_INVAL;
	}
        //cprintf("\nsys_ipc_recv 3\n");

	//Set all required variables.
	curenv->env_tf.tf_regs.reg_rax = 0; //We need this to inform lib/syscall.c that everything went fine.
					    //Note that this function does not return. Thus, set rax so that
					    //if anyone schedules this env, it returns will rax=0 in lib/syscall.
    //we get a gpa from guest through syscall , we have to allocate a page to that gpa and set the permisions
        //cprintf("\nsys_ipc_recv 4\n");
/*	if(curenv->env_type == ENV_TYPE_GUEST){
		void *hva;
		ept_gpa2hva(curenv->env_pml4e,dstva,&hva);
			curenv->env_ipc_dstva = hva;
	}
	else*/
		curenv->env_ipc_dstva = dstva;

        //cprintf("\nsys_ipc_recv 5\n");
	curenv->env_ipc_perm = 0;
        curenv->env_ipc_from = 0;
        curenv->env_ipc_recving = 1; //Receiver is ready to listen
        curenv->env_status = ENV_NOT_RUNNABLE; //Block the execution of current env.

        //cprintf("\nsys_ipc_recv 6\n");
	curenv->env_ipc_perm = 0;
	sched_yield(); //Give up the cpu. Don't return, instead env_run some other env.
    //panic("sys_ipc_recv not implemented");
    return 0;
}

// Return the current time.
int sys_time_msec(void)
{
     int r;

	r= time_msec();
 //      cprintf("\n host time = [%d] \n",r);
	return r;
	//return time_msec();
    panic("sys_time_msec not implemented");
}

// Maps a page from the evnironment corresponding to envid into the guest vm 
// environments phys addr space. 
//
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if srcenvid and/or guest doesn't currently exist,
//		or the caller doesn't have permission to change one of them.
//	-E_INVAL if srcva >= UTOP or srcva is not page-aligned,
//		or guest_pa >= guest physical size or guest_pa is not page-aligned.
//	-E_INVAL is srcva is not mapped in srcenvid's address space.
//	-E_INVAL if perm is inappropriate 
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in srcenvid's
//		address space.
//	-E_NO_MEM if there's no memory to allocate any necessary page tables. 
//
// Hint: The TA solution uses ept_map_hva2gpa().  A guest environment uses 
//       env_pml4e to store the root of the extended page tables.
// 

static int
sys_ept_map(envid_t srcenvid, void *srcva,
	    envid_t guestvid, void* guestpa, int perm)
{

    //    cprintf("guestpa %x\n", guestpa);
     //   cprintf("srcva %x\n", srcva );
     //   cprintf("UTOP %x\n", UTOP);
	uint64_t mask = 0xfff;
    //    cprintf("1\n");
	if ((uint64_t)srcva >= UTOP || (srcva != ROUNDUP(srcva, PGSIZE) && srcva != ROUNDDOWN(srcva, PGSIZE)))
		return -E_INVAL;


        if ((uint64_t)srcva >= UTOP || (srcva != ROUNDUP(srcva, PGSIZE) && srcva != ROUNDDOWN(srcva, PGSIZE)) ||
            (uint64_t)guestpa >= UTOP || (guestpa != ROUNDUP(guestpa, PGSIZE) && guestpa != ROUNDDOWN(guestpa, PGSIZE)))
                return -E_INVAL;



  //      cprintf("22\n");
//	if (!(perm & PTE_U) )
//`	return -E_INVAL;

    //    cprintf("23\n");
	//if ( !(perm & PTE_P))
       //return -E_INVAL;

    //.    cprintf("24\n");
	//if ( !(perm &  __EPTE_FULL))
	//return -E_INVAL;

    //    cprintf("25\n");
	struct Env *srcenv, *guestenv;
	int srcerr = envid2env(srcenvid, &srcenv, 1);
	int guesterr = envid2env(guestvid, &guestenv, 1);

        if(guestenv->env_type!=ENV_TYPE_GUEST)	
		return -E_BAD_ENV;
        if (srcerr < 0)
		return -E_BAD_ENV;
	else if (guesterr < 0)
		return -E_BAD_ENV;
	else if (srcerr == 0 && guesterr == 0) {
		pte_t *pte;
  //      cprintf("4\n");
		epte_t *epte_out;
		struct Page *pp = page_lookup(srcenv->env_pml4e, srcva, &pte);

 ///       cprintf("45\n");
		if (pp == NULL){
    //                    cprintf("451\n");
			return -E_INVAL;}
    //    cprintf("456\n");
	if ( !( *pte & PTE_P))
	return -E_INVAL;
	/*	if (!(*pte & PTE_U))
                    {
                        cprintf("452\n");
			return -E_INVAL;
                    }*/
	if ( !(perm &  __EPTE_FULL))
	return -E_INVAL;
	if (!(*pte & PTE_W)&& (perm & PTE_W) )
	return -E_INVAL;
 //       cprintf("5555\n");
	//if (!(*pte & PTE_U)&& (perm & PTE_U) )
	//return -E_INVAL;
   ///     cprintf("6666\n");
	//if (!(*pte & PTE_U) )
	//return -E_INVAL;
  ///      cprintf("66666666\n");

	//ig guest_pa + pagesize > destion env-> vmxinfo sys_sz false -rinval
	if(((uint64_t)guestpa + PGSIZE) > guestenv->env_vmxinfo.phys_sz)
		return -E_INVAL;

		int output = ept_lookup_gpa(guestenv->env_pml4e, guestpa, 1, &epte_out);
 //       cprintf("56\n");
		if (output != 0)
                {
   //                             cprintf("561\n");
			return -E_INVAL;
                 }

  //      cprintf("6\n");
pp->pp_ref++;;
		(*epte_out) = (uint64_t)((page2pa(pp)&(~mask)) | perm);

 //       cprintf("success 1\n");
		return 0;
	}

 //       cprintf("success 2\n");
 //       cprintf("7\n");
	//panic("sys_ept_map not implemented");
	return 0;
}

static envid_t
sys_env_mkguest(uint64_t gphysz, uint64_t gRIP) {
    int r;
    struct Env *e;

    if ((r = env_guest_alloc(&e, curenv->env_id)) < 0)
        return r;
    e->env_status = ENV_NOT_RUNNABLE;
    e->env_vmxinfo.phys_sz = gphysz;
    e->env_tf.tf_rip = gRIP;
    return e->env_id;
}


// Dispatches to the correct kernel function, passing the arguments.
    int64_t
syscall(uint64_t syscallno, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    // Call the function corresponding to the 'syscallno' parameter.
    // Return any appropriate return value.
    // LAB 3: Your code here.
    //cprintf("Syscall number : %x , %x %x %x %x", syscallno, a1, a2, a3, a4);
    //cprintf("Syscall number : %x", syscallno);

    switch (syscallno) {
    		case SYS_cputs:
    			sys_cputs((const char *)a1, (size_t) a2);
    			return 0; //does not really matter!
    		case SYS_cgetc:
    			return sys_cgetc();
    		case SYS_getenvid:
    			return sys_getenvid();
    		case SYS_env_destroy:
    			return sys_env_destroy((envid_t)a1);
    		case SYS_yield:
    			sys_yield();
    			return 0;	
    		case SYS_page_alloc:
    			return sys_page_alloc((envid_t)a1, (void *)a2, (int)a3);
    		case SYS_page_map:
    			return sys_page_map((envid_t)a1, (void *)a2, (envid_t) a3, (void *)a4, (int)a5);
    		case SYS_page_unmap:
    			return sys_page_unmap((envid_t)a1, (void *)a2);
    		case SYS_exofork:
    			return sys_exofork();
    		case SYS_env_set_status:
    			return sys_env_set_status((envid_t)a1,(int)a2);
    		case SYS_env_set_pgfault_upcall:
    			return sys_env_set_pgfault_upcall((envid_t)a1, (void *)a2);
    		case SYS_env_set_trapframe:
    			return sys_env_set_trapframe((envid_t) a1, (struct Trapframe *) a2);
    		case SYS_ept_map:
    			return sys_ept_map(a1, (void*) a2, a3, (void*) a4, a5);
    		case SYS_env_mkguest:
    			return sys_env_mkguest(a1, a2);
    		case SYS_ipc_recv:
    			return sys_ipc_recv((void *)a1);
    		case SYS_ipc_try_send:
    			return sys_ipc_try_send((envid_t) a1, (uint32_t) a2, (void *) a3, (unsigned) a4);
    		case SYS_time_msec:
    			return sys_time_msec();
    		//todo: Network related system calls?

					case SYS_env_transmit_packet:
			return sys_env_transmit_packet(a1, (char*)a2, a3);
		case SYS_env_receive_packet:
			return sys_env_receive_packet(a1, (char*)a2, (size_t*)a3);
    		default:
    			return -E_NO_SYS;
    }
}

#ifdef TEST_EPT_MAP
int
_export_sys_ept_map(envid_t srcenvid, void *srcva,
	    envid_t guest, void* guest_pa, int perm)
{
	return sys_ept_map(srcenvid, srcva, guest, guest_pa, perm);
}
#endif

