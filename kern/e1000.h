#ifndef JOS_KERN_E1000_H
#define JOS_KERN_E1000_H

#include <kern/pci.h>
#include <kern/pmap.h>

volatile void *pci_mmio;

int e1000_transmit_packet(const char *data, size_t len);
int e1000_receive_packet(char *data, size_t *len);
int e1000_attach_func(struct pci_func *pcif);
int guest_e1000_receive_packet(char *data, size_t *len);

#endif	// JOS_KERN_E1000_H
