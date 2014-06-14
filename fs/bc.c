
#include "fs.h"

    void*
diskaddr(uint64_t blockno)
{
    if (blockno == 0 || (super && blockno >= super->s_nblocks))
        panic("bad block number %08x in diskaddr", blockno);
    return (char*) (DISKMAP + blockno * BLKSIZE);
}

    bool
va_is_mapped(void *va)
{
    return (vpml4e[VPML4E(va)] & PTE_P) && (vpde[VPDPE(va)] & PTE_P) && (vpd[VPD(va)] & PTE_P) && (vpt[PPN(va)] & PTE_P);
}

    bool
va_is_dirty(void *va)
{
    return (vpt[PPN(va)] & PTE_D) != 0;
}

// Fault any disk block that is read or written in to memory by
// loading it from disk.
// Hint: Use ide_read and BLKSECTS.
    static void
bc_pgfault(struct UTrapframe *utf)
{
    void *addr = (void *) utf->utf_fault_va;
    uint64_t blockno = ((uint64_t)addr - DISKMAP) / BLKSIZE;
    int r;

    // Check that the fault was within the block cache region
    if (addr < (void*)DISKMAP || addr >= (void*)(DISKMAP + DISKSIZE))
        panic("page fault in FS: eip %08x, va %08x, err %04x",
                utf->utf_rip, addr, utf->utf_err);

    // Sanity check the block number.
    if (super && blockno >= super->s_nblocks)
        panic("reading non-existent block %08x\n", blockno);

    // Allocate a page in the disk map region, read the contents
    // of the block from the disk into that page, and mark the
    // page not-dirty (since reading the data from disk will mark
    // the page dirty).
    //
    // LAB 5: Your code here
    r = sys_page_alloc(0, ROUNDDOWN(addr, PGSIZE), PTE_SYSCALL);
    if(r < 0) {
        panic("%s:%d %s() %e at addr %x", __FILE__, __LINE__, __func__, r, addr);
    }
#ifndef VMM_GUEST
    r = ide_read(blockno*BLKSECTS, ROUNDDOWN(addr,BLKSIZE), BLKSECTS);
#else
    r = host_read(blockno*BLKSECTS, ROUNDDOWN(addr,BLKSIZE), BLKSECTS);
#endif
    if(r < 0) {
        panic("%s:%d %s() %e at addr %x", __FILE__, __LINE__, __func__, r, addr);
    }

    r = sys_page_map(0, ROUNDDOWN(addr, PGSIZE), 0, ROUNDDOWN(addr, BLKSIZE), PTE_P | PTE_U | PTE_W);
    if(r < 0) {
        panic("%s:%d %s() %e at addr %x", __FILE__, __LINE__, __func__, r, addr);
    }

    // Check that the block we read was allocated. (exercise for
    // the reader: why do we do this *after* reading the block
    // in?)
    if (bitmap && block_is_free(blockno))
        panic("reading free block %08x\n", blockno);
}

// Flush the contents of the block containing VA out to disk if
// necessary, then clear the PTE_D bit using sys_page_map.
// If the block is not in the block cache or is not dirty, does
// nothing.
// Hint: Use va_is_mapped, va_is_dirty, and ide_write.
// Hint: Use the PTE_SYSCALL constant when calling sys_page_map.
// Hint: Don't forget to round addr down.
    void
flush_block(void *addr)
{
    uint64_t blockno = ((uint64_t)addr - DISKMAP) / BLKSIZE;

    if (addr < (void*)DISKMAP || addr >= (void*)(DISKMAP + DISKSIZE))
        panic("flush_block of bad va %08x", addr);

    // LAB 5: Your code here.
    if((va_is_mapped(addr)) && (va_is_dirty(addr))) {
#ifndef VMM_GUEST
        ide_write(blockno*BLKSECTS, ROUNDDOWN(addr,BLKSIZE), BLKSECTS);
#else
        host_write(blockno*BLKSECTS, ROUNDDOWN(addr, BLKSIZE), BLKSECTS);
#endif
    }

    if((va_is_mapped(addr))) {
        int r = sys_page_map(0, ROUNDDOWN(addr,PGSIZE), 0, ROUNDDOWN(addr,PGSIZE), PTE_SYSCALL&~PTE_D);
        if(r < 0) {
            panic("%s:%d %s() %e at addr %x", __FILE__, __LINE__, __func__, r, addr);
        }
    }
}

// Test that the block cache works, by smashing the superblock and
// reading it back.
    static void
check_bc(void)
{
    struct Super backup;

    memmove(&backup, diskaddr(1), sizeof backup);

    strcpy(diskaddr(1), "OOPS!\n");
    flush_block(diskaddr(1));
    assert(va_is_mapped(diskaddr(1)));
    assert(!va_is_dirty(diskaddr(1)));

    sys_page_unmap(0, diskaddr(1));
    assert(!va_is_mapped(diskaddr(1)));

        memmove(diskaddr(1), &backup, sizeof backup);
    flush_block(diskaddr(1));

    cprintf("block cache is good\n");
}

    void
bc_init(void)
{
    set_pgfault_handler(bc_pgfault);
    check_bc();
}

