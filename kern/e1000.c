#include <kern/e1000.h>
#include <inc/x86.h>
#include <inc/assert.h>
#include <inc/string.h>

int guest_rdt_head=0;
int guest_rdt_tail=0;

struct tx_desc
{
        uint64_t addr;
        uint16_t length;
        uint8_t  cso;
        uint8_t  cmd;
        uint8_t  status;
        uint8_t  css;
        uint16_t special;
};

struct rcv_desc
{
	uint64_t addr;
	uint16_t length;
	uint16_t pktchksum;
	uint8_t  status;
	uint8_t  error;
	uint16_t special;
};

void* offset2pointer(uint64_t offset)
{
        char *addr = (char*)pci_mmio;
        return addr+offset;
}


int e1000_transmit_packet(const char *data, size_t len)
{
	//cprintf("\n shashank :: transmit packet 1::\n");
	int i;
	static int myTail=0;
	struct tx_desc *td = (struct tx_desc*)transDespList;

	td += myTail;

	if (!(td->status & 0x01))
	{
		//          cprintf("error: descriptor queue is full, dropping packets and returning\n");
		return -1;
	}

	memcpy(KADDR(td->addr), data, len);
	td->length = len;
	myTail = (myTail+1)%TOTAL_TX_DESC;

	uint32_t *TDT = (uint32_t*)offset2pointer(0x03818);
	*TDT = myTail;
	return 0;
}

int guest_e1000_receive_packet(char *data, size_t *len)
{
	static int myHead=0;
	struct rcv_desc *rd = (struct rcv_desc*)guest_rcvDespList;
	int i;

	if (myHead == GUEST_TOTAL_RX_DESC)
		myHead = 0;

	rd += myHead;

	if (!(rd->status & 0x01)) {
		//	cprintf("error: data not received to descriptor buffer\n");
		return -1;
	}

	char *addr = (char*)(uint64_t*)(KADDR(rd->addr));
	*len = rd->length; 
	rd->status =0;
	for (i=0; i<rd->length; i++){
		data[i] = addr[i];
	}
	guest_rdt_tail=myHead;
	myHead++;
	return 0;
}

int e1000_receive_packet(char *data, size_t *len)
{
	static int myHead=0;
	struct rcv_desc *rd = (struct rcv_desc*)rcvDespList;
	struct rcv_desc *guest_rd = (struct rcv_desc*)guest_rcvDespList;
	int i;

	if (myHead == TOTAL_RX_DESC)
		myHead = 0;

	if (guest_rdt_head == GUEST_TOTAL_RX_DESC)
		guest_rdt_head = 0;

	rd += myHead;
	guest_rd += guest_rdt_head;

	guest_rd->status = rd->status;

	if (!(rd->status & 0x01)) {
		//                cprintf("error: data not received to descriptor buffer\n");
		return -1;
	}

	char *addr = (char*)(uint64_t*)(KADDR(rd->addr));
	char *guest_addr = (char*)(uint64_t*)(KADDR(guest_rd->addr));
	*len = rd->length; 
	guest_rd->status = rd->status;
	guest_rd->length= rd->length;
	rd->status =0;
	for (i=0; i<rd->length; i++){
		data[i] = addr[i];
		guest_addr[i] = addr[i];
	}

	uint32_t *RDT = (uint32_t*)offset2pointer(0x02818);
	*RDT = myHead;	

	myHead++;
	guest_rdt_head++;
	return 0;
}

int e1000_attach_func(struct pci_func *pcif)
{
	int i;

	pci_func_enable(pcif);

	pci_mmio = mmio_map_region((physaddr_t)pcif->reg_base[0], pcif->reg_size[0]);

	struct tx_desc *td = (struct tx_desc*)transDespList;
	for (i=0; i<TOTAL_TX_DESC; i++)
	{
		struct Page *pp = page_alloc(ALLOC_ZERO);
		td->addr = page2pa(pp);
		td->length = 0x2a;
		td->cso = 0;
		td->cmd = 0x09;
		td->status = 0x01;
		td->css = 0;
		td->special = 0;
		td++;
	}

	uint32_t *deviceStatusReg = (uint32_t*)offset2pointer(0x08);
	cprintf("e1000 status register = %x\n",*deviceStatusReg);

	uint32_t* TDBAH = (uint32_t*)offset2pointer(0x3804);
	uint32_t* TDBAL = (uint32_t*)offset2pointer(0x03800);
	*TDBAH = 0;
	*TDBAL = (uint64_t)PADDR(transDespList);

	uint32_t *TDLEN = (uint32_t*)offset2pointer(0x03808);
	*TDLEN = (16*TOTAL_TX_DESC); 

	uint32_t *TDH = (uint32_t*)offset2pointer(0x03810);
	uint32_t *TDT = (uint32_t*)offset2pointer(0x03818);
	*TDH = 0x0;
	*TDT = 0x0;

	uint32_t *TCTL =  (uint32_t*)offset2pointer(0x00400);
	*TCTL = 0x4010A;

	uint32_t *TGIP = (uint32_t*)offset2pointer(0x00410);
	*TGIP = 0x60200A;




	struct rcv_desc *rcv = (struct rcv_desc*)rcvDespList;
	for (i=0; i<TOTAL_RX_DESC; i++)
	{
		struct Page *pp = page_alloc(ALLOC_ZERO);
		rcv->addr = page2pa(pp);
		rcv->length = 0x0;
		rcv->pktchksum = 0;
		rcv->status = 0x0;
		rcv->error = 0;
		rcv->special = 0;
		rcv++;
	}

	struct rcv_desc *guest_rcv = (struct rcv_desc*)guest_rcvDespList;
	for (i=0; i<GUEST_TOTAL_RX_DESC; i++)
	{
		struct Page *pp = page_alloc(ALLOC_ZERO);
		guest_rcv->addr = page2pa(pp);
		guest_rcv->length = 0x0;
		guest_rcv->pktchksum = 0;
		guest_rcv->status = 0x0;
		guest_rcv->error = 0;
		guest_rcv->special = 0;
		guest_rcv++;
	}
	cprintf("\n initialing guest_rcvDespList end \n");

	uint32_t *RAL = (uint32_t*)offset2pointer(0x5400);
	uint32_t *RAH = (uint32_t*)offset2pointer(0x5404);
	*RAL = 0x12005452; // Magic code.
	*RAH = 0x80005634; // Magic code

	for (i=0; i<128; i++)
	{
		uint32_t *MTA = (uint32_t*)offset2pointer(0x5200 + (i*4));
		*MTA = 0x0;
	}


	uint32_t *RDBAH = (uint32_t*)offset2pointer(0x2804);
	uint32_t *RDBAL = (uint32_t*)offset2pointer(0x02800);
	*RDBAH = 0;
	*RDBAL = (uint64_t)PADDR(rcvDespList);

	uint32_t *RDLEN = (uint32_t*)offset2pointer(0x02808);
	*RDLEN = (16*TOTAL_RX_DESC); 

	uint32_t *RDH = (uint32_t*)offset2pointer(0x02810);
	uint32_t *RDT = (uint32_t*)offset2pointer(0x02818);
	*RDH = 0x0;
	*RDT = 55;

	uint32_t *RCTL =  (uint32_t*)offset2pointer(0x00100);
	*RCTL = 0x4008002; 

	return 1;
}
