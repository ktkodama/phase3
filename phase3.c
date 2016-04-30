//David Smith
//Kevin Kodama

/*
 * skeleton.c
 *
 *  This is a skeleton for phase3 of the programming assignment. It
 *  doesn't do much -- it is just intended to get you started. Feel free 
 *  to ignore it.
 */
 
#include <assert.h>
#include <phase3.h>
#include <string.h>
#include <libuser.h>
 
/*
 * Everybody uses the same tag.
 */
#define TAG 0
/*
 * Page table entry
 */
 
#define UNUSED  0
#define INCORE  1
#define ONDISK  2
/* You'll probably want more states */

/*kill message*/
#define KILLSELF 424242
 
/* Page Table Entry */
typedef struct PTE {
    int     state;      /* See above. */
    int     frame;      /* The frame that stores the page. */
    int     block;      /* The disk block that stores the page. */
    /* Add more stuff here */
} PTE;
 
/*
 * Per-process information
 */
typedef struct Process {
    int     numPages;   /* Size of the page table. */
    PTE     *pageTable; /* The page table for the process. */
	P1_Semaphore	semPTE;
    /* Add more stuff here if necessary. */
} Process;
 
/*
 * Information about page faults.
 */
typedef struct Fault {
    int     pid;        /* Process with the problem. */
    void    *addr;      /* Address that caused the fault. */
    int     mbox;       /* Where to send reply. */
    /* Add more stuff here if necessary. */
} Fault;

typedef struct Frame {
	int 	page;			/* Page*/
	int 	pid;			/* Process id*/
	int 	state;			/* */

} Frame;

typedef struct Block {
	int 	pid;
	
} Block;

static Frame *frmTable;
static Block *diskBlock;

static void *vmRegion = NULL;
 
P3_VmStats  P3_vmStats;
 
static int pagerMbox = -1;

 
int p4_pid;
int p4_return_pid;
int* p4_pid_ptr = &p4_pid;
int sectorsPerBlock;  //USLOSS_MmuPageSize() / USLOSS_DISK_SECTOR_SIZE;

static Process  processes[P1_MAXPROC];

static int  numPages = 0;
static int  numFrames = 0;
static int clockPos = 0; 	//the position of the clock hand;
static int diskUnit = 1;
//static int usedBlocks = 0;

int sectorSize;
int numSectorPerTrack;
int numTracks;	//total tracks on the disk
int numBlocks;	//total disk blocks that store a individual frame


P1_Semaphore semFreeFrame;	//protect frame table
P1_Semaphore semPageTable;	//protect entire page table
P1_Semaphore semMutex;	//global
P1_Semaphore semP3_VmStats;
P1_Semaphore semClockPos;	//protect runClockAlgo method
P1_Semaphore semDiskBlock;	//protect diskBlock table
P1_Semaphore semDiskIO;		//protect diskIO


static void CheckPid(int);
static void CheckMode(void);
static void FaultHandler(int type, void *arg);
static int Pager(void *arg);
void    P3_Fork(int pid);
void    P3_Switch(int old, int new);
void    P3_Quit(int pid);

static int findFreeFrame(int, int);
static int runClockAlgo(int currPID, int) ;
static void writeOldPage(int, int, int, void*); 
static void writeNewPage(int, int, int, void*); 
static int findDiskBlock(int);

//////////////////////////////////////////////////////////// David's
static int pagerIdTable[P3_MAX_PAGERS];  
////////////////////////////////////////////////////////////Addition
/*
 *----------------------------------------------------------------------
 *
 * P3_VmInit --
 *
 *  Initializes the VM system by configuring the MMU and setting
 *  up the page tables.
 *  
 * Results:
 *      MMU return status
 *
 * Side effects:
 *      The MMU is initialized.
 *
 *----------------------------------------------------------------------
 */
int
P3_VmInit(int mappings, int pages, int frames, int pagers)
{
    int     status;
    int     i;
    int     tmp;
	
	semFreeFrame = P1_SemCreate(1);
	semPageTable = P1_SemCreate(1);
	semMutex = P1_SemCreate(1);
	semP3_VmStats = P1_SemCreate(1);
	semClockPos = P1_SemCreate(1);
	semDiskBlock = P1_SemCreate(1);	
	semDiskIO = P1_SemCreate(1);
	
    CheckMode();
	
	//error checking prior to MMU INIT
	if (mappings <= 0  || pages <= 0 || frames <= 0 ) {
		return -1;
	}
	if (mappings != pages) {
		return -1;
	}
	//////******************************************************************************
	
	//moving this ***BEFORE*** the forking of the pager to try to solve glibc error
	memset((char *) &P3_vmStats, 0, sizeof(P3_VmStats));
    P3_vmStats.pages = pages;
    P3_vmStats.frames = frames;
	P3_vmStats.freeFrames = frames;
	P3_vmStats.blocks = numBlocks;
	P3_vmStats.freeBlocks = numBlocks;
	//assert(P3_vmStats.freeBlocks==200);
    numPages = pages;
    numFrames = frames;
	//////******************************************************************************	
	
    status = USLOSS_MmuInit(mappings, pages, frames);
    if (status != USLOSS_MMU_OK) {
       USLOSS_Console("P3_VmInit: couldn't initialize MMU, status %d\n", status);
       //USLOSS_Halt(1);
	   if (status == USLOSS_MMU_ERR_ON) {
		   return -2;
	   }
    }
    vmRegion = USLOSS_MmuRegion(&tmp);
	//assert(vmRegion != NULL);
	//assert(tmp >= pages);
    USLOSS_IntVec[USLOSS_MMU_INT] = FaultHandler;
    for (i = 0; i < P1_MAXPROC; i++) {
        processes[i].numPages = 0;
        processes[i].pageTable = NULL;
		processes[i].semPTE = P1_SemCreate(1);
    }
	
	//create Frame table
	P1_P(semFreeFrame);
	
	frmTable = (Frame *) malloc(frames * sizeof(Frame));
	memset(frmTable, 0, frames * sizeof(Frame));
	for(i=0;i<numFrames;i++) {
		frmTable[i].pid = -1;
	}
	
	P1_V(semFreeFrame);
	
	//create diskBlock table 
	
	P1_P(diskBlock);
	
	diskBlock = (Block *) malloc(numBlocks * sizeof(Block));
	memset(diskBlock, 0, numBlocks * sizeof(Block));
	for(i=0;i<numBlocks;i++) {
		diskBlock[i].pid = -1;
	}
	
	P1_V(diskBlock);
	
    /*
     * Create the page fault mailbox and fork the pagers here.
     */
	//
	 pagerMbox = P2_MboxCreate(P1_MAXPROC, sizeof(Fault));
	 
	////////////////////////////////////////////////////////////David's
	
	 for(i = 0; i < 1; i++){		//P3_MAX_PAGERS
	 //for(i = 0; i < P3_MAX_PAGERS; i++){		//
		char pagersName[20];
		snprintf(pagersName, sizeof(pagersName), "Pager %d", i);
		pagerIdTable[i] = P1_Fork(pagersName, Pager, NULL, USLOSS_MIN_STACK, 2);
	} 
	
	////////////////////////////////////////////////////////////Addition 
    
    return 0;
}
/*
 *----------------------------------------------------------------------
 *
 * P3_VmDestroy --
 *
 *  Frees all of the global data structures
 *  
 * Results:
 *      None
 *
 * Side effects:
 *      The MMU is turned off.
 *
 *----------------------------------------------------------------------
 */
void
P3_VmDestroy(void)
{
    int status;
	
	CheckMode();
    status = USLOSS_MmuDone();
	if (status==USLOSS_MMU_ERR_OFF) {
		return;
	}
	
	Fault killFault;
	int killMsgSize = sizeof(killFault);
	
    /*
     * Kill the pagers here.
     */
	
	killFault.pid = KILLSELF;
	status = P2_MboxSend(pagerMbox, &killFault, &killMsgSize); 
	 
    /* 
     * Print vm statistics.
     */
    USLOSS_Console("P3_vmStats:\n");
    USLOSS_Console("pages: %d\n", P3_vmStats.pages);
    USLOSS_Console("frames: %d\n", P3_vmStats.frames);
    USLOSS_Console("blocks: %d\n", P3_vmStats.blocks);
	USLOSS_Console("freeFrames: %d\n", P3_vmStats.freeFrames);
	USLOSS_Console("freeBlocks: %d\n", P3_vmStats.freeBlocks);
	USLOSS_Console("switches: %d\n", P3_vmStats.switches);
	USLOSS_Console("faults: %d\n", P3_vmStats.faults);
	USLOSS_Console("new: %d\n", P3_vmStats.new);
	USLOSS_Console("pageIns: %d\n", P3_vmStats.pageIns);
	USLOSS_Console("pageOuts: %d\n", P3_vmStats.pageOuts);
	USLOSS_Console("replaced: %d\n", P3_vmStats.replaced);
	   
}
 
/*
 *----------------------------------------------------------------------
 *
 * P3_Fork --
 *
 *  Sets up a page table for the new process.
 *
 * Results:
 *  None.
 *
 * Side effects:
 *  A page table is allocated.
 *
 *----------------------------------------------------------------------
 */
void
P3_Fork(pid)
    //int     pid;        /* New process */
{
    int     i;
	CheckMode();
    CheckPid(pid);
	
	P1_P(semPageTable);
	
    processes[pid].numPages = numPages;
	processes[pid].pageTable = (PTE *) malloc(sizeof(PTE) * numPages);
    for (i = 0; i < numPages; i++) {
        processes[pid].pageTable[i].frame = -1;
        processes[pid].pageTable[i].block = -1;
        processes[pid].pageTable[i].state = UNUSED;
    }
	
	P1_V(semPageTable);
	
}
 
/*
 *----------------------------------------------------------------------
 *
 * P3_Quit --
 *
 *  Called when a process quits and tears down the page table 
 *  for the process and frees any frames and disk space used
 *      by the process.
 *
 * Results:
 *  None
 *
 * Side effects:
 *  None.
 *
 *----------------------------------------------------------------------
 */
void
P3_Quit(pid)
    
{
    CheckMode();
    CheckPid(pid);
	
	int 	i;
	int 	frmNum;	//frame number to be reused
	
	/* 
     * Free any of the process's pages that are on disk and free any page frames the
     * process is using.
     */
	//P1_DumpProcesses(); 
	//;;
	if (pid == 19) {
		//USLOSS_Console("pid quit: %d\n", pid);
		//P1_DumpProcesses();
	}
	//
	
	if (processes[pid].pageTable != NULL && processes[pid].numPages > 0) { 
		
		//88888888888   CHANGE THIS BACK  //8888888888888888888888888
		//for (i=0;i<numPages;i++) {
		
		
		for (i=0;i<processes[pid].numPages;i++) {
			frmNum = processes[pid].pageTable[i].frame;
		
			P1_P(semFreeFrame);
		
			//check for a valid frame number.  If so update the frmTable and the PTE
			if (frmNum > -1 && frmNum < numFrames) {
				
				
				frmTable[frmNum].page = -1;
				frmTable[frmNum].pid = -1;
				frmTable[frmNum].state = UNUSED;
				
				USLOSS_MmuUnmap(TAG,i);
				
				P1_P(semPageTable);
				
				processes[pid].pageTable[i].frame = -1;
				processes[pid].pageTable[i].frame = UNUSED;
				processes[pid].pageTable[i].block = -1;
				
				P1_V(semPageTable);
			
				P1_P(semP3_VmStats);
				//if (P3_vmStats.freeFrames <= frames) {
					P3_vmStats.freeFrames++;	
				//}
				
				P1_V(semP3_VmStats);
			
			}
			P1_V(semFreeFrame);
		}	
		
		P1_P(diskBlock);
		
		for (i=0;i<numBlocks;i++) {
			
			if (diskBlock[i].pid == pid) {
				diskBlock[i].pid = -1;		
				
				P1_P(semP3_VmStats);
				P3_vmStats.freeBlocks++;			
				P1_V(semP3_VmStats);
				
			}
		} 
		
		P1_V(diskBlock);
 
    /* Clean up the page table. */
	
		P1_P(semPageTable);
	
 		free((char *) processes[pid].pageTable);
		processes[pid].numPages = 0;
		processes[pid].pageTable = NULL;
		P1_SemFree(processes[pid].semPTE);
		
		P1_V(semPageTable);
	}	//if
   
}
 
/*
 *----------------------------------------------------------------------
 *
 * P3_Switch
 *
 *  Called during a context switch. Unloads the mappings for the old
 *  process and loads the mappings for the new.
 *
 * Results:
 *  None.
 *
 * Side effects:
 *  The contents of the MMU are changed.
 *
 *----------------------------------------------------------------------
 */
void
P3_Switch(old, new)
    int     old;    /* Old (current) process */
    int     new;    /* New process */
{
    int     page;
    int     status;
 
    CheckMode();
    CheckPid(old);
    CheckPid(new);
	
	P1_P(semP3_VmStats);
	
    P3_vmStats.switches++;
    
	P1_V(semP3_VmStats);
	
	for (page = 0; page < processes[old].numPages; page++) {
    /*
     * If a page of the old process is in memory then a mapping 
     * for it must be in the MMU. Remove it.
     */
    if (processes[old].pageTable[page].state == INCORE) {
        //assert(processes[old].pageTable[page].frame != -1);
        status = USLOSS_MmuUnmap(TAG, page);
        if (status != USLOSS_MMU_OK) {
          // report error and abort
        }
    }
    }
    for (page = 0; page < processes[new].numPages; page++) {
    /*
     * If a page of the new process is in memory then add a mapping 
     * for it to the MMU.
     */
    if (processes[new].pageTable[page].state == INCORE) {
        //assert(processes[new].pageTable[page].frame != -1);
        status = USLOSS_MmuMap(TAG, page, processes[new].pageTable[page].frame,
            USLOSS_MMU_PROT_RW);
        if (status != USLOSS_MMU_OK) {
          // report error and abort
        }
    }
    }
}
 
/*
 *----------------------------------------------------------------------
 *
 * FaultHandler
 *
 *  Handles an MMU interrupt. 
 *
 * Results:
 *  None.
 *
 * Side effects:
 *  The current process is blocked until the fault is handled.
 *
 *----------------------------------------------------------------------
 */
static void
FaultHandler(type, arg)
    int     type;   /* USLOSS_MMU_INT */
    void    *arg;   /* Address that caused the fault */
{
    int     cause;
    int     status;
    Fault   fault;
    int     size;
 
	//USLOSS_Console("fault handler was called %d times \n", P3_vmStats.faults);
 //
 
    //assert(type == USLOSS_MMU_INT);
    cause = USLOSS_MmuGetCause();
    //assert(cause == USLOSS_MMU_FAULT);
	
	P1_P(semP3_VmStats);
		
    P3_vmStats.faults++;
	
	
    
	P1_V(semP3_VmStats);
	
	fault.pid = P1_GetPID();
    fault.addr = arg;
    fault.mbox = P2_MboxCreate(1, 0);
	
	if (P3_vmStats.faults==5) {
		//assert(fault.pid == 99);
	}
	
    //assert(fault.mbox >= 0);
    size = sizeof(fault);
	//USLOSS_Console("sending to pager mailbox pid %d, addr %x mbox %d\n", fault.pid, fault.addr, fault.mbox);
    status = P2_MboxSend(pagerMbox, &fault, &size);
    //assert(status >= 0);
    //assert(size == sizeof(fault));
    size = 0;
    status = P2_MboxReceive(fault.mbox, NULL, &size);
	//USLOSS_Console("fault handler heard back from pager\n");
	
	//assert(status >= 0);
    status = P2_MboxRelease(fault.mbox);
    //assert(status == 0);
}
 
/*
 *----------------------------------------------------------------------
 *
 * Pager 
 *
 *  Kernel process that handles page faults and does page 
 *  replacement.
 *
 * Results:
 *  None.
 *
 * Side effects:
 *  None.
 *
 *----------------------------------------------------------------------
 */
static int
Pager(void* arg)
{
    int recStatus;
	int sendStatus;
	int quitStatus=0;
	int errorCode;
	int size = sizeof(Fault);
	void *vmRegion;
	
	int freeFrame;
	int pagePtr;
	int nosize = 0; //the size of the message sent back to FaultHandler.  This need to be a separate variable
	int pagerPID;
	
	int oldPID;
	int oldPage;
	//int accessPtr;
	int i;
	
	//int track = 0;
	//int first = 0;
			
	//void *buffer;
	//int returnVal;
	
	//int blnReplacePage = 1;

	
	while(1) {
        /* Wait for fault to occur (receive from pagerMbox) */
        /* Find a free frame */
        /* If there isn't one run clock algorithm, write page to disk if necessary */
        /* Load page into frame from disk or fill with zeros */
        /* Unblock waiting (faulting) process */
		int fpage;	//page number that is faulted
		
		Fault currFault;
		Fault* p4buffer = malloc(sizeof(Fault));
		memset(p4buffer,0, sizeof(Fault));
		//USLOSS_Console("right after memset, pid %d, addr %x  mbox %d\n", p4buffer->pid, p4buffer->addr, p4buffer->mbox);		
		recStatus = P2_MboxReceive(pagerMbox, p4buffer, &size);
		//USLOSS_Console("pager just got another mesage, pid %d, addr %x  mbox %d\n", p4buffer->pid, p4buffer->addr, p4buffer->mbox);
		//assert(recStatus >= 0);
		
		/*check to see if its been killed*/
		if( (p4buffer->pid) == KILLSELF)  {
			//USLOSS_Console("Pager trying to quit\n");
			P1_Quit(quitStatus);
			
		}
		//
		//if still alive, read in the Fault message
		currFault.pid = p4buffer->pid;
		currFault.addr = p4buffer->addr;
		currFault.mbox = p4buffer->mbox;
		
		//compute page number
		fpage = (int )currFault.addr/USLOSS_MmuPageSize();
		//USLOSS_Console("fpage is: %d\n", fpage);
		
		free(p4buffer);		
		
		//check for whether page has been previously accessed
		if (processes[currFault.pid].pageTable[fpage].state == UNUSED) {
			P3_vmStats.new++;
		} 
		//
		
		//P1_DumpProcesses();
		USLOSS_Console("enter pager process\n");
		USLOSS_Console("currFault.pid: %d\n", currFault.pid);
		USLOSS_Console("currFault.addr: %x\n", currFault.addr);
		
		P1_P(semFreeFrame);
		
		freeFrame = findFreeFrame(currFault.pid, fpage);
		USLOSS_Console("frree frame: %d\n", freeFrame);
		
		P1_V(semFreeFrame);
		
		
		//phase 3a logic..An unused frame exists
		//**************************************************************************************************
		if (freeFrame != -1)  {
			vmRegion=USLOSS_MmuRegion(&pagePtr);
			pagerPID = P1_GetPID();		
			
			//Give mapping to Pager and update Pagers pageTable
			errorCode = USLOSS_MmuMap(0, fpage, freeFrame, USLOSS_MMU_PROT_RW);
			assert(errorCode == USLOSS_MMU_OK);
			processes[pagerPID].pageTable[fpage].frame = freeFrame;
			processes[pagerPID].pageTable[fpage].state = INCORE;
			
			//check if the page exists on disk, if not zero out the page
			if (processes[currFault.pid].pageTable[fpage].block == -1) {
				memset(vmRegion+fpage*USLOSS_MmuPageSize(), 0, USLOSS_MmuPageSize());
				errorCode = USLOSS_MmuUnmap(0, fpage);	
				assert(errorCode == USLOSS_MMU_OK);
				
				//Unmap this from the Pager process and give mapping to the faulting process
				processes[pagerPID].pageTable[fpage].frame = -1;
				processes[pagerPID].pageTable[fpage].state = UNUSED;
				processes[currFault.pid].pageTable[fpage].frame = freeFrame;
				processes[currFault.pid].pageTable[fpage].state = INCORE;
				
			}
			
			//page is on disk
			else {
				writeNewPage(currFault.pid, fpage, freeFrame, vmRegion);  //the mapping for the faulting process is handled
																		 // in writeNewPage
			}
		} //if (freeFrame != -1)
		//**************************************************************************************************
		//cannot find an unused frame.  so replace an existing page
		else {	//(freeFrame == -1) {
			
			P1_P(semClockPos);
			
			//find a frame to use
			freeFrame = runClockAlgo(currFault.pid,fpage);
			USLOSS_Console("freeFrame after return from clockAlgo %d\n", freeFrame);
					
			P1_V(semClockPos);
			
			//find old page associated with frame
			
			P1_P(semFreeFrame);
			
			oldPage = frmTable[freeFrame].page;
			oldPID = frmTable[freeFrame].pid;
			USLOSS_Console("oldPage: %d oldPID: %d\n", oldPage, oldPID);
			
			//update frame table here and not in runClockAlgo() 
			frmTable[freeFrame].page = fpage;
			frmTable[freeFrame].pid = currFault.pid;
			frmTable[freeFrame].state = INCORE;
				
			P1_V(semFreeFrame);
			
			//update the page table for the old process and possibly write old page to disk
			P1_P(processes[oldPID].semPTE);
			
			processes[oldPID].pageTable[oldPage].frame = -1;
			processes[oldPID].pageTable[oldPage].state = ONDISK;
			
			P1_V(processes[oldPID].semPTE);
			
			P1_P(semDiskIO);
			
			writeOldPage(oldPID, oldPage, freeFrame, vmRegion); 
			
			//update the page table for the new process 
			//processes[currFault.pid].pageTable[fpage].frame = freeFrame;
			//processes[currFault.pid].pageTable[fpage].state = INCORE;
			writeNewPage(currFault.pid, fpage, freeFrame, vmRegion);
			
			P1_V(semDiskIO);

			
			
			
			//check if process tied to replaced page has quit if so update the page table to not point to
			//any blocks
			if (P1_GetState(oldPID) == 3) {
				for (i=0;i<numPages;i++) {
					//processes[oldPID].pageTable[i].block = -1;
				}
				for (i=0;i<numFrames;i++) {
					if (frmTable[i].pid == oldPID ) {
						frmTable[i].pid = -1;
						frmTable[i].page = -1;
						frmTable[i].state = UNUSED;
					}
				}
				for (i=0;i<numBlocks;i++) {
					diskBlock[i].pid = -1;
				}
			}
		}	//if freeFrame != -1
		//**************************************************************************************************************8
				
		//size = 0;		///this is a big bug
		sendStatus = P2_MboxSend(currFault.mbox, NULL, &nosize);
		
		
    }	//end of while (1)
    /* Never gets here. */
    return 1;
}	//end of Pager
 
/*
 * Helper routines
 */
 
static void
CheckPid(int pid) 
{
    if ((pid < 0) || (pid >= P1_MAXPROC)) {
        USLOSS_Console("Invalid pid\n"); 
        USLOSS_Halt(1);
    }
}
 
static void
CheckMode(void)
{
    if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0) {
       USLOSS_Console("Invoking protected routine from user mode\n");
       USLOSS_Halt(1);
    }
}
 
int P3_Startup(void *arg)
{
    int rc;
	int child;
	
	sectorsPerBlock = USLOSS_MmuPageSize() / USLOSS_DISK_SECTOR_SIZE;	
	Sys_DiskSize(diskUnit, &sectorSize, &numSectorPerTrack, &numTracks);
	//USLOSS_Console("disk info: %d %d %d %d\n", diskUnit, sectorSize, numSectorPerTrack, numTracks);	
	numBlocks = numTracks * numSectorPerTrack / sectorsPerBlock;
	//assert(numBlocks==200);
	
	p4_return_pid = Sys_Spawn("P4_Startup", P4_Startup, NULL,  4 * USLOSS_MIN_STACK, 3, p4_pid_ptr);
	rc = Sys_Wait(&p4_return_pid, &child);
    assert(rc == 0);
	return 0;
}



int findFreeFrame(int currPID, int pageNum) {
	int		i;		//loop counter
	
	for(i=0;i<numFrames;i++) {
		if (frmTable[i].state == UNUSED) {
			frmTable[i].state = INCORE;
			frmTable[i].pid = currPID;
			frmTable[i].page = pageNum;
			P3_vmStats.freeFrames--;
			return i;
		}
	}
	//cannot find a free frame
	//USLOSS_Console("cannot find a free frame\n");
	//USLOSS_Halt(1);
	return -1;
}

int runClockAlgo(int currPID, int pageNum) {
//	int 	i; 		//loop counter
	int 	returnVal;
	int 	freeFrame = -1;
	int 	accessPtr;
	int 	blnFound = 0;
		
	while(! blnFound ) {
		returnVal = USLOSS_MmuGetAccess(clockPos, &accessPtr);
		assert(returnVal==0);
		
		//USLOSS_Console("accessPtr: %d\n", accessPtr);
		if ( ((accessPtr) & (1 << 0))  == 0 )  {  //USLOSS_MMU_REF not set accessPtr & (1 << 0)
			//frmTable[clockPos].state = INCORE;
			//frmTable[clockPos].pid = currPID;
			//frmTable[clockPos].page = pageNum;
			//P3_vmStats.freeFrames--;
			USLOSS_Console("free frame from clock algo: %d\n", clockPos);
			freeFrame = clockPos;
			blnFound = 1;
			clockPos++;		//advance clockhand to next frame
			if (clockPos == numFrames) {	//set clock hand back to zero
				clockPos = 0;
			}
			break;
				
		}	//if
		else {	///clear the reference bit and advance clock hand
			USLOSS_MmuSetAccess(clockPos, (accessPtr &= ~ (1 << 0)) );   // num &=  ~(1 << 0);
			//USLOSS_Console("after clearing reference bit accessptr: %d\n", accessPtr);
			clockPos++;
			if (clockPos == numFrames) {	//set clock hand back to zero
				clockPos = 0;
			}
		}
	}	//for
	
	return freeFrame;
}

/*  Check if the page to be replaced is on disk and if so, check if the page in memory is dirty



*/
void writeOldPage(int currPID, int pageNum, int freeFrame, void* vmRegion) {
	int returnVal; 
	int startTrack;	//track number to start writing from
	int first;	//first sector to be written
	int assignedBlock; //the disk block assigned to the page to be replaced
	int accessPtr;
	int pagerPID;
	int framePtr;
	int protPtr;
	int errorCode;
	int currBlock;
	void* buffer;
	
	USLOSS_Console("fault no: %d\n", P3_vmStats.faults);
	
	//page does not exist on disk.  Update disk block table, update usedBlocks, write page to disk
	if ( processes[currPID].pageTable[pageNum].block == -1) {
		
		P1_P(semDiskBlock);
		
		currBlock = findDiskBlock(currPID);
		USLOSS_Console("new disk block: %d\n", currBlock);
		diskBlock[currBlock].pid = currPID;
		
		P1_V(semDiskBlock);
		
		processes[currPID].pageTable[pageNum].block = currBlock;
		startTrack = currBlock/2;
		first = (currBlock % 2) * 8;
		
		P1_P(semP3_VmStats);
		
		P3_vmStats.freeBlocks--;
		
		P1_V(semP3_VmStats);
		
		//usedBlocks++;
	}
	
	//otherwise replaced page is on disk.  Check if dirty
	
	else {
		
		returnVal = USLOSS_MmuGetAccess(freeFrame, &accessPtr);
		assert(returnVal==0);
		if ( ((accessPtr) & (1 << 1))  == 1 ) {
			USLOSS_Console("page is dirty\n");
			assignedBlock = processes[currPID].pageTable[pageNum].block;
			
			//compute startTrack and first from the block number;
			startTrack = assignedBlock/2;
			first = (assignedBlock % 2) * 8; 
		}
		
		//just return without bothering the Disk Driver
		else {
			USLOSS_Console("page is not dirty and is already on disk..no writing to disk\n");
			return;
		}
	}
	
	
	//map the page to the Pager process...Then set mapping otherwise infinite loop
	pagerPID = P1_GetPID();	
	processes[pagerPID].pageTable[pageNum].state = INCORE;
	processes[pagerPID].pageTable[pageNum].frame = freeFrame;
	//errorCode = USLOSS_MmuUnmap(0, pageNum);
	//assert(errorCode==0);
	
	//if a mapping for this page exists, unmap it
	returnVal = USLOSS_MmuGetMap(TAG, pageNum, &framePtr, &protPtr );
	if (returnVal != USLOSS_MMU_ERR_NOMAP) {
		errorCode = USLOSS_MmuUnmap(TAG, pageNum);
		assert(errorCode == USLOSS_MMU_OK);	
		assert(framePtr == freeFrame);
	}
	
	errorCode = USLOSS_MmuMap(0, pageNum, freeFrame, USLOSS_MMU_PROT_RW);
	assert(errorCode==0);
	
	//copy to buffer then call disk write
	buffer = malloc((USLOSS_MmuPageSize()));
	memcpy(buffer, vmRegion+pageNum*USLOSS_MmuPageSize() , USLOSS_MmuPageSize());
	returnVal = P2_DiskWrite(diskUnit, startTrack, first , sectorsPerBlock , buffer);
	assert(returnVal==0);
	errorCode = USLOSS_MmuUnmap(0, pageNum);
	assert(errorCode==0);
	
	processes[pagerPID].pageTable[pageNum].state = UNUSED;
	processes[pagerPID].pageTable[pageNum].frame = -1;
	
	free(buffer);
	return;
}	//writeOldPage

/* if this is a brand new page, zero it out.  Otherwise load the page in from disk;
 *
 */
void writeNewPage(int newPID, int newPage, int freeFrame, void* vmRegion) {
	
	int returnVal; 
	int startTrack;	//track number to start writing from
	int first;	//first sector to be written
	int assignedBlock; //the disk block assigned to the page to be replaced
//	int accessPtr;
	int pagePtr;
	int pagerPID;
	int framePtr;
	int protPtr;
	void* buffer;
	int errorCode;
	
	buffer = malloc((USLOSS_MmuPageSize()));
	pagerPID = P1_GetPID();	
	
	//assign frame to pager so it can zero the frame
			
	processes[pagerPID].pageTable[newPage].frame = freeFrame;
	processes[pagerPID].pageTable[newPage].state = INCORE;
	
	//if a mapping for this page exists, unmap it
	returnVal = USLOSS_MmuGetMap(TAG, newPage, &framePtr, &protPtr );
	if (returnVal != USLOSS_MMU_ERR_NOMAP) {
		errorCode = USLOSS_MmuUnmap(TAG, newPage);
		assert(errorCode == USLOSS_MMU_OK);	
		assert(framePtr == freeFrame);
	}
		
	//map new page
	vmRegion=USLOSS_MmuRegion(&pagePtr);
	errorCode = USLOSS_MmuMap(0, newPage, freeFrame, USLOSS_MMU_PROT_RW);
	assert(errorCode == USLOSS_MMU_OK);
	
	
	//brand new page
	USLOSS_Console("brand new page \n");
	if (processes[newPID].pageTable[newPage].block == -1 ) {
		memset(vmRegion+newPage*USLOSS_MmuPageSize(), 0, USLOSS_MmuPageSize());
		//free(buffer);	
	}

	//page is on disk so load into memory
	else {
		
		assignedBlock = processes[newPID].pageTable[newPage].block;
		USLOSS_Console("assignedBlock from disk: %d\n", assignedBlock);
		startTrack = assignedBlock/2;
		first = (assignedBlock % 2) * 8; 	//always 0 or 8 
		
		returnVal = P2_DiskRead(diskUnit, startTrack, first , sectorsPerBlock , buffer);
		assert(returnVal==0);
		memcpy(vmRegion+newPage*USLOSS_MmuPageSize() , buffer, USLOSS_MmuPageSize());
		USLOSS_Console("loading page %d into RAM\n", newPage);
	}
	
	//unmap the pager from mmu and then update page tables
	errorCode = USLOSS_MmuUnmap(TAG, newPage);
	assert(errorCode == USLOSS_MMU_OK);		
	
	processes[pagerPID].pageTable[newPage].frame = -1;
	processes[pagerPID].pageTable[newPage].state = UNUSED;
	
	processes[newPID].pageTable[newPage].frame = freeFrame;
	processes[newPID].pageTable[newPage].state = INCORE;
	free(buffer);		
	return;
}	//writeNewPage

int findDiskBlock(int oldPID) {
	int 	i;
	for(i=0;i<numBlocks;i++) {
		if (diskBlock[i].pid == -1) {
			diskBlock[i].pid = oldPID;
			return i;
		}
	}
	USLOSS_Console("cannot find free disk block\n");
	USLOSS_Halt(10);
	return 0;
}


