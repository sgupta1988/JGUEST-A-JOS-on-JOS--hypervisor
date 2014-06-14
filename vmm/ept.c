
#include <vmm/ept.h>
#include <kern/env.h>
#include <inc/error.h>
#include <inc/memlayout.h>
#include <kern/pmap.h>
#include <inc/string.h>

// Return the physical address of an ept entry
uintptr_t epte_addr(epte_t epte)
{
	return (epte & EPTE_ADDR);
}

// Return the host kernel virtual address of an ept entry
static inline uintptr_t epte_page_vaddr(epte_t epte)
{
	return (uintptr_t) KADDR(epte_addr(epte));
}

// Return the flags from an ept entry
static inline epte_t epte_flags(epte_t epte)
{
	return (epte & EPTE_FLAGS);
}

// Return true if an ept entry's mapping is present
int epte_present(epte_t epte)
{
	return (epte & __EPTE_FULL) > 0;
}

#define EPT_PTSHIFT   12
#define EPT_PDTSHIFT   21
#define EPT_PDPSHIFT   30
#define EPT_PML4SHIFT   39
#define EPT_PT(la)  ((((uintptr_t) (la)) >> EPT_PTSHIFT) & 0x1FF)
#define EPT_PML4(la)  ((((uintptr_t) (la)) >> EPT_PML4SHIFT) & 0x1FF)
#define EPT_PDT(la)  ((((uintptr_t) (la)) >> EPT_PDTSHIFT) & 0x1FF)
#define EPT_PDP(la)  ((((uintptr_t) (la)) >> EPT_PDPSHIFT) & 0x1FF)
// Return true if an ept entry's mapping is present


static int ept_pdte_walk(pde_t* ept_pdt, void *gpa, int create, epte_t **epte_out){
	//cprintf("pdte ept_pdt%x\n",ept_pdt);
	uintptr_t index_in_pdt = EPT_PDT(gpa);
		//cprintf("pdte index_in_pdt%x\n",index_in_pdt);
	pde_t *offsetd_ptr_in_pgdir = ept_pdt + index_in_pdt;
		//cprintf("pdte *offsetd_ptr_in_pgdir%x\n",*offsetd_ptr_in_pgdir);
	pte_t *page_table_base = (pte_t*)(PTE_ADDR(*offsetd_ptr_in_pgdir));
        //cprintf("pdte page_table_base%x\n",page_table_base);
	if (page_table_base==0){
		if (create==0) return -E_NO_ENT;
		else {
			struct Page *newPage = page_alloc(ALLOC_ZERO);
			  //cprintf("pdte newPage%x\n",newPage);

			if (newPage == NULL) return -E_NO_MEM; // Out of memory

			newPage->pp_ref++;
			page_table_base = (pdpe_t*)page2pa(newPage);
			//cprintf("pdte page_table_base%x\n",page_table_base);
			*offsetd_ptr_in_pgdir = ((uint64_t)page_table_base) | __EPTE_FULL;;
			//cprintf("pdte *offsetd_ptr_in_pgdir%x\n",*offsetd_ptr_in_pgdir);
			// Return PTE
			uintptr_t index_in_page_table = EPT_PT(gpa);
			pte_t *offsetd_ptr_in_page_table = page_table_base + index_in_page_table;
			//cprintf("pdte offsetd_ptr_in_page_table%x\n",offsetd_ptr_in_page_table);

			*epte_out=(pte_t*)((KADDR((uint64_t)offsetd_ptr_in_page_table)));
			//*epte_out= (pte_t*)KADDR((uint64_t)offsetd_ptr_in_page_table);
                        return 0;
		}

	}
	else
	{
		// PT exists, so return PTE
		uintptr_t index_in_page_table = PTX(gpa);
		pte_t *offsetd_ptr_in_page_table = page_table_base + index_in_page_table;
		//cprintf("pdte offsetd_ptr_in_page_table%x\n",offsetd_ptr_in_page_table);
		*epte_out=(pte_t*)((KADDR((uint64_t)offsetd_ptr_in_page_table)));
		//*epte_out=(pte_t*)KADDR((uint64_t)offsetd_ptr_in_page_table);
		//cprintf("CHECK:%d\n", __LINE__);
                return 0;
	}
	return -E_INVAL;
}
static int ept_pdpe_walk(pde_t* ept_pdpt, void *gpa, int create, epte_t **epte_out){
        //cprintf("pdpe ept_pdpt%x\n",ept_pdpt);
uintptr_t index_in_pdpt = EPT_PDP(gpa);
        //cprintf("pdpe index_in_pdpt%x\n",index_in_pdpt);
pdpe_t *offsetd_ptr_in_pdpt = ept_pdpt + index_in_pdpt;
        //cprintf("pdpe offsetd_ptr_in_pdpt%x\n",offsetd_ptr_in_pdpt);
		
		//cprintf("CHECK:%d\n", __LINE__);
pde_t *pgdir_base = (pde_t*) PTE_ADDR(*offsetd_ptr_in_pdpt);
        //cprintf("pdpe PD23\n");

		//cprintf("CHECK:%d\n", __LINE__);
// Check if PDP does exists
if (pgdir_base == 0)
{
        //cprintf("pdpe PD3\n");
	if (create==0) return -E_NO_ENT;
	else {
        //cprintf("pdpe PD4\n");
		struct Page *newPage = page_alloc(ALLOC_ZERO);
			  //cprintf("pdpe newPage%x\n",newPage);

		if (newPage == NULL) return -E_NO_MEM; // Out of memory

		newPage->pp_ref++;
        //cprintf("pdpe PD6\n");
		pgdir_base = (pdpe_t*)page2pa(newPage);
		ept_pdte_walk(page2kva(newPage), gpa, create, epte_out);
        //cprintf("pdpe PD7\n");
		if (*epte_out == NULL) page_decref(newPage); // Free allocated page for PDPE
		else {
        //cprintf("pdpe PD8\n");
			*offsetd_ptr_in_pdpt = ((uint64_t)pgdir_base) | __EPTE_FULL;
			return 0;
		}
	}
}

else{
        //cprintf("PD9\n");
		//cprintf("CHECK:%d\n", __LINE__);
return ept_pdte_walk(KADDR((uint64_t)pgdir_base), gpa, create, epte_out);
}
return -E_INVAL;
}

// Error values:
//    -E_INVAL if eptrt is NULL
//    -E_NO_ENT if create == 0 and the intermediate page table entries are missing.
//    -E_NO_MEM if allocation of intermediate page table entries fails
static int ept_pml4e_walk(epte_t* ept_pml4t, void *gpa, int create, epte_t **epte_out){
	uintptr_t index_in_pml4t = EPT_PML4(gpa);
        //cprintf("pml4e index_in_pml4t %x\n", index_in_pml4t);
	pml4e_t *offsetd_ptr_in_pml4t = ept_pml4t + index_in_pml4t;
        //cprintf("pml4e offsetd_ptr_in_pml4t %x\n", offsetd_ptr_in_pml4t);
	pdpe_t *pdpt_base = (pdpe_t*)(PTE_ADDR(*offsetd_ptr_in_pml4t));
        //cprintf("pml4e pdpt_base %x\n", pdpt_base);
	// Check if PDP does exists
	if (pdpt_base == 0){

        //cprintf("pml4e P41\n");
		if (!create) {
                 //cprintf("pml4e P42\n");
                return -E_NO_ENT;
                }
		else {
			struct Page *newPage = page_alloc(ALLOC_ZERO);
			//cprintf("pml4e newPageAddress: %x\n",newPage);

        //cprintf("pml4e  P43\n");
			if (newPage == NULL)
                       {
        //cprintf("pml4e P44\n");
                        return -E_NO_MEM; // Out of memory
                       }
        //cprintf("pml4e P45\n");
                        newPage->pp_ref++;
                        pdpt_base = (pdpe_t*)page2pa(newPage);
						//cprintf("pml4e pdpt_base: %x\n",pdpt_base);
                        ept_pdpe_walk((pdpe_t*)page2kva(newPage), gpa, create,epte_out);
                        if (*epte_out == NULL)
                         {
        //cprintf("pml4e P46\n");
                                page_decref(newPage); // Free allocated page for PDPE
                          }
                        else {
						        //cprintf("pml4e pdpt_base: %x\n",((uint64_t)pdpt_base) | __EPTE_FULL);
                                *offsetd_ptr_in_pml4t = ((uint64_t)pdpt_base) | __EPTE_FULL;
								//cprintf("pml4e *offsetd_ptr_in_pml4t: %x\n",*offsetd_ptr_in_pml4t);
        //cprintf("pml4e P47\n");
                                return 0;
                        }
			//cprintf("pml4e P41\n");
               }
        //cprintf("pml4e P48\n");
        }
        else
                return ept_pdpe_walk(KADDR((uint64_t)pdpt_base), gpa, create, epte_out); // PDP exists, so walk through it.
        return -E_INVAL;
}
// Find the final ept entry for a given guest physical address,
// creating any missing intermediate extended page tables if create is non-zero.
//
// If epte_out is non-NULL, store the found epte_t* at this address.
//
// Return 0 on success.  
// 
// Error values:
//    -E_INVAL if eptrt is NULL
//    -E_NO_ENT if create == 0 and the intermediate page table entries are missing.
//    -E_NO_MEM if allocation of intermediate page table entries fails
//
// Hint: Set the permissions of intermediate ept entries to __EPTE_FULL.
//       The hardware ANDs the permissions at each level, so removing a permission
//       bit at the last level entry is sufficient (and the bookkeeping is much simpler).
int ept_lookup_gpa(epte_t* eptrt, void *gpa, 
			  int create, epte_t **epte_out) {
    //cprintf("eptrt%x\n",eptrt);
    //cprintf("gpa%x\n",gpa);
    //cprintf("create%x\n",create);

    //Shashank
    if (eptrt == NULL)
        return -E_INVAL;
     //cprintf("E1\n");
    epte_t* ept_pml4 = (uint64_t *)(eptrt);// Bit 12-51
 //   if (ept_pml4 == NULL)
   //     return -E_INVAL;

    *epte_out = NULL;

     //cprintf("E2\n");
    int output = ept_pml4e_walk(ept_pml4, gpa, create, epte_out);
  //  *epte_out=(uint64_t *)PADDR(*epte_out);
	
     //cprintf("E3\n");
	if(output==1)
		cprintf("EPT Translation successful\n");
    
     //cprintf("E444444\n");
    return output;
    //panic("ept_lookup_gpa not implemented\n");
}

void ept_gpa2hva(epte_t* eptrt, void *gpa, void **hva) {
    epte_t* pte;
    int ret = ept_lookup_gpa(eptrt, gpa, 0, &pte);
    if(ret < 0) {
        *hva = NULL;
 //       cprintf("\n ept_gpa2hva NULL 1\n");
    } else {
        if(!epte_present(*pte)) {
           *hva = NULL;
   //     cprintf("\n ept_gpa2hva NULL 2\n");
        } else {
           *hva = KADDR(epte_addr(*pte));
     //   cprintf("\n ept_gpa2hva gpa=[%x] hva=[%x]\n",gpa,*hva);
        }
    }
}

static void free_ept_level(epte_t* eptrt, int level) {
    epte_t* dir = eptrt;
    int i;

    for(i=0; i<NPTENTRIES; ++i) {
        if(level != 0) {
            if(epte_present(dir[i])) {
                physaddr_t pa = epte_addr(dir[i]);
                free_ept_level((epte_t*) KADDR(pa), level-1);
                // free the table.
                page_decref(pa2page(pa));
            }
        } else {
            // Last level, free the guest physical page.
            if(epte_present(dir[i])) {
                physaddr_t pa = epte_addr(dir[i]);
                page_decref(pa2page(pa));
            }
        }
    }
    return;
}

// Free the EPT table entries and the EPT tables.
// NOTE: Does not deallocate EPT PML4 page.
void free_guest_mem(epte_t* eptrt) {
    free_ept_level(eptrt, EPT_LEVELS - 1);
}

// Add Page pp to a guest's EPT at guest physical address gpa
//  with permission perm.  eptrt is the EPT root.
// 
// Return 0 on success, <0 on failure.
//
int ept_page_insert(epte_t* eptrt, struct Page* pp, void* gpa, int perm) {

    /* Your code here */
    int val = ept_map_hva2gpa(eptrt, KADDR(page2pa(pp)), gpa, perm, 1);
    if(val < 0) {
 //       cprintf("\n ept_page_insert val=[%d] \n",val);
    }

 //       cprintf("\n ept_page_insert val=[%d] \n",val);
    pp->pp_ref+=1;

    //    cprintf("\n ept_page_insert pp->pp_ref=[%d] \n", pp->pp_ref);
    return val;
  //  panic("ept_page_insert not implemented\n");
  //  return 0;
}

// Map host virtual address hva to guest physical address gpa,
// with permissions perm.  eptrt is a pointer to the extended
// page table root.
//
// Return 0 on success.
// 
// If the mapping already exists and overwrite is set to 0,
//  return -E_INVAL.
// 
// Hint: use ept_lookup_gpa to create the intermediate 
//       ept levels, and return the final epte_t pointer.
int ept_map_hva2gpa(epte_t* eptrt, void* hva, void* gpa, int perm, int overwrite) {

	uint64_t mask = 0xfff;
       //cprintf("\n hva2gpa 11\n");
//	if ((uint64_t)hva >= UTOP || (hva != ROUNDUP(hva, PGSIZE) && hva != ROUNDDOWN(hva, PGSIZE)) ||
//			(uint64_t)gpa >= UTOP || (gpa != ROUNDUP(gpa, PGSIZE) && gpa != ROUNDDOWN(gpa, PGSIZE)))
//		return -E_INVAL;
       //cprintf("\n hva2gpa 12\n");

	epte_t *epte_out;
       //cprintf("\n hva2gpa 13\n");
/*	struct Page *pp = page_lookup(curenv->env_pml4e, hva, &pte);
        
       cprintf("\n hva2gpa 1\n");
	if (pp == NULL)
		return -E_INVAL;

       cprintf("\n hva2gpa 2\n");
	if ( !( *pte & PTE_P))
		return -E_INVAL;
       cprintf("\n hva2gpa 3\n");
	if ( !(perm &  __EPTE_FULL))
		return -E_INVAL;
       cprintf("\n hva2gpa 4\n");
	if (!(*pte & PTE_W)&& (perm & PTE_W) )
		return -E_INVAL;
       cprintf("\n hva2gpa 5\n");
*/

	int output = ept_lookup_gpa(eptrt, gpa, 1, &epte_out);
       //cprintf("\n hva2gpa 6\n");

	if (output != 0)
		return -E_INVAL;
    
       //cprintf("\n hva2gpa 7\n");
    // If the mapping already exists and overwrite is set to 0,
    //  return -E_INVAL.
    if (epte_present(*epte_out) && overwrite==0)
        return -E_INVAL;
       //cprintf("\n hva2gpa 8\n");

	(*epte_out) = (uint64_t)((PADDR(hva)&(~mask)) | perm);
      // cprintf("\n hva2gpa 9\n");
       // cprintf("\n Success hva2gpa\n");
	return 0;
}

int ept_alloc_static(epte_t *eptrt, struct VmxGuestInfo *ginfo) {
    physaddr_t i;
    
    for(i=0x0; i < 0xA0000; i+=PGSIZE) {
        struct Page *p = page_alloc(0);
        p->pp_ref += 1;
        int r = ept_map_hva2gpa(eptrt, page2kva(p), (void *)i, __EPTE_FULL, 0);
    }

    for(i=0x100000; i < ginfo->phys_sz; i+=PGSIZE) {
        struct Page *p = page_alloc(0);
        p->pp_ref += 1;
        int r = ept_map_hva2gpa(eptrt, page2kva(p), (void *)i, __EPTE_FULL, 0);
    }
    return 0;
}

#ifdef TEST_EPT_MAP
#include <kern/env.h>
#include <kern/syscall.h>
int _export_sys_ept_map(envid_t srcenvid, void *srcva,
	    envid_t guest, void* guest_pa, int perm);

int test_ept_map(void)
{
	struct Env *srcenv, *dstenv;
	struct Page *pp;
	epte_t *epte;
	int r;

	/* Initialize source env */
	if ((r = env_alloc(&srcenv, 0)) < 0)
		panic("Failed to allocate env (%d)\n", r);
	if (!(pp = page_alloc(ALLOC_ZERO)))
		panic("Failed to allocate page (%d)\n", r);
	if ((r = page_insert(srcenv->env_pml4e, pp, UTEMP, 0)) < 0)
		panic("Failed to insert page (%d)\n", r);
	curenv = srcenv;

	/* Check if sys_ept_map correctly verify the target env */
	if ((r = env_alloc(&dstenv, srcenv->env_id)) < 0)
		panic("Failed to allocate env (%d)\n", r);
	if ((r = _export_sys_ept_map(srcenv->env_id, UTEMP, dstenv->env_id, UTEMP, __EPTE_READ)) < 0)
		cprintf("EPT map to non-guest env failed as expected (%d).\n", r);
	else
		panic("sys_ept_map success on non-guest env.\n");

	/*env_destroy(dstenv);*/

	if ((r = env_guest_alloc(&dstenv, srcenv->env_id)) < 0)
		panic("Failed to allocate guest env (%d)\n", r);
	dstenv->env_vmxinfo.phys_sz = (uint64_t)UTEMP + PGSIZE;

	/* Check if sys_ept_map can verify srcva correctly */
	if ((r = _export_sys_ept_map(srcenv->env_id, (void *)UTOP, dstenv->env_id, UTEMP, __EPTE_READ)) < 0)
		cprintf("EPT map from above UTOP area failed as expected (%d).\n", r);
	else
		panic("sys_ept_map from above UTOP area success\n");
	if ((r = _export_sys_ept_map(srcenv->env_id, UTEMP+1, dstenv->env_id, UTEMP, __EPTE_READ)) < 0)
		cprintf("EPT map from unaligned srcva failed as expected (%d).\n", r);
	else
		panic("sys_ept_map from unaligned srcva success\n");

	/* Check if sys_ept_map can verify guest_pa correctly */
	if ((r = _export_sys_ept_map(srcenv->env_id, UTEMP, dstenv->env_id, UTEMP + PGSIZE, __EPTE_READ)) < 0)
		cprintf("EPT map to out-of-boundary area failed as expected (%d).\n", r);
	else
		panic("sys_ept_map success on out-of-boundary area\n");
	if ((r = _export_sys_ept_map(srcenv->env_id, UTEMP, dstenv->env_id, UTEMP-1, __EPTE_READ)) < 0)
		cprintf("EPT map to unaligned guest_pa failed as expected (%d).\n", r);
	else
		panic("sys_ept_map success on unaligned guest_pa\n");

	/* Check if the sys_ept_map can verify the permission correctly */
	if ((r = _export_sys_ept_map(srcenv->env_id, UTEMP, dstenv->env_id, UTEMP, 0)) < 0)
		cprintf("EPT map with empty perm parameter failed as expected (%d).\n", r);
	else
		panic("sys_ept_map success on empty perm\n");
	if ((r = _export_sys_ept_map(srcenv->env_id, UTEMP, dstenv->env_id, UTEMP, __EPTE_WRITE)) < 0)
		cprintf("EPT map with write perm parameter failed as expected (%d).\n", r);
	else
		panic("sys_ept_map success on write perm\n");

	/* Check if the sys_ept_map can succeed on correct setup */
	if ((r = _export_sys_ept_map(srcenv->env_id, UTEMP, dstenv->env_id, UTEMP, __EPTE_READ)) < 0)
		panic("Failed to do sys_ept_map (%d)\n", r);
	else
		cprintf("sys_ept_map finished normally.\n");
	cprintf("ABHI%d\n", __LINE__);
	/* Check if the mapping is valid */
	if ((r = ept_lookup_gpa(dstenv->env_pml4e, UTEMP, 0, &epte)) < 0)
		panic("Failed on ept_lookup_gpa (%d)\n", r);
	if (page2pa(pp) != (epte_addr(*epte)))
		panic("EPT mapping address mismatching (%x vs %x).\n",
				page2pa(pp), epte_addr(*epte));
	else
		cprintf("EPT mapping address looks good: %x vs %x.\n",
				page2pa(pp), epte_addr(*epte));

	/* stop running after test, as this is just a test run. */
	panic("Cheers! sys_ept_map seems to work correctly.\n");

	return 0;
}
#endif

