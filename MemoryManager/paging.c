#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "simos.h"



// Memory definitions, including the memory itself and a page structure
// that maintains the informtion about each memory page
// config.sys input: pageSize, numFrames, OSpages
// ------------------------------------------------
// process page table definitions
// config.sys input: loadPpages, maxPpages

mType *Memory;   // The physical memory, size = pageSize*numFrames

typedef unsigned ageType;
typedef struct
{ int pid, page;   // the frame is allocated to process pid for page page
  ageType age;
  char free, dirty, pinned;   // in real systems, these are bits
  int next, prev;
} FrameStruct;

FrameStruct *memFrame;   // memFrame[numFrames]
int freeFhead, freeFtail;   // the head and tail of free frame list

// define special values for page/frame number
#define nullIndex -1   // free frame list null pointer
#define nullPage -1   // page does not exist yet
#define diskPage -2   // page is on disk swap space
#define pendingPage -3  // page is pending till it is actually swapped
   // have to ensure: #memory-frames < address-space/2, (pageSize >= 2)
   //    becuase we use negative values with the frame number
   // nullPage & diskPage are used in process page table 

// define values for fields in FrameStruct
#define zeroAge 0x00000000
#define highestAge 0x80
#define dirtyFrame 1
#define cleanFrame 0
#define freeFrame 1
#define usedFrame 0
#define pinnedFrame 1
#define nopinFrame 0

// define shifts and masks for instruction and memory address 
#define opcodeShift 24
#define operandMask 0x00ffffff

// shift address by pagenumShift bits to get the page number
unsigned pageoffsetMask;
int pagenumShift; // 2^pagenumShift = pageSize

//============================
// Our memory implementation is a mix of memory manager and physical memory.
// get_instr, put_instr, get_data, put_data are the physical memory operations
//   for instr, instr is fetched into registers: IRopcode and IRoperand
//   for data, data is fetched into registers: MBR (need to retain AC value)
// page table management is software implementation
//============================


//==========================================
// run time memory access operations, called by cpu.c
//==========================================

// define rwflag to indicate whehter the addr computation is for read or write
#define flagRead 1
#define flagWrite 2

#define gdata 1
#define ginstr 2

int pfpage;

// address calcuation are performed for the program in execution
// so, we can get address related infor from CPU registers

int calculate_memory_address (unsigned offset, int rwflag)
{ 
  // rwflag is used to differentiate the caller
  // different access violation decisions are made for reader/writer
  // if there is a page fault, need to set the page fault interrupt
  // also need to set the age and dirty fields accordingly
  // returns memory address or mPFault or mError

	int pageNumber = offset/pageSize;
	int frameOffset = offset%pageSize;

	if(pageNumber > maxPpages){
		return mError;
	}

	if(PCB[CPU.Pid]->PTptr[pageNumber] == diskPage){
		set_interrupt (pFaultException);
		return mPFault;
	}

	if(PCB[CPU.Pid]->PTptr[pageNumber] == nullPage && rwflag == flagRead){
		return mError;
	}else if(PCB[CPU.Pid]->PTptr[pageNumber] == nullPage && rwflag == flagWrite){
		set_interrupt (pFaultException);
		return mPFault;
	}

	int addr = (frameOffset & pageoffsetMask) | (PCB[CPU.Pid]->PTptr[pageNumber] << pagenumShift);
	return addr;
}


int get_data (int offset)
{ 
  // call calculate_memory_address to get memory address
  // copy the memory content to MBR
  // return mNormal, mPFault or mError
	  int maddr;
	  maddr = calculate_memory_address(offset, flagRead);
	  if (maddr == mError) return (mError);
	  else if (maddr == mPFault) {
		  pfpage = gdata;
		  return (mPFault);
	  }
	  else
	  {
		memFrame[PCB[CPU.Pid]->PTptr[offset/pageSize]].age = highestAge;
		CPU.MBR = Memory[maddr].mData;
		return (mNormal);
	  }
}


int put_data (int offset)
{ 
  // call calculate_memory_address to get memory address
  // copy MBR to memory 
  // return mNormal, mPFault or mError
	  int maddr;
	  maddr = calculate_memory_address(offset, flagWrite);
	  if (maddr == mError) return (mError);
	  else if (maddr == mPFault){
		  CPU.exeStatus = ePFault;
		  pfpage = gdata;
		  return (mPFault);
	  }
	  else
	  { memFrame[PCB[CPU.Pid]->PTptr[offset/pageSize]].dirty = dirtyFrame;
		memFrame[PCB[CPU.Pid]->PTptr[offset/pageSize]].age = highestAge;
		Memory[maddr].mData = CPU.AC;
		return (mNormal);
	  }
}


int get_instruction (int offset)
{ 
  // call calculate_memory_address to get memory address
  // convert memory content to opcode and operand
  // return mNormal, mPFault or mError

	int maddr, instr;
	maddr = calculate_memory_address(offset, flagRead);

	if (maddr == mError) return (mError);
	else if (maddr == mPFault){
		pfpage = ginstr;
		return (mPFault);
	}
    else
    {
      memFrame[PCB[CPU.Pid]->PTptr[offset/pageSize]].age = highestAge;
      instr = Memory[maddr].mInstr;
	  CPU.IRopcode = instr >> opcodeShift;
	  CPU.IRoperand = instr & operandMask;
	  return (mNormal);
    }
}

// these two direct_put functions are only called for loading idle process
// no specific protection check is done
void direct_put_instruction (int findex, int offset, int instr)
{ int addr = (offset & pageoffsetMask) | (findex << pagenumShift);
  Memory[addr].mInstr = instr;
}

void direct_put_data (int findex, int offset, mdType data)
{ int addr = (offset & pageoffsetMask) | (findex << pagenumShift);
  Memory[addr].mData = data;
}

//==========================================
// Memory and memory frame management
//==========================================

void dump_one_frame (int findex)
{ int i;
  // dump the content of one memory frame
	printf("start-end:%d,%d:",findex*pageSize,((findex*pageSize)+pageSize));
	for (i=0; i<pageSize; i++){
		int addr = (i & pageoffsetMask) | (findex << pagenumShift);
		printf(" %x",Memory[addr]);
	}
	printf("\n\n");

}

void dump_memory ()
{ int i;

  printf ("************ Dump the entire memory\n");
  for (i=0; i<numFrames; i++) dump_one_frame (i);
}

// above: dump memory content, below: only dump frame infor

void dump_free_list ()
{ 
  // dump the list of free memory frames
	printf ("******************** Memory Free Frame	Dump\n");
	int i = freeFhead;
	int j = freeFtail;
	int count = 0;
	while(1){
		if(i == j){
			if(i!= nullIndex){
				printf("%d, ",i);
			}
			count++;
			if(count == pageSize){
				printf("\n");
			}
			break;
		}
		if(count == pageSize){
			printf("\n");
		}
		printf("%d, ",i);
		count++;
		i = memFrame[i].next ;
	}
	printf("\n");
}

void print_one_frameinfo (int indx)
{ printf ("pid/page/age=%d,%d,%x, ",
          memFrame[indx].pid, memFrame[indx].page, memFrame[indx].age);
  printf ("dir/free/pin=%d/%d/%d, ",
          memFrame[indx].dirty, memFrame[indx].free, memFrame[indx].pinned);
  printf ("next/prev=%d,%d\n",
          memFrame[indx].next, memFrame[indx].prev);
}

void dump_memoryframe_info ()
{ int i;

  printf ("******************** Memory Frame Metadata\n");
  printf ("Memory frame head/tail: %d/%d\n", freeFhead, freeFtail);
  for (i=OSpages; i<numFrames; i++)
  { printf ("Frame %d: ", i); print_one_frameinfo (i); }
  dump_free_list ();
}


void  update_frame_info (findex, pid, page)
int findex, pid, page;
{
  // update the metadata of a frame, need to consider different update scenarios
  // need this function also becuase loader also needs to update memFrame fields
  // while it is better to not to expose memFrame fields externally
	memFrame[findex].pid = pid;
	memFrame[findex].age = highestAge;
	memFrame[findex].page = page;
	memFrame[findex].dirty = cleanFrame;
	memFrame[findex].free = usedFrame;
}

// should write dirty frames to disk and remove them from process page table
// but we delay updates till the actual swap (page_fault_handler)
// unless frames are from the terminated process (status = nullPage)
// so, the process can continue using the page, till actual swap

void addto_free_frame (int findex, int status)
{
	if (status == nullPage) {
		memFrame[findex].age = zeroAge;
		memFrame[findex].dirty = cleanFrame;
		memFrame[findex].free = freeFrame;
		memFrame[findex].pinned = nopinFrame;
		memFrame[findex].pid = nullPid;
		memFrame[findex].page = nullPage;
	} else {
		memFrame[findex].age = zeroAge;
		memFrame[findex].free = freeFrame;
		memFrame[findex].pinned = nopinFrame;
	}

	if(freeFhead == nullIndex){
		freeFhead = findex;
		if (freeFtail == nullIndex) {
			freeFtail = findex;
		}
	}else{
		memFrame[freeFtail].next = findex;
		memFrame[findex].prev = freeFtail;
		memFrame[findex].next = nullIndex;
		freeFtail = findex;
	}



	printf("Added free frame = %d\n",findex);
}


int select_agest_frame ()
{ 
  // select a frame with the lowest age 
  // if there are multiple frames with the same lowest age, then choose the one
  // that is not dirty
	int i;
	int cleanCounter = 0;
	int j;
	int arbitraryFrame;
	int lowestAge = memFrame[OSpages].age;

	for (i = OSpages+1; i< numFrames; i++) {
		if (memFrame[i].age < lowestAge) {
			lowestAge = memFrame[i].age;
		}
	}


	for (j = OSpages; j < numFrames; j++) {
		if (memFrame[j].age == lowestAge && memFrame[j].dirty == cleanFrame) {
			cleanCounter++;
			if (cleanCounter == 1) {
				arbitraryFrame = j;
			}else{
				addto_free_frame(j, pendingPage);
			}
		}
	}

	if (cleanCounter == 0) {
		for (j = OSpages; j < numFrames; j++) {
			if (memFrame[j].age == lowestAge) {
				//addto_free_frame(j, pendingPage);
				arbitraryFrame = j;
				break;
			}
		}
	}
	printf("Selected agest frame = %d, age = %x, dirty = %d\n", arbitraryFrame, memFrame[arbitraryFrame].age, memFrame[arbitraryFrame].dirty);
	return arbitraryFrame;
}


int get_free_frame ()
{ 
// get a free frame from the head of the free list 
// if there is no free frame, then get one frame with the lowest age
// this func always returns a frame, either from free list or get one with lowest age

//	int i;
//	for (i = OSpages;  i < numFrames; i++) {
//		if(memFrame[i].free == freeFrame){
//			return i;
//		}
//	}
//	return select_agest_frame();
	int i;

	if(freeFhead == nullIndex && freeFtail == nullIndex){
		return select_agest_frame();
	}else{
		if(memFrame[freeFhead].prev == nullIndex && memFrame[freeFhead].next != nullIndex){
			i = freeFhead;
			freeFhead = memFrame[freeFhead].next;
			memFrame[i].next = nullIndex;
			memFrame[freeFhead].prev = nullIndex;

			if(memFrame[i].age != zeroAge){
				printf("============= Frame got used after freed %d\n",i);
				printf("Selected agest frame = %d, age %x, dirty %d\n",i, memFrame[i].age, memFrame[i].dirty);
			}

			return i;
		}else if(memFrame[freeFhead].prev == nullIndex && memFrame[freeFhead].next == nullIndex){
			i = freeFhead;
			freeFhead = nullIndex;
			freeFtail = nullIndex;
			if(memFrame[i].age != zeroAge){
				printf("============= Frame got used after freed %d\n",i);
				printf("Selected agest frame = %d, age %x, dirty %d\n",i, memFrame[i].age, memFrame[i].dirty);
			}
			return i;
		}
	}

} 


void initialize_memory ()
{ int i;

  // create memory + create page frame array memFrame 
  Memory = (mType *) malloc (numFrames*pageSize*sizeof(mType));
  memFrame = (FrameStruct *) malloc (numFrames*sizeof(FrameStruct));

  // compute #bits for page offset, set pagenumShift and pageoffsetMask
  // *** ADD CODE

  	  pagenumShift = log2(pageSize);
  	  pageoffsetMask = (pageSize*numFrames)-1;

  // initialize OS pages
  for (i=0; i<OSpages; i++)
  { memFrame[i].age = zeroAge;
    memFrame[i].dirty = cleanFrame;
    memFrame[i].free = usedFrame;
    memFrame[i].pinned = pinnedFrame;
    memFrame[i].pid = osPid;
  }
  // initilize the remaining pages, also put them in free list
  // *** ADD CODE

  for (i=OSpages; i<numFrames; i++)
    { memFrame[i].age = zeroAge;
      memFrame[i].dirty = cleanFrame;
      memFrame[i].free = freeFrame;
      memFrame[i].pinned = nopinFrame;
      memFrame[i].pid = nullPid;
      memFrame[i].page = nullPage;
      memFrame[i].next = i+1;
      if( i == numFrames-1 ){
    	  memFrame[i].next = -1;
      }
      memFrame[i].prev = i-1;
      if( i == OSpages ){
    	  memFrame[i].prev = -1;
      }
    }

  freeFhead = OSpages;
  freeFtail = numFrames-1;
}

//==========================================
// process page table manamgement
//==========================================

void init_process_pagetable (int pid)
{ int i;

  PCB[pid]->PTptr = (int *) malloc (addrSize*maxPpages);
  for (i=0; i<maxPpages; i++) PCB[pid]->PTptr[i] = nullPage;
}

// frame can be normal frame number or nullPage, diskPage

void update_process_pagetable (pid, page, frame)
int pid, page, frame;
{ 
  // update the page table entry for process pid to point to the frame
  // or point to disk or null
	printf("PT update for (%d,%d) to %d\n",pid,page,frame);
	PCB[pid]->PTptr[page] = frame;
}


int free_process_memory (int pid)
{ 
  // free the memory frames for a terminated process
  // some frames may have already been freed, but still in process pagetable
	printf("Free frames allocated to process %d\n",pid);
	int i;
	for (i=0; i<maxPpages; i++){
		if(PCB[pid]->PTptr[i] != nullPage){
			if(PCB[pid]->PTptr[i] !=diskPage){
				if(memFrame[PCB[pid]->PTptr[i]].free != freeFrame){
					addto_free_frame(PCB[pid]->PTptr[i], nullPage);
				}
			}
			PCB[pid]->PTptr[i] = nullPage;
		}
	}
	return pid;
}


void dump_process_pagetable (int pid)
{ 
  // print page table entries of process pid
	printf ("******************** Page Table Dump for Process %d\n",pid);
	int i;
	int count = 0;
	for (i=0; i<maxPpages; i++){
		if(count == pageSize){
			printf("\n");
		}
		printf("%d, ",PCB[pid]->PTptr[i]);
		count++;
	}
	printf("\n");
}


void dump_process_memory (int pid)
{ 
  // print out the memory content for process pid
	int i;
	printf("******************** Memory Dump for Process %d\n",pid);
	for (i=0; i<maxPpages; i++){
			if(PCB[pid]->PTptr[i] != nullPage && PCB[pid]->PTptr[i] !=diskPage){
					printf("***P/F:%d,%d: ",i,PCB[pid]->PTptr[i]);
					print_one_frameinfo(PCB[pid]->PTptr[i]);
					dump_one_frame(PCB[pid]->PTptr[i]);
			}else if(PCB[pid]->PTptr[i] == diskPage){
					printf("***P/F:%d,%d: ",i,diskPage);
					dump_process_swap_page(pid,i);
			}else if(PCB[pid]->PTptr[i] == nullPage){
				printf("***P/F:%d,%d: \n",i,nullPage);
			}
		}

}

//==========================================
// the major functions for paging, invoked externally
//==========================================

#define sendtoReady 1  // has to be the same as those in swap.c
#define notReady 0   
#define actRead 0   
#define actWrite 1


void page_fault_handler ()
{ 
  // handle page fault
  // obtain a free frame or get a frame with the lowest age
  // if the frame is dirty, insert a write request to swapQ 
  // insert a read request to swapQ to bring the new page to this frame
  // update the frame metadata and the page tables of the involved processes

	int availableFrame = get_free_frame();
	printf("Got free frame = %d\n",availableFrame);
	dump_memoryframe_info();
	int i = 0;
	int id = -1;
	int pageno = -1;
	if(memFrame[availableFrame].dirty == dirtyFrame){
		int addr = (i & pageoffsetMask) | (availableFrame << pagenumShift);
		update_process_pagetable(memFrame[availableFrame].pid, memFrame[availableFrame].page, diskPage);
		insert_swapQ(memFrame[availableFrame].pid, memFrame[availableFrame].page, &Memory[addr], actWrite, Nothing);
		//printf("Page Fault Handler: pid/page=(%d,%d)\n",memFrame[availableFrame].pid,memFrame[availableFrame].page);
		id = memFrame[availableFrame].pid;
		pageno = memFrame[availableFrame].page;
	}
	int addr = (i & pageoffsetMask) | (availableFrame << pagenumShift);

	if(memFrame[availableFrame].pid != nullPid){
		update_process_pagetable(memFrame[availableFrame].pid, memFrame[availableFrame].page, diskPage);
		id = memFrame[availableFrame].pid;
		pageno = memFrame[availableFrame].page;
	}

	if(pfpage == ginstr){
		update_frame_info(availableFrame, CPU.Pid, CPU.PC/pageSize);
		update_process_pagetable(CPU.Pid, CPU.PC/pageSize, availableFrame);
		PCB[CPU.Pid]->numPF += 1 ;
		insert_swapQ(CPU.Pid, CPU.PC/pageSize, &Memory[addr], actRead, toReady);
		printf("Swap_in: in=(%d,%d,%x), out=(%d,%d,%x), m=%x\n",CPU.Pid,CPU.PC/pageSize,&Memory[addr],id,pageno,&Memory[addr],&Memory[0]);
		printf("Page Fault Handler: pid/page=(%d,%d)\n",CPU.Pid,CPU.PC/pageSize);
	}else if(pfpage == gdata){
		update_frame_info(availableFrame, CPU.Pid, CPU.IRoperand/pageSize);
		update_process_pagetable(CPU.Pid, CPU.IRoperand/pageSize, availableFrame);
		PCB[CPU.Pid]->numPF += 1 ;
		insert_swapQ(CPU.Pid, CPU.IRoperand/pageSize, &Memory[addr], actRead, toReady);
		printf("Swap_in: in=(%d,%d,%x), out=(%d,%d,%x), m=%x\n",CPU.Pid,CPU.IRoperand/pageSize,&Memory[addr],id,pageno,&Memory[addr],&Memory[0]);
		printf("Page Fault Handler: pid/page=(%d,%d)\n",CPU.Pid,CPU.IRoperand/pageSize);
	}
}

// scan the memory and update the age field of each frame
void memory_agescan ()
{ 
	int i;
	int count = 0;
	for (i = OSpages; i < numFrames; ++i) {
		memFrame[i].age = memFrame[i].age >> 1;
		if (memFrame[i].age == zeroAge && memFrame[i].free != freeFrame) {
			addto_free_frame(i, pendingPage);
			count++;
		}
	}

	if(count > 0 ){
		printf("Some frames got freed during age scan\n");
		dump_memoryframe_info();
	}
}


void initialize_memory_manager ()
{ 
  // initialize memory and add page scan event request
	initialize_memory();
	add_timer (periodAgeScan, osPid, actAgeInterrupt, periodAgeScan);
}


void initial_page_loading(int pid, int pagesToLoad){

	int i;
	int j = 0;

	dump_process_pagetable(pid);
	for (i = 0; i < pagesToLoad; i++) {
		//mType *buf = (mType *) malloc (pageSize*sizeof(mType));
		int availableFrame = get_free_frame();
		printf("Got free frame = %d\n",availableFrame);
		if(i == pagesToLoad - 1){
			dump_memoryframe_info();
			update_frame_info(availableFrame, pid, i);
			update_process_pagetable (pid, i, availableFrame);
			insert_swapQ (pid, i, &Memory[availableFrame*pageSize], actRead, toReady);
		}else{
			dump_memoryframe_info();
			update_frame_info(availableFrame, pid, i);
			update_process_pagetable (pid, i, availableFrame);
			insert_swapQ (pid, i, &Memory[availableFrame*pageSize], actRead, Nothing);
		}
		printf("Swap_in: in=(%d,%d,%x), out=(%d,%d,%x), m=%x\n",pid,i,&Memory[availableFrame*pageSize],nullIndex,nullIndex,&Memory[availableFrame*pageSize],&Memory[0]);
	}

}
