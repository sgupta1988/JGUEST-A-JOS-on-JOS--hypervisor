#include <inc/lib.h>
#include <inc/vmx.h>
#include <inc/elf.h>
#include <inc/ept.h>

#define GUEST_KERN "/vmm/kernel"
#define GUEST_BOOT "/vmm/boot"
//#define GUEST_BIOS "/vmm/BIOS-bochs-latest"

#define JOS_ENTRY 0x7000
//#define BIOS_REGION 0xF0000

int counter = 0;

// Map a region of file fd into the guest at guest physical address gpa.
// The file region to map should start at fileoffset and be length filesz.
// The region to map in the guest should be memsz.  The region can span multiple pages.
//
// Return 0 on success, <0 on failure.
//
static int map_in_guest( envid_t guest, uintptr_t gpa, size_t memsz,int fd, size_t filesz, off_t fileoffset ) {
	/* Your code here */
	int count = 0,nxt_addr=0;
	int bytes_read = 0;
        uintptr_t orig_gpa=gpa;
	while (bytes_read <= filesz){
		
		if(bytes_read==filesz)
			break;

		sys_page_alloc(sys_getenvid(),(void *)(UTEMP), PTE_P | PTE_U | PTE_W);

		seek(fd, fileoffset /*+ (nxt_addr * 4096)*/);
                
                fileoffset+=4096;

		if(filesz-bytes_read > 4096)
			count = read(fd, (char *)UTEMP, 4096);
		else
			count = read(fd, (char *)UTEMP, filesz-bytes_read);


		if (count <= 0){
			cprintf("ERROR READING KERNEL kernfd program headers  %d\n", fd);
			return -E_NO_SYS;
		}

		sys_ept_map(sys_getenvid(),(void *)(UTEMP),guest,(void *)( gpa/* + (nxt_addr *4096)*/),__EPTE_FULL); 

                gpa=(uint64_t) ((char *)gpa+4096);  
                
		sys_page_unmap(sys_getenvid(), (void *)(UTEMP));

		bytes_read += count;
	}


        filesz += 4096 - (filesz % 4096);
	if (filesz < memsz){
		while ((memsz - filesz)>4096){
			sys_page_alloc(sys_getenvid(),(void *)(UTEMP), PTE_P | PTE_U | PTE_W);
			sys_ept_map(sys_getenvid(),(void *)(UTEMP),guest,(void *)(gpa),__EPTE_FULL); 
			gpa=(uint64_t) ((char *)gpa+4096);  
			sys_page_unmap(sys_getenvid(), (void *)(UTEMP));
			filesz += 4096;
		}
		if (filesz < memsz){
			sys_page_alloc(sys_getenvid(),(void *)(UTEMP), PTE_P | PTE_U | PTE_W);
			sys_ept_map(sys_getenvid(),(void *)(UTEMP),guest,(void *)(gpa),__EPTE_FULL); 
			gpa=(uint64_t) ((char *)gpa+4096);  
			sys_page_unmap(sys_getenvid(), (void *)(UTEMP));
			filesz += 4096;
		}
	}
	return 0;

} 


// Read the ELF headers of kernel file specified by fname,
// mapping all valid segments into guest physical memory as appropriate.
//
// Return 0 on success, <0 on error
//
// Hint: compare with ELF parsing in env.c, and use map_in_guest for each segment.
static int copy_guest_kern_gpa( envid_t guest, char* fname ) {
	int kernfd, num_ph_hdr = 1;
	struct Elf kelf, *elf;
	struct Proghdr kph;

	if ((kernfd = open(fname, O_RDONLY)) < 0)
	cprintf("\n open %s: %e", fname, kernfd);

	memset(&kelf, 0, sizeof(struct Elf));
	int count = read(kernfd, &kelf, sizeof(struct Elf));

	if (count <= 0){
		cprintf("ERROR READING KERNEL kernfd %d\n", kernfd);
		return -E_NO_SYS;
	}

	elf = &kelf;

	if (elf->e_magic != ELF_MAGIC){
		cprintf("load_kernel_code is not an ELF image\n");
		return -E_NO_SYS;
	}

	for (num_ph_hdr = 1; num_ph_hdr <= elf->e_phnum; num_ph_hdr++){

		//cprintf("\n num_ph_hdr [%d] , elf->e_phnum [%d] kernfd [%d]",num_ph_hdr,elf->e_phnum,kernfd);

		memset(&kph, 0, sizeof(struct Proghdr));

		seek(kernfd, ((num_ph_hdr-1)*sizeof(struct Proghdr)) + elf->e_phoff);
		//	seek(kernfd, (num_ph_hdr)* elf->e_phoff);

		count = read(kernfd, &kph, sizeof(struct Proghdr));
		if (kph.p_type == ELF_PROG_LOAD) {
			if (kph.p_filesz > kph.p_memsz)
				cprintf("ERROR :: copy_guest_kern_gpa:: ph->p_filesz > ph->p_memsz\n");
			map_in_guest(guest, kph.p_pa, kph.p_memsz, kernfd, kph.p_filesz, kph.p_offset);
			//static int map_in_guest( envid_t guest, uintptr_t gpa, size_t memsz, int fd, size_t filesz, off_t fileoffset ) {

		}
		}
		close(kernfd);
		cprintf("\nFinished succesfully Kernel Mapped\n");
		return 0;
	}

void
umain(int argc, char **argv) {
    int ret;
    envid_t guest;

    if ((ret = sys_env_mkguest( GUEST_MEM_SZ, JOS_ENTRY )) < 0) {
        cprintf("Error creating a guest OS env: %e\n", ret );
        exit();
    }
    guest = ret;

    if((ret = copy_guest_kern_gpa(guest, GUEST_KERN)) < 0) {
	cprintf("Error copying page into the guest - %e\n.", ret);
        exit();
    }

    int fd;
    if ((fd = open( GUEST_BOOT, O_RDONLY)) < 0 ) {
        cprintf("open %s for read: %e\n", GUEST_BOOT, fd );
        exit();
    }

    if ((ret = map_in_guest(guest, JOS_ENTRY, 512, fd, 512, 0)) < 0) {
	cprintf("Error mapping bootloader into the guest - %d\n.", ret);
	exit();
    }
    cprintf("Bootloader Mapping Finished");

    sys_env_set_status(guest, ENV_RUNNABLE);
    cprintf("Marked the guest as runnable..");
    wait(guest);
}


