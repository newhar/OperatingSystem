#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#define CHILDNUM 10
#define QUANTUM 3
#define PDIRNUM 512
#define PTLBNUM 512
#define FRAMENUM 512
#define QNUM 32

// Data Structure & Functions

typedef struct{
	int valid;
	int pfn;
}TABLE;

typedef struct{
	int valid;
	TABLE*  pt;
}PDIR;

struct msg_buffer{
        long int  mtype;
        int pid_index;
        unsigned int  vm[10];
};

FILE* fp;
pid_t pid[CHILDNUM];
PDIR pdir[CHILDNUM][PDIRNUM];
struct msg_buffer msg;
int cpu_burst[CHILDNUM] ={10,6,5,2,8,4,3,2,7,4};
int cpu_burst_ref[CHILDNUM];
int rq[QNUM]; //RUNQUEUE
int hd,tl = 0;
int free_page_list[512] ; //FREE PAGE LIST
int fpl_tl,fpl_hd = 0;
int idx, total_tik, count, proc_done = 0;

int msgq;
int ret;
int key = 0x12345;

void kernelHandler(int sig); 
void processHandler(int sig);
void toFreePageList(PDIR* p_dir);

int main(int argc, char *argv[])
{
	fp = fopen("paging.txt","w");
	unsigned int vm[10];
  unsigned int offset[10];
	unsigned int pdir_idx[10];
  unsigned int tbl_idx[10];
	int pid_index;

	for(int l=0; l<CHILDNUM;l++)
		cpu_burst_ref[l]=cpu_burst[l]; 
	
	for(int l=0 ; l < FRAMENUM; l++){
		free_page_list[l] = l ;
		fpl_tl++ ;
	}
	msgq = msgget( key, IPC_CREAT | 0666);

	while(idx< CHILDNUM) {
		for( int i = 0; i < CHILDNUM ; i++) {
			for(int j =0; j< PDIRNUM ; j++)	{	
			pdir[i][j].valid =0;
			pdir[i][j].pt = NULL;
			}
		}
		pid[idx] = fork();
		rq[(tl++)%QNUM] = idx ;
		if (pid[idx]== -1) {
			perror("fork error");
			return 0;
		}
		else if (pid[idx]== 0) {
			//child
			struct sigaction old_sa;
			struct sigaction new_sa;

			memset(&new_sa, 0, sizeof(new_sa));
			new_sa.sa_handler = &processHandler;
			sigaction(SIGINT, &new_sa, &old_sa);

			while(1);

			return 0;
		}
		else {
			//parent
			struct sigaction old_sa;
			struct sigaction new_sa;
			memset(&new_sa, 0, sizeof(new_sa));

			new_sa.sa_handler = &kernelHandler; // 매 시간마다 발생되는 신호
			sigaction(SIGALRM, &new_sa, &old_sa);

			struct itimerval new_itimer, old_itimer;
			new_itimer.it_interval.tv_sec = 0;
			new_itimer.it_interval.tv_usec = 100000; // 처음 신호까지의 시간
			new_itimer.it_value.tv_sec = 0;
			new_itimer.it_value.tv_usec = 100000; // 다음 부터 주기적인 신호 시간
			setitimer(ITIMER_REAL, &new_itimer, &old_itimer);  // 타이머 시작
		}
		idx++;
	}
	while(1){
		ret = msgrcv(msgq,&msg,sizeof(msg),IPC_NOWAIT,IPC_NOWAIT);
		if(ret != -1){
			pid_index = msg.pid_index;
			for(int k=0 ; k < 10 ; k ++ ){				
				vm[k]=msg.vm[k]; 
				offset[k] = vm[k] & 0xfff;				
				tbl_idx[k] = (vm[k] & 0xff000)>>12;
				pdir_idx[k]=(vm[k] & 0xff00000)>>20;

				if( pdir[pid_index][pdir_idx[k]].valid == 0 )
				{	
					fprintf(fp,"FIRST PAGE FAULT!\n");
					printf("FIRST PAGE FAULT!\n");
					TABLE* table = (TABLE*) calloc(PTLBNUM, sizeof(TABLE));
					pdir[pid_index][pdir_idx[k]].pt = table;
					pdir[pid_index][pdir_idx[k]].valid = 1; 
				}

				TABLE* ptbl = pdir[pid_index][pdir_idx[k]].pt;

				if(ptbl[tbl_idx[k]].valid== 0)
				{
					fprintf(fp,"SECOND PAGE FAULT!\n");
					printf("SECOND PAGE FAULT!\n");
					if(fpl_hd != fpl_tl){
						ptbl[tbl_idx[k]].pfn = free_page_list[(fpl_hd%FRAMENUM)];
						ptbl[tbl_idx[k]].valid = 1;
						fpl_hd++;
					}
					else{
						for(int k = 0; k < CHILDNUM ; k ++)
						{
							kill(pid[k],SIGKILL);
						}
						msgctl(msgq, IPC_RMID, NULL);
						exit(0);
						return 0;
					}
				}
			fprintf(fp,"VA : 0x%08x[PDIR : %d, PTBL : %d, OFFEST : 0x%04x] -> PA:0x%08x\n", vm[k], pdir_idx[k], tbl_idx[k], offset[k], (ptbl[tbl_idx[k]].pfn<<12)+offset[k]);
			printf("VA : 0x%08x[PDIR : %d, PTBL : %d,OFFSET : 0x%04x] -> PA:0x%08x\n", vm[k], pdir_idx[k], tbl_idx[k], offset[k], (ptbl[tbl_idx[k]].pfn<<12)+offset[k]);
			}
			memset(&msg, 0, sizeof(msg));
		}
	}
	return 0;
}

void kernelHandler(int sig) 
{
	if(proc_done == 1){
		toFreePageList(pdir[rq[(hd-1)%QNUM]]);
		proc_done = 0;
	}
  total_tik ++;
  count ++;
  if(total_tik >= 10000){
		for(int k = 0; k < CHILDNUM ; k ++)
		{
			kill(pid[k],SIGKILL);
		}
		msgctl(msgq, IPC_RMID, NULL);
		exit(0);
	}
	fprintf(fp,"-------------------------------------------------\n");
	fprintf(fp,"Tik Times : %d ",total_tik);
	fprintf(fp,"PID :%d ,REMAINING CPU BURST : %d\n",pid[rq[hd% QNUM]],cpu_burst[rq[hd% QNUM]]);
	printf("-------------------------------------------------\n");
	printf("Tik Times %d ",total_tik);
	printf("PID : %d ,REMAINING CPU BURST : %d\n",pid[rq[hd% QNUM]],cpu_burst[rq[hd% QNUM]]);

	if((hd%QNUM) != (tl%QNUM)){
		cpu_burst[rq[hd%QNUM]] --;
		kill(pid[rq[hd % QNUM]],SIGINT);
		if((count == QUANTUM)|(cpu_burst[rq[hd%QNUM]]==0)){
	                count  = 0;
			if(cpu_burst[rq[hd%QNUM]] != 0)
				 rq[(tl++)%QNUM] = rq[hd%QNUM];
			if(cpu_burst[rq[hd%QNUM]] == 0 ){
				cpu_burst[rq[hd%QNUM]] = cpu_burst_ref[rq[hd%QNUM]];
				rq[(tl++)%QNUM] = rq[hd%QNUM];
				proc_done = 1;
			}
			hd ++;
		}
	}
}

void processHandler(int sig)
{
	cpu_burst[idx] -- ; // 현재 실행중인 프로세스 cpu burst time 감소
	if(cpu_burst[idx] <= 0)
		cpu_burst[idx] = cpu_burst_ref[idx];
	memset(&msg,0,sizeof(msg));
	msg.mtype = IPC_NOWAIT;
	msg.pid_index = idx;
	unsigned int temp_add;
	for (int k=0; k< 10 ; k++){
		// 10개의 메모리 접근 시도 
		temp_add = (rand() %4)<<20; // 10 bit for directory page table
		temp_add |= (rand()%32)<<12; // 10 bit for page table 
		temp_add |= (rand()%0xfff); // 12 bit for offset 
		msg.vm[k] = temp_add ;
	}
	ret = msgsnd(msgq, &msg, sizeof(msg),IPC_NOWAIT);
	if(ret == -1)
		perror("msgsnd error");
}

void toFreePageList(PDIR* p_dir)
{
	PDIR* temp_dir = p_dir;
	for(int i=0; i< PDIRNUM ; i++)
	{
		if(temp_dir[i].valid == 1 ){
			temp_dir[i].valid=0;
			for(int j=0 ; j < PTLBNUM ; j++)
				if((temp_dir[i].pt)[j].valid == 1){
					free_page_list[(fpl_tl++)%FRAMENUM]=(temp_dir[i].pt)[j].pfn; 
				}
			temp_dir[i].pt =NULL;
			free(temp_dir[i].pt);
		}
	}
}
