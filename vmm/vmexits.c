
#include <vmm/vmx.h>
#include <inc/error.h>
#include <vmm/vmexits.h>
#include <vmm/ept.h>
#include <inc/x86.h>
#include <inc/assert.h>
#include <kern/pmap.h>
#include <kern/console.h>
#include <kern/kclock.h>
#include <kern/multiboot.h>
#include <inc/string.h>
#include <kern/syscall.h>
#include <kern/env.h>
#include <kern/e1000.h>

void sched_yield(void);

bool
find_msr_in_region(uint32_t msr_idx, uintptr_t *area, int area_sz, struct vmx_msr_entry **msr_entry) {
    struct vmx_msr_entry *entry = (struct vmx_msr_entry *)area;
    int i;
    for(i=0; i<area_sz; ++i) {
        if(entry->msr_index == msr_idx) {
            *msr_entry = entry;
            return true;
        }
    }
    return false;
}

bool
handle_rdmsr(struct Trapframe *tf, struct VmxGuestInfo *ginfo) {
    uint64_t msr = tf->tf_regs.reg_rcx;
    if(msr == EFER_MSR) {
        // TODO: setup msr_bitmap to ignore EFER_MSR
        uint64_t val;
        struct vmx_msr_entry *entry;
        bool r = find_msr_in_region(msr, ginfo->msr_guest_area, ginfo->msr_count, &entry);
        assert(r);
        val = entry->msr_value;

        tf->tf_regs.reg_rdx = val << 32;
        tf->tf_regs.reg_rax = val & 0xFFFFFFFF;

        tf->tf_rip += vmcs_read32(VMCS_32BIT_VMEXIT_INSTRUCTION_LENGTH);
        return true;
    }

    return false;
}

bool 
handle_wrmsr(struct Trapframe *tf, struct VmxGuestInfo *ginfo) {
    uint64_t msr = tf->tf_regs.reg_rcx;
    if(msr == EFER_MSR) {

        uint64_t cur_val, new_val;
        struct vmx_msr_entry *entry;
        bool r = 
            find_msr_in_region(msr, ginfo->msr_guest_area, ginfo->msr_count, &entry);
        assert(r);
        cur_val = entry->msr_value;

        new_val = (tf->tf_regs.reg_rdx << 32)|tf->tf_regs.reg_rax;
        if(BIT(cur_val, EFER_LME) == 0 && BIT(new_val, EFER_LME) == 1) {
            // Long mode enable.
            uint32_t entry_ctls = vmcs_read32( VMCS_32BIT_CONTROL_VMENTRY_CONTROLS );
            entry_ctls |= VMCS_VMENTRY_x64_GUEST;
            vmcs_write32( VMCS_32BIT_CONTROL_VMENTRY_CONTROLS, 
                    entry_ctls );

        }

        entry->msr_value = new_val;
        tf->tf_rip += vmcs_read32(VMCS_32BIT_VMEXIT_INSTRUCTION_LENGTH);
        return true;
    }

    return false;
}

bool
handle_eptviolation(uint64_t *eptrt, struct VmxGuestInfo *ginfo) {
    uint64_t gpa = vmcs_read64(VMCS_64BIT_GUEST_PHYSICAL_ADDR);
    int r;
//    cprintf("\n handle_eptviolation gpa=[%x] ginfo->phys_sz=[%x]\n",gpa ,ginfo->phys_sz);
    if(gpa < 0xA0000 || (gpa >= 0x100000 && gpa < ginfo->phys_sz)) {
        // Allocate a new page to the guest.
        struct Page *p = page_alloc(0);
        if(!p)
            return false;
        p->pp_ref += 1;
        //cprintf("EPT violation for gpa:%x mapped KVA:%x\n", gpa, page2kva(p));
        r = ept_map_hva2gpa(eptrt, 
                page2kva(p), (void *)ROUNDDOWN(gpa, PGSIZE), __EPTE_FULL, 0);
        assert(r >= 0);
        /* cprintf("EPT violation for gpa:%x mapped KVA:%x\n", gpa, page2kva(p)); */
        return true;
    } else if (gpa >= CGA_BUF && gpa < CGA_BUF + PGSIZE) {
        // FIXME: This give direct access to VGA MMIO region.
        r = ept_map_hva2gpa(eptrt,
                (void *)(KERNBASE + CGA_BUF), (void *)CGA_BUF, __EPTE_FULL, 0);
        assert(r >= 0);
        return true;
    } else if (gpa >= 0xF0000 && gpa <= 0xF0000  + 0x10000) {
        r = ept_map_hva2gpa(eptrt,
                (void *)(KERNBASE + gpa), (void *)gpa, __EPTE_FULL, 0);
        assert(r >= 0);
        return true;
    } else if (gpa >=0xfee00000 /*0xF0000 && gpa <= 0xF0000  + 0x10000*/) {
        r = ept_map_hva2gpa(eptrt,
                (void *)(KERNBASE + gpa), (void *)(gpa), __EPTE_FULL, 0);
        assert(r >= 0);
        return true;
}
    return false;
}

bool
handle_ioinstr(struct Trapframe *tf, struct VmxGuestInfo *ginfo) {
    static int port_iortc;

    uint64_t qualification = vmcs_read64(VMCS_VMEXIT_QUALIFICATION);
    int port_number = (qualification >> 16) & 0xFFFF;
    bool is_in = BIT(qualification, 3);
    bool handled = false;

    // handle reading physical memory from the CMOS.
    if(port_number == IO_RTC) {
        if(!is_in) {
            port_iortc = tf->tf_regs.reg_rax;
            handled = true;
        }
    } else if (port_number == IO_RTC + 1) {
        if(is_in) {
            if(port_iortc == NVRAM_BASELO) {
                tf->tf_regs.reg_rax = 640 & 0xFF;
                handled = true;
            } else if (port_iortc == NVRAM_BASEHI) {
                tf->tf_regs.reg_rax = (640 >> 8) & 0xFF;
                handled = true;
            } else if (port_iortc == NVRAM_EXTLO) {
                tf->tf_regs.reg_rax = ((ginfo->phys_sz / 1024) - 1024) & 0xFF;
                handled = true;
            } else if (port_iortc == NVRAM_EXTHI) {
                tf->tf_regs.reg_rax = (((ginfo->phys_sz / 1024) - 1024) >> 8) & 0xFF;
                handled = true;
            }
        }

    }

    if(handled) {
        tf->tf_rip += vmcs_read32(VMCS_32BIT_VMEXIT_INSTRUCTION_LENGTH);
        return true;
    } else {
        cprintf("%x %x\n", qualification, port_iortc);
        return false;    
    }
}

// Emulate a cpuid instruction.
// It is sufficient to issue the cpuid instruction here and collect the return value.
// You can store the output of the instruction in Trapframe tf,
//  but you should hide the presence of vmx from the guest if processor features are requested.
// 
// Return true if the exit is handled properly, false if the VM should be terminated.
//
// Finally, you need to increment the program counter in the trap frame.
// 
// Hint: The TA's solution does not hard-code the length of the cpuid instruction.
bool handle_cpuid(struct Trapframe *tf, struct VmxGuestInfo *ginfo){
   /* Your code here */

	uint32_t info,eax=0, ebx=0, ecx=0, edx=0;
	unsigned int mask=32;


	info=tf->tf_regs.reg_rax;
	cpuid(info,&eax,&ebx,&ecx,&edx);

       //	cprintf("\n IN handle_cpuid info=[%x],eax=[%x],ebx=[%x],ecx=[%x],edx=[%x]\n",info,eax,ebx,ecx,edx);
	ecx=ecx &(~mask);
	//cprintf("\n IN handle_cpuid info=[%x],eax=[%x],ebx=[%x],ecx=[%x],edx=[%x]\n",info,eax,ebx,ecx,edx);
	tf->tf_regs.reg_rax=eax;
	tf->tf_regs.reg_rbx=ebx;
	tf->tf_regs.reg_rcx=ecx;
	tf->tf_regs.reg_rdx=edx;

	tf->tf_rip += vmcs_read32(VMCS_32BIT_VMEXIT_INSTRUCTION_LENGTH);
	/*  static __inline void
cpuid(uint32_t info, uint32_t *eaxp, uint32_t *ebxp, uint32_t *ecxp, uint32_t *edxp)
{
    uint32_t eax, ebx, ecx, edx;
    asm volatile("cpuid" 
            : "=a" (eax), "=b" (ebx), "=c" (ecx), "=d" (edx)
            : "a" (info));
    if (eaxp)
        *eaxp = eax;
    if (ebxp)
        *ebxp = ebx;
    if (ecxp)
        *ecxp = ecx;
    if (edxp)
        *edxp = edx;


}*/
    cprintf("\nHandle cpuid Success\n");
    return true;
 //   cprintf("Handle cpuid not implemented\n");
  //  return false;

}

// Handle vmcall traps from the guest.
// We currently support 3 traps: read the virtual e820 map, 
//   and use host-level IPC (send andrecv).
//
// Return true if the exit is handled properly, false if the VM should be terminated.
//
// Finally, you need to increment the program counter in the trap frame.
// 
// Hint: The TA's solution does not hard-code the length of the cpuid instruction.//

	bool
handle_vmcall(struct Trapframe *tf, struct VmxGuestInfo *gInfo, uint64_t *eptrt)
{
	bool handled = false;
	multiboot_info_t mbinfo;

	/********************************************************/
	memory_map_t mmap_list[3],*mmap;
	epte_t *epte_out;
	static envid_t fsenv=0;
	int len;
	/**************************************************/
	int perm, r;
	void *gpa_pg, *hva_pg,*pg;
	envid_t to_env;
	uint32_t val;


	// phys address of the multiboot map in the guest.
	uint64_t multiboot_map_addr = 0x6000;

	switch(tf->tf_regs.reg_rax) {
		case VMX_VMCALL_MBMAP:

			// Craft a multiboot (e820) memory map for the guest.
			//
			// Create three  memory mapping segments: 640k of low mem, the I/O hole (unusable), and 
			//   high memory (phys_size - 1024k).  //
			/********************************************************************/

			mmap=&mmap_list[0];

			mmap->type=MB_TYPE_USABLE;
			mmap->size=0x14;
			mmap->base_addr_low=0x0;
			mmap->base_addr_high=0x0;
			mmap->length_low=0x000A0000;
			mmap->length_high=0x0;

			//cprintf("\nBoot map segment [0] addr_low=[%x] addr_high=[%x] length_low=[%x] length_high=[%x] gInfo->phys_sz=[%x]\n", mmap->base_addr_low, mmap->base_addr_high, mmap->length_low, mmap->length_high,gInfo->phys_sz);
			mmap=&mmap_list[1];

			mmap->type=MB_TYPE_RESERVED;
			mmap->size=0x14;
			mmap->base_addr_low=0x000A0000;
			mmap->base_addr_high=0x0;
			mmap->length_low=0x00060000;
			mmap->length_high=0x0;

			// cprintf("\nBoot map segment [1] addr_low=[%x] addr_high=[%x] length_low=[%x] length_high=[%x] gInfo->phys_sz=[%x]\n", mmap->base_addr_low, mmap->base_addr_high, mmap->length_low, mmap->length_high,gInfo->phys_sz);
			mmap=&mmap_list[2];

			mmap->type=MB_TYPE_USABLE;
			mmap->size=0x14;
			mmap->base_addr_low=0x00100000;
			mmap->base_addr_high=0x0;
			mmap->length_low=(uint32_t)((gInfo->phys_sz-0x00100000) & (0xffffffff));
			mmap->length_high=(uint32_t)(((gInfo->phys_sz-0x00100000)>>32) & (0xffffffff));

			// cprintf("\nBoot map segment [2] addr_low=[%x] addr_high=[%x] length_low=[%x] length_high=[%x] gInfo->phys_sz=[%x]\n", mmap->base_addr_low, mmap->base_addr_high, mmap->length_low, mmap->length_high,gInfo->phys_sz);

			/*******************************************************************/

			// Once the map is ready, find the kernel virtual address of the guest page (if present),
			//   or allocate one and map it at the multiboot_map_addr (0x6000).
			/**********************************************************************/
			uint64_t mask = 0xfff;

			//	    int output= ept_lookup_gpa(eptrt,KADDR((uint64_t)multiboot_map_addr) ,1,&epte_out); 

			ept_gpa2hva(eptrt,(void *) multiboot_map_addr,(void *)&epte_out); 
			uint64_t *kernel_page_address = epte_out;
			uint64_t *phy_page_address;
			if(epte_out == NULL){
				epte_t *epte_new;
				struct Page *newPage = page_alloc(ALLOC_ZERO);
				if (newPage == NULL){
					cprintf("\n ERROR :: Allocating new page for multiboot map vmcall\n");
					page_decref(newPage);
					handled = false;
					break;
				}
				newPage->pp_ref++;
				int output= ept_lookup_gpa(eptrt,(void *)multiboot_map_addr ,1,&epte_new); 
				if (output != 0){
					cprintf("\n ERROR :: multiboot map vmcall :: in ept_lookup_gpa\n");
					handled = false;
					break;
				}
				phy_page_address = (uint64_t *)page2pa(newPage);
				(*epte_new) = (uint64_t)(((uint64_t)phy_page_address &(~mask)) | __EPTE_FULL);
				epte_out=KADDR((uint64_t)phy_page_address);
				kernel_page_address = epte_out;
			}
			phy_page_address=(uint64_t *)PADDR((uint64_t)epte_out);

			/************************************************************/
			//    if (output != 0)
			//    return -E_INVAL;

			//   uint64_t *phy_page_address = (pte_t*)(PTE_ADDR(*epte_out));
			//	    uint64_t *kernel_page_address ;

			// if (phy_page_address==0){
			//	    struct Page *newPage = page_alloc(ALLOC_ZERO);
			//	    if (newPage == NULL) return -E_NO_MEM; // Out of memory
			//	    newPage->pp_ref++;
			//	    phy_page_address = (uint64_t *)page2pa(newPage);
			//		    (*epte_out) = (uint64_t)(((uint64_t)phy_page_address &(~mask)) | __EPTE_FULL);

			//	    }


			//	    kernel_page_address=KADDR((uint64_t)phy_page_address);

			/********************************************************************/
			/*********************************************************/
			// Copy the mbinfo and memory_map_t (segment descriptions) into the guest page, and return
			//   a pointer to this region in rbx (as a guest physical address).
			/* Your code here */
			/*********************************************************/

			mbinfo.flags=MB_FLAG_MMAP;
			mbinfo.mmap_length=sizeof(mmap_list);
			mbinfo.mmap_addr=(uint32_t)(((uint64_t)multiboot_map_addr +(uint64_t) sizeof(multiboot_info_t)) & 0xffffffff);

			//mbinfo.mmap_addr=(char *)kernel_page_address + (char *)sizeof(multiboot_info_t);

			memcpy((void *) kernel_page_address, (void *)&mbinfo, sizeof(multiboot_info_t));
			memcpy((void *) ((uint64_t)kernel_page_address + (uint64_t)sizeof(multiboot_info_t)), (void *)mmap_list, sizeof(mmap_list));

			tf->tf_regs.reg_rbx=(uint64_t)multiboot_map_addr;//kernel_page_address;
			/********************************************************************************/
			/*	    asm(   	"mov %0,%%rbx \n\t"
				    : : "r"((unsigned long)kernel_page_address) 
				    : "rbx"
				    );
			 */
			/****************************************************************************/
			// Return true if the exit is handled properly, false if the VM should be terminated.
			//
			// Finally, you need to increment the program counter in the trap frame.
			// 
			// Hint: The TA's solution does not hard-code the length of the cpuid instruction.//

			cprintf("\ne820 map hypercall Success\n");	    
			handled = true;
			break;


			/*	case VMX_VMCALL_NS_TIME_MSEC:
				tf->tf_regs.reg_rax=sys_time_msec();
				tf->tf_regs.reg_rbx= tf->tf_regs.reg_rax;

			//    cprintf("\n HOST reg_rbx=[%d] reg_rax=[%d]\n",tf->tf_regs.reg_rbx,tf->tf_regs.reg_rax); 
			//  cprintf("\nIPC VMX_VMCALL_NS_TIME_MSEC SUCCESS \n");	    
			handled = true;
			break;

			 */
		case VMX_VMCALL_NS_PKT_INPUT:
			//	    cprintf("\nIPC VMX_VMCALL_NS_PKT_INPUT START \n");	    
			gpa_pg=(void *)(tf->tf_regs.reg_rdx);
			//   pg=gpa_pg;
			//  len=tf->tf_regs.reg_rcx;
			uint64_t *guest_len=(uint64_t *)tf->tf_regs.reg_rcx;
			to_env=tf->tf_regs.reg_rbx;

			//  cprintf("\n VMX_VMCALL_NS_PKT_INPUT gpa_pg=[%x] pg=[%x]\n",gpa_pg,pg);
			ept_gpa2hva(curenv->env_pml4e, gpa_pg, &pg); 
			//  cprintf("\n VMX_VMCALL_NS_PKT_INPUT gpa_pg=[%x] pg=[%x]\n",gpa_pg,pg);
			//   e1000_receive_packet((char *)pg,(size_t *)&len);
			perm=0;
			len=0;
			perm =guest_e1000_receive_packet((char *)pg,(size_t *)&len);
			// tf->tf_regs.reg_rsi =guest_e1000_receive_packet((char *)pg,(size_t *)&len);
			//  tf->tf_regs.reg_rsi =sys_env_receive_packet(to_env,(char *)gpa_pg,(size_t *)&len);
			tf->tf_regs.reg_rsi =perm;
			tf->tf_regs.reg_rax=tf->tf_regs.reg_rsi;

			if(perm==0){
				//    cprintf("\n guest_e1000_receive_packet=[%d]",perm);
				//  cprintf("\n In VMCALL DATA with len=[%d] IS=[",len);
				tf->tf_regs.reg_rsi =len;
				tf->tf_regs.reg_rax=tf->tf_regs.reg_rsi;
				//   char *buf=(char *)pg;
				// int i=0;
				//  for(i=0;i<len;i++)
				//	    cprintf("%u ",buf[i]);
				//   cprintf("]\n");


			}
			//   cprintf("\nIPC VMX_VMCALL_NS_PKT_INPUT SUCCESS \n");	    
			handled = true;
			break;

		case VMX_VMCALL_NS_PKT_OUTPUT:
			//   cprintf("\nIPC VMX_VMCALL_NS_PKT_OUTPUT START \n");	    
			gpa_pg=(void *)(tf->tf_regs.reg_rdx);
			pg=gpa_pg;
			len=tf->tf_regs.reg_rcx;

			//  cprintf("\n shashank 1 \n");
			ept_gpa2hva(curenv->env_pml4e, gpa_pg, &pg); 
			//  cprintf("\n shashank 2\n");
			//		user_mem_assert(curenv, (void*)pg, len, PTE_P | PTE_U);
			//  cprintf("\n shashank 3\n");
			//	user_mem_assert(curenv, (void*)gpa_pg, len, PTE_P | PTE_U);
			/*	if((vpml4e[VPML4E(pg)] & PTE_P) && (vpde[VPDPE(pg)] & PTE_P)
				&& (vpd[VPD(pg)] & PTE_P) && (vpt[VPN(pg)] & PTE_P))
				{cprintf("\nGUEST AADESS IS MAPPED\n");}
				else
				{cprintf("\nGUEST AADESS IS NOT  MAPPED\n");}
			 */
			//  cprintf("\n shashank 4\n");
			to_env=tf->tf_regs.reg_rbx;
			//	    tf->tf_regs.reg_rsi =sys_env_transmit_packet(to_env,(char *)pg,len);
			perm=0;
			//    cprintf("\n e1000_transmit_packet start=[%d]\n",perm);
			perm =e1000_transmit_packet((char *)pg,len);
			//     cprintf("\n e1000_transmit_packet end=[%d]\n",perm);
			tf->tf_regs.reg_rsi=perm; 
			//  tf->tf_regs.reg_rsi =e1000_transmit_packet((char *)pg,len);
			//  cprintf("\n shashank 5\n");
			tf->tf_regs.reg_rax=tf->tf_regs.reg_rsi;
			if(perm==0){
				//    cprintf("\n guest_e1000_receive_packet=[%d]",perm);
				// cprintf("\n In VMCALL DATA Transitted with len=[%d] IS=[",len);
				tf->tf_regs.reg_rsi =perm;
				tf->tf_regs.reg_rax=tf->tf_regs.reg_rsi;
				//  char *buf=(char *)pg;
				//  int i=0;
				//    for(i=0;i<len;i++)
				//	    cprintf("%u ",buf[i]);
				//  cprintf("]\n");


			}
			//    cprintf("\nIPC VMX_VMCALL_NS_PKT_OUTPUT SUCCESS \n");	    
			handled = true;
			break;

		case VMX_VMCALL_IPCSEND:
			// Issue the sys_ipc_send call to the host.
			// 
			// If the requested environment is the HOST FS, this call should
			//  do this translation.
			/* Your code here */
			//   int perm, r;
			//  void *gpa_pg, *hva_pg;
			//  envid_t to_env;
			//  uint32_t val;
			//  if (fsenv == 0)
			//	fsenv = ipc_find_env(ENV_TYPE_FS);
			to_env=tf->tf_regs.reg_rbx;
			val=tf->tf_regs.reg_rcx;
			gpa_pg=(void *)(tf->tf_regs.reg_rdx);
			perm=tf->tf_regs.reg_rdi;
			// cprintf("\n IN VMCALL VMX_VMCALL_IPCSEND to_env=[%x],val=[%x],gpa_pg=[%x],perm=[%x]",to_env,val,gpa_pg,perm);

			if(to_env==VMX_HOST_FS_ENV){
				int i;
				for (i = 0; i < NENV; i++) {
					if (envs[i].env_type == ENV_TYPE_FS)
						fsenv=envs[i].env_id;
				}
				//		    cprintf("\n IPC VMX_VMCALL_IPCSEND :: sys_ipc_try_send :: fsenv=[%x],val=[%x],gpa_pg=[%x],perm=[%x]\n",fsenv,val,gpa_pg,perm);
				tf->tf_regs.reg_rsi = sys_ipc_try_send(fsenv,val,gpa_pg,perm); 
				//	    cprintf("\n IPC VMX_VMCALL_IPCSEND :: sys_ipc_try_send :: tf->tf_regs.reg_rsi=[%x]\n",tf->tf_regs.reg_rsi);
				tf->tf_regs.reg_rax=tf->tf_regs.reg_rsi;
				//  cprintf("\nIPC VMX_VMCALL_IPCSEND SUCCESS \n");	    
				handled = true;
				break;

			}
			else{
				cprintf("\nIPC VMX_VMCALL_IPCSEND WRONG ENV \n");	    
				handled = false;
				break;

			}

			//    cprintf("IPC send hypercall not implemented\n");	    
			//  handled = true ;
			// handled = false;
			break;
			// r=ipc_send(fsenv, val,gpa_pg,perm);
			//r=sys_ipc_try_send(to_env, val,gpa_pg,perm);
			//          r=ipc_send(to_env, val,gpa_pg,perm);
			/*******************************************************************************************/
			// Send 'val' (and 'pg' with 'perm', if 'pg' is nonnull) to 'toenv'.
			// This function keeps trying until it succeeds.
			// It should panic() on any error other than -E_IPC_NOT_RECV.
			//
			// Hint:
			//   Use sys_yield() to be CPU-friendly.
			//   If 'pg' is null, pass sys_ipc_recv a value that it will understand
			//   as meaning "no page".  (Zero is not the right value.)
			//   static int
			//sys_ipc_try_send(envid_t envid, uint32_t value, void *srcva, unsigned perm)
			/********************************************************************************************/


		case VMX_VMCALL_IPCRECV:
			// Issue the sys_ipc_recv call for the guest.
			// NB: because recv can call schedule, clobbering the VMCS, 
			// you should go ahead and increment rip before this call.
			/* Your code here */
			gpa_pg=(void *)(tf->tf_regs.reg_rdx);
			//	cprintf("\n IN VMCALL VMX_VMCALL_IPCRECV gpa_pg=[%x]",gpa_pg);
			tf->tf_rip += vmcs_read32(VMCS_32BIT_VMEXIT_INSTRUCTION_LENGTH);
			//      cprintf("\n after increasing rip VMX_VMCALL_IPCRECV \n");

			tf->tf_regs.reg_rsi = 0; 
			tf->tf_regs.reg_rax=tf->tf_regs.reg_rsi;
			sys_ipc_recv(gpa_pg); 
			//	cprintf("\nIPC recv hypercall Success :: tf_regs.reg_rsi=[%x]\n", tf->tf_regs.reg_rsi );
			//sched_yield();	    
			handled = true;
			break;
	}
	if(handled && tf->tf_regs.reg_rax != VMX_VMCALL_IPCRECV) {
		/* TODO Advance the program counter by the length of the vmcall instruction. 
		 * 
		 * Hint: The TA solution does not hard-code the length of the vmcall instruction.
		 */
		/* Your code here */
		tf->tf_rip += vmcs_read32(VMCS_32BIT_VMEXIT_INSTRUCTION_LENGTH);
	}
	return handled;
}

