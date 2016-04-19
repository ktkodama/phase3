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
 
/*
 * Everybody uses the same tag.
 */
#define TAG 0
/*
 * Page table entry
 */
 
#define UNUSED  0
#define INCORE  1
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
    /* Add more stuff here if necessary. */
} Process;
 
static Process  processes[P1_MAXPROC];

static int  numPages = 0;
static int  numFrames = 0;

//temporary
//static int nextPage = 0;
 
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
	int 	id;				/* Frame id*/
	int 	pid;			/* Process id*/
	int 	state;			/* */

} Frame;

static Frame *frmTable;

static void *vmRegion = NULL;
 
P3_VmStats  P3_vmStats;
 
static int pagerMbox = -1;

static void CheckPid(int);
static void CheckMode(void);
static void FaultHandler(int type, void *arg);
static int Pager(void);
static void TestMMUDriver(void);
static void TestRecMail(void);
static int findFreeFrame(int);
 
void    P3_Fork(int pid);
void    P3_Switch(int old, int new);
void    P3_Quit(int pid);
 
int p4_pid;
int p4_return_pid;
int* p4_pid_ptr = &p4_pid;

P1_Semaphore semFreeFrame;
P1_Semaphore semProcTable;
P1_Semaphore semMutex;
P1_Semaphore semP3_VmStats;



 ////
 
 
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
 //
    CheckMode();
    status = USLOSS_MmuInit(mappings, pages, frames);
    if (status != USLOSS_MMU_OK) {
       USLOSS_Console("P3_VmInit: couldn't initialize MMU, status %d\n", status);
       USLOSS_Halt(1);
    }
    vmRegion = USLOSS_MmuRegion(&tmp);
	USLOSS_Console("address of mmuregion in p3 vm init: %d\n", (int)vmRegion);
    assert(vmRegion != NULL);
	USLOSS_Console("the number of pages is: %d\n", tmp);
	//USLOSS_Console("the number of pages is: %d\n", tmp);
    assert(tmp >= pages);
    USLOSS_IntVec[USLOSS_MMU_INT] = FaultHandler;
    for (i = 0; i < P1_MAXPROC; i++) {
        processes[i].numPages = 0;
        processes[i].pageTable = NULL;
    }
	
	//create Frame table
	P1_P(semFreeFrame);
	
	frmTable = (Frame *) malloc(frames * sizeof(Frame));
	memset(frmTable, 0, frames * sizeof(Frame));
	
	P1_V(semFreeFrame);
	
    /*
     * Create the page fault mailbox and fork the pagers here.
     */
	 
	 pagerMbox = P2_MboxCreate(P1_MAXPROC, sizeof(Fault));
	 
	////////////////////////////////////////////////////////////David's
	
	 for(i = 0; i < 1; i++){		//P3_MAX_PAGERS
	 //for(i = 0; i < P3_MAX_PAGERS; i++){		//
		char pagersName[20];
		snprintf(pagersName, sizeof(pagersName), "Pager %d", i);
		pagerIdTable[i] = P1_Fork(pagersName, Pager, NULL, USLOSS_MIN_STACK, 2);
	} 
	
	////////////////////////////////////////////////////////////Addition 
	
	
      
    memset((char *) &P3_vmStats, 0, sizeof(P3_VmStats));
    P3_vmStats.pages = pages;
    P3_vmStats.frames = frames;
	P3_vmStats.freeFrames = frames;
    numPages = pages;
    numFrames = frames;
    //return numPages * USLOSS_MmuPageSize();
	
	//testing only
	//P1_Fork("TestMMUDriver", TestMMUDriver, NULL, USLOSS_MIN_STACK, 2);
	//P1_Fork("TestSendMail", TestSendMail, NULL, USLOSS_MIN_STACK, 2);
	//P1_Fork("TestRecMail", TestRecMail, NULL, USLOSS_MIN_STACK, 2);
	
	
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
    Fault killFault;
	int killMsgSize = sizeof(killFault);
	int status;
	CheckMode();
    USLOSS_MmuDone();
    /*
     * Kill the pagers here.
     */
	
	killFault.pid = KILLSELF;
	//USLOSS_Console("sending to pager mailbox to die\n");
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
    int     pid;        /* New process */
{
    int     i;
	//USLOSS_Console("p3 fork is called:\n");
	//P1_DumpProcesses();
    CheckMode();
    CheckPid(pid);
    processes[pid].numPages = numPages;
    processes[pid].pageTable = (PTE *) malloc(sizeof(PTE) * numPages);
    for (i = 0; i < numPages; i++) {
        processes[pid].pageTable[i].frame = -1;
        processes[pid].pageTable[i].block = -1;
        processes[pid].pageTable[i].state = UNUSED;
    }
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
    int     pid;
{
    CheckMode();
    CheckPid(pid);
	
	//if(P1_GetPID()==15) {
		//P1_DumpProcesses();
	//}
	
    
	//assert(processes[pid].numPages > 0);
    //assert(processes[pid].pageTable != NULL);
 
    /* 
     * Free any of the process's pages that are on disk and free any page frames the
     * process is using.
     */
 
    /* Clean up the page table. */
 
    free((char *) processes[pid].pageTable);
    processes[pid].numPages = 0;
    processes[pid].pageTable = NULL;
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
        assert(processes[old].pageTable[page].frame != -1);
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
        assert(processes[new].pageTable[page].frame != -1);
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
 
    assert(type == USLOSS_MMU_INT);
    cause = USLOSS_MmuGetCause();
    assert(cause == USLOSS_MMU_FAULT);
	
	P1_P(semP3_VmStats);
		
    P3_vmStats.faults++;
    
	P1_V(semP3_VmStats);
	
	fault.pid = P1_GetPID();
    fault.addr = arg;
    fault.mbox = P2_MboxCreate(1, 0);
    assert(fault.mbox >= 0);
    size = sizeof(fault);
	USLOSS_Console("sending to pager mailbox pid %d, addr %x mbox %d\n", fault.pid, fault.addr, fault.mbox);
    status = P2_MboxSend(pagerMbox, &fault, &size);
    assert(status >= 0);
    assert(size == sizeof(fault));
    size = 0;
    status = P2_MboxReceive(fault.mbox, NULL, &size);
    assert(status >= 0);
    status = P2_MboxRelease(fault.mbox);
    assert(status == 0);
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
Pager(void)
{
    int recStatus;
	int sendStatus;
	int quitStatus;
	int errorCode;
	int size = sizeof(Fault);
	//void *buffer;
	void *vmRegion;
	//Fault currFault;
	
	//buffer = malloc(sizeof(Fault));
	
	int freeFrame;
	int protPtr = USLOSS_MMU_PROT_RW;
	int pagePtr;
	int nosize = 0; //the size of the message sent back to FaultHandler.  This need to be a separate variable
	int pagerPID;
	
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
		USLOSS_Console("pager just got another mesage, pid %d, addr %x  mbox %d\n", p4buffer->pid, p4buffer->addr, p4buffer->mbox);
		assert(recStatus >= 0);
		
		/*check to see if its been killed*/
		if( (p4buffer->pid) == KILLSELF)  {
			P1_Quit(&quitStatus);
		}
		
		//if still alive, read in the Fault message
		currFault.pid = p4buffer->pid;
		currFault.addr = p4buffer->addr;
		currFault.mbox = p4buffer->mbox;
		
		//compute page number
		fpage = (int )currFault.addr/USLOSS_MmuPageSize();
		USLOSS_Console("fpage is: %d\n", fpage);
		
		free(p4buffer);		
		
		//check for whether page has been previously accessed
		if (processes[currFault.pid].pageTable[fpage].state == UNUSED) {
			P3_vmStats.new++;
		} 
		
		
		//P1_DumpProcesses();
		//USLOSS_Console("enter pager process\n");
		//USLOSS_Console("currFault.pid: %d\n", currFault.pid);
		//USLOSS_Console("currFault.addr: %x\n", currFault.addr);
		
		P1_P(semFreeFrame);
		
		freeFrame = findFreeFrame(currFault.pid);
		vmRegion=USLOSS_MmuRegion(&pagePtr);
		
		pagerPID = P1_GetPID();		//ask LO is this needed?
		
		//ask LO is this needed?  Give mapping to Pager and update Pagers pageTable
		errorCode = USLOSS_MmuMap(0, fpage, freeFrame, USLOSS_MMU_PROT_RW);
		assert(errorCode == USLOSS_MMU_OK);
		processes[pagerPID].pageTable[fpage].frame = freeFrame;
		processes[pagerPID].pageTable[fpage].state = INCORE;
		
		memset(vmRegion+fpage*USLOSS_MmuPageSize(), 0, USLOSS_MmuPageSize());
		
		//ask LO is this needed?  Unmap this from the Pager process and give mapping to the faulting process
		errorCode = USLOSS_MmuUnmap(0, fpage);
		processes[pagerPID].pageTable[fpage].frame = -1;
		processes[pagerPID].pageTable[fpage].state = UNUSED;
		assert(errorCode == USLOSS_MMU_OK);
		
		//assign mapping to the faulting process
		errorCode = USLOSS_MmuMap(0, fpage, freeFrame, USLOSS_MMU_PROT_RW);
		processes[currFault.pid].pageTable->frame = freeFrame;
		processes[currFault.pid].pageTable->state = INCORE;
				
		P1_V(semFreeFrame);
		
		
		//size = 0;		///this is a big bug
		sendStatus = P2_MboxSend(currFault.mbox, NULL, &nosize);
		
		
    }
    /* Never gets here. */
    return 1;
}
 
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
    semFreeFrame = P1_SemCreate(1);
	semProcTable = P1_SemCreate(1);
	semMutex = P1_SemCreate(1);
	semP3_VmStats = P1_SemCreate(1);
	
	p4_return_pid = Sys_Spawn("P4_Startup", P4_Startup, NULL,  4 * USLOSS_MIN_STACK, 3, p4_pid_ptr);
	return 0;
}
/*
static void TestMMUDriver(void) {
	int errorCode;
	
	errorCode = USLOSS_MmuMap(0, 0, 6, USLOSS_MMU_PROT_READ);
	assert(errorCode == USLOSS_MMU_ERR_FRAME);
	errorCode = USLOSS_MmuMap(0, 0, 4, USLOSS_MMU_PROT_READ);
	assert(errorCode == USLOSS_MMU_ERR_FRAME);
	errorCode = USLOSS_MmuMap(0, 2, 0, USLOSS_MMU_PROT_READ);
	assert(errorCode == USLOSS_MMU_ERR_PAGE);
	errorCode = USLOSS_MmuMap(0, 0, 1, USLOSS_MMU_PROT_RW);
	assert(errorCode == USLOSS_MMU_OK);
	errorCode = USLOSS_MmuMap(0, 1, 0, USLOSS_MMU_PROT_RW);
	assert(errorCode == USLOSS_MMU_OK);
	
	USLOSS_Console("erorr code is: %d\n", errorCode);
}

*/
/*this test method works but has nothing to do with phase 3*/
/*
static void TestRecMail(void) {
	int recStatus, sendStatus;
	int size;
	
	size = sizeof(Fault);
	int pidCount=1;
	int mboxCount=100;
	
	
	while(1) {
		Fault currFault, f1, f2;
		Fault* p4buffer = malloc(sizeof(Fault));
		//Fault p4buffer;
        
		f1.pid = pidCount++;
		f1.mbox = mboxCount;
		f1.addr = (void *) 0x245;
		mboxCount += 100;
		
		sendStatus = P2_MboxSend(pagerMbox, &f1, &size);
        recStatus = P2_MboxReceive(pagerMbox, p4buffer, &size);
		//USLOSS_Console("pager just got another mesage, pid %d, addr %x  mbox %d\n", p4buffer.pid, p4buffer.addr, p4buffer.mbox);
		USLOSS_Console("pager just got another mesage, pid %d, addr %x  mbox %d\n", p4buffer->pid, p4buffer->addr, p4buffer->mbox);
		
		//currFault.pid = p4buffer->pid;
		//currFault.addr = p4buffer->addr;
		//currFault.mbox = p4buffer->mbox;
		free(p4buffer);		
		
		if (pidCount == 3) {
			return;
		}
		
			
	}

	
}	
*/
int findFreeFrame(int currPID) {
	int		i;		//loop counter
	
	for(i=0;i<numFrames;i++) {
		if (frmTable[i].state == UNUSED) {
			frmTable[i].state = INCORE;
			frmTable[i].pid = currPID;
			P3_vmStats.freeFrames--;
			return i;
		}
	}
	//cannot find a free frame
	USLOSS_Console("cannot find a free frame\n");
	//USLOSS_Halt(1);
}
