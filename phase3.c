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
 
/*
 * Information about page faults.
 */
typedef struct Fault {
    int     pid;        /* Process with the problem. */
    void    *addr;      /* Address that caused the fault. */
    int     mbox;       /* Where to send reply. */
    /* Add more stuff here if necessary. */
} Fault;
 
static void *vmRegion = NULL;
 
P3_VmStats  P3_vmStats;
 
static int pagerMbox = -1;

static void CheckPid(int);
static void CheckMode(void);
static void FaultHandler(int type, void *arg);
static int Pager(void);
static void TestMMUDriver(void);
 
void    P3_Fork(int pid);
void    P3_Switch(int old, int new);
void    P3_Quit(int pid);
 
int p4_pid;
int p4_return_pid;
int* p4_pid_ptr = &p4_pid;
 ;
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
    /*
     * Create the page fault mailbox and fork the pagers here.
     */
	 
	 pagerMbox = P2_MboxCreate(P1_MAXPROC, sizeof(Fault));
	 
	////////////////////////////////////////////////////////////David's
	
	 for(i = 0; i < P3_MAX_PAGERS; i++){
		char pagersName[20];
		snprintf(pagersName, sizeof(pagersName), "Pager %d", i);
		pagerIdTable[i] = P1_Fork(pagersName, Pager, NULL, USLOSS_MIN_STACK, 2);
	} 
	
	////////////////////////////////////////////////////////////Addition 
	
	
      
    memset((char *) &P3_vmStats, 0, sizeof(P3_VmStats));
    P3_vmStats.pages = pages;
    P3_vmStats.frames = frames;
    numPages = pages;
    numFrames = frames;
    //return numPages * USLOSS_MmuPageSize();
	
	//testing only
	//P1_Fork("TestMMUDriver", TestMMUDriver, NULL, USLOSS_MIN_STACK, 2);
	
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
    CheckMode();
    USLOSS_MmuDone();
    /*
     * Kill the pagers here.
     */
    /* 
     * Print vm statistics.
     */
    USLOSS_Console("P3_vmStats:\n");
    USLOSS_Console("pages: %d\n", P3_vmStats.pages);
    USLOSS_Console("frames: %d\n", P3_vmStats.frames);
    USLOSS_Console("blocks: %d\n", P3_vmStats.blocks);
    /* and so on... */
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
    assert(processes[pid].numPages > 0);
    assert(processes[pid].pageTable != NULL);
 
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
 
    P3_vmStats.switches++;
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
    P3_vmStats.faults++;
    fault.pid = P1_GetPID();
    fault.addr = arg;
    fault.mbox = P2_MboxCreate(1, 0);
    assert(fault.mbox >= 0);
    size = sizeof(fault);
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
	int errorCode;
	int size = sizeof(Fault);
	void *buffer;
	Fault currFault;
	
	buffer = malloc(sizeof(Fault));
	
	int frame = 1;
	int protPtr = USLOSS_MMU_PROT_RW;
	
	while(1) {
        /* Wait for fault to occur (receive from pagerMbox) */
        /* Find a free frame */
        /* If there isn't one run clock algorithm, write page to disk if necessary */
        /* Load page into frame from disk or fill with zeros */
        /* Unblock waiting (faulting) process */
		
		recStatus = P2_MboxReceive(pagerMbox, &currFault, &size);
		assert(recStatus >= 0);
		
		//currFault = (*(Fault *) buffer);
		P1_DumpProcesses();
		USLOSS_Console("enter pager process\n");
		//errorCode = USLOSS_MmuUnmap(0, 1);
		//assert(errorCode == USLOSS_MMU_OK);
		//errorCode=USLOSS_MmuGetMap(0,1, &frame, &protPtr );
		errorCode = USLOSS_MmuMap(0, 0, 0, USLOSS_MMU_PROT_RW);
		assert(errorCode == USLOSS_MMU_OK);
		errorCode = USLOSS_MmuMap(0, 1, 1, USLOSS_MMU_PROT_RW);
		assert(errorCode == USLOSS_MMU_OK);
		size = 0;
		sendStatus = P2_MboxSend(currFault.mbox, NULL, &size);
		
		
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
    p4_return_pid = Sys_Spawn("P4_Startup", P4_Startup, NULL,  4 * USLOSS_MIN_STACK, 3, p4_pid_ptr);
	return 0;
}

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
	//errorCode = USLOSS_MmuMap(0, 1, 1, USLOSS_MMU_PROT_RW);
	//assert(errorCode == USLOSS_MMU_OK);
	
	USLOSS_Console("erorr code is: %d\n", errorCode);
}
