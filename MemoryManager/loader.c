#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "simos.h"


// need to be consistent with paging.c: mType and constant definitions
#define opcodeShift 24
#define operandMask 0x00ffffff
#define diskPage -2

FILE *progFd;

//==========================================
// load program into memory and build the process, called by process.c
// a specific pid is needed for loading, since registers are not for this pid
//==========================================


// may return progNormal or progError (the latter, if the program is incorrect)
int load_instruction (mType *buf, int page, int offset)
{ 
  // load instruction to buffer
	int opcode = buf[offset].mInstr >> opcodeShift;
	if(Debug) printf("Load instruction(%x,%d) into M(%d,%d)\n", opcode << opcodeShift, buf[offset].mInstr & operandMask, page, offset);
	return progNormal;
}


int load_data (mType *buf, int page, int offset)
{ 
  // load data to buffer (same as load instruction, but use the mData field
	if(Debug) printf("Load data: %.2f into M(%d,%d)\n",buf[offset].mData, page, offset);
	return progNormal;
}


// load program to swap space, returns the #pages loaded
int load_process_to_swap (int pid, char *fname)
{ 
  // read from program file "fname" and call load_instruction & load_data
  // to load the program into the buffer, write the program into
  // swap space by inserting it to swapQ
  // update the process page table to indicate that the page is not empty
  // and it is on disk (= diskPage)

	  FILE *fprog;
	  int msize, numinstr, numdata;
	  int ret, i, j, opcode, operand;
	  int count = 0;
	  float data;

	init_process_pagetable (pid);
	fprog = fopen (fname, "r");
	  if (fprog == NULL)
	  { printf ("Submission Error: Incorrect program name: %s!\n", fname);
	    return progError;
	  }
	  ret = fscanf (fprog, "%d %d %d\n", &msize, &numinstr, &numdata);
	  if (ret < 3)   // did not get all three inputs
	  { printf ("Submission failure: missing %d program parameters!\n", 3-ret);
	    return progError;
	  }


	  printf("Program info: %d %d %d\n",msize,numinstr,numdata);
	  int requiredPages = ceil((1.0*msize)/(pageSize));

	  if(requiredPages > maxPpages){
		  return progError;
	  }else{
		  for (i = 0; i < requiredPages; i++) {
			  mType *buf = (mType *) malloc (pageSize*sizeof(mType));
				memset(buf, 0, sizeof(buf)*sizeof(int));
			  int offset = 0;
			  for (j = 0; j < pageSize; ++j) {
				  if(count == msize){
					  break;
				  }
				  if(count < numinstr){
					  fscanf (fprog, "%d %d\n", &opcode, &operand);
					  opcode = opcode << opcodeShift;
					  operand = operand & operandMask;
					  buf[j].mInstr = opcode | operand;
					  load_instruction(buf, i, offset);
					  offset++;
					  count++;

				  }else{
					  fscanf (fprog, "%f\n", &data);
					  buf[j].mData = data;
					  load_data(buf, i, offset);
					  offset++;
					  count++;
				  }

		  }
			  update_process_pagetable (pid, i, diskPage);
			  insert_swapQ (pid, i, buf, actWrite, freeBuf);
		}
		 return requiredPages;
	  }
}


int load_pages_to_memory (int pid, int numpage)
{
  // call insert_swapQ to load the pages of process pid to memory
  // #pages to load = min (loadPpages, numpage = #pages loaded to swap for pid)
  // ask swap.c to place the process to ready queue only after the last load
  // do not forget to update the page table of the process
  // this function has some similarity with page fault handler

	int pagesToLoad = (loadPpages < numpage) ? loadPpages : numpage;
	printf("Load %d pages for process %d\n",numpage,pid);
	initial_page_loading(pid, pagesToLoad);
	return pagesToLoad;
}

int load_process (int pid, char *fname)
{ int ret;

  ret = load_process_to_swap (pid, fname);   // return #pages loaded
  if (ret != progError){
	  ret = load_pages_to_memory (pid, ret);
  }
  return (ret);
}

// load idle process, idle process uses OS memory
// We give the last page of OS memory to the idle process
#define OPifgo 5   // has to be consistent with cpu.c
void load_idle_process ()
{ int page, frame;
  int instr, opcode, operand, data;

  init_process_pagetable (idlePid);
  page = 0;   frame = OSpages - 1;
  update_process_pagetable (idlePid, page, frame);
  update_frame_info (frame, idlePid, page);
  
  // load 1 ifgo instructions (2 words) and 1 data for the idle process
  opcode = OPifgo;   operand = 0;
  instr = (opcode << opcodeShift) | operand;
  direct_put_instruction (frame, 0, instr);   // 0,1,2 are offset
  direct_put_instruction (frame, 1, instr);
  direct_put_data (frame, 2, 1);
}

