#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <queue>
using namespace std;
#define CHILDNUMBER 10
#define QUANTUM 5
#define READY 1
#define WAIT 0
#define MAX_TIK 10000

// Data Structure & Fucntions 
struct process {
  long pid;
  int status;
  int remain_quantum;
  int remain_io;

  process(long p, int s, int r_q, int r_io)
      : pid(p), status(s), remain_quantum(r_q), remain_io(r_io){};
} typedef process;

struct parent_process {
  long pid;
  int remaining_io_time;

  parent_process() :pid(0), remaining_io_time(0){};
};

struct child_process {
  long pid;
  int cpu_burst;
  int io_burst;
  child_process(int p, int c, int i) : pid(p), cpu_burst(c), io_burst(i){};
};

struct msg_buffer {
  long m_type;
  pid_t pid;
  int io_msg;
};

FILE *fp;
pid_t pid;
int total_tik;
struct parent_process processes[CHILDNUMBER]; // 부모프로세스가 관리할 자식 프로세스들
struct child_process *pchild; 
struct sigaction usr_sigact1; // 사용자 정의신호에 따른 동작 1
struct sigaction usr_sigact2; // 사용자 정의신호에 따른 동작 2
deque<process> run_queue;
deque<process> wait_queue;
deque<process> done_queue;

void burst_handler(int sig); // io_burst가 종료되면 임의의 수로 새로 할당하는 함수.
void remain_cpu_handler(int sig); // cpu_burst를 관리하고 이에 따라서 io meg 를 보냄. 
void per_tik_handler(int sig); // 매 tik 마다 큐의 상태에 따라서 신호 발생시킴.
void parent_work(); // 메세지를 기다리며 io 발생시 대기 큐로 이동시킴
void child_work(); // 신호에 따라 burst_handler와 reamin_cpu_handler가 실행되어 프로세스가 관리됨. 
void timer_start(); // 타이머를 동작시킴.
int process_num(long pid); 

int main() {
  total_tik = 0; //set start time.
  fp = fopen("Round_Robin_Dump.txt", "w"); // output file

  for (int i = 0; i < CHILDNUMBER; i++) {
    pid = fork();
    if (pid == -1) {
      exit(1);
    } else if (pid == 0) {  // child proces
      child_work();
      return 0;
    } else {  // parent process
      processes[i].pid = pid;
      run_queue.push_back(process(pid, READY, QUANTUM, 0));
    }
  }
  timer_start();
  parent_work();
  kill(pid, SIGKILL);
  delete &wait_queue;
  delete &run_queue;
  return 0;
}

void parent_work() {
  struct msg_buffer msg;
  int msqid;
  key_t key;
  key = ftok(".", 'A');
  if (-1 == (msqid = msgget(key, IPC_CREAT | 0644))) {
    perror("msgget() failed");
    exit(1);
  }

  while (1) {
    if (msgrcv(msqid, &msg, sizeof(struct msg_buffer), 4, 0) == -1) {
      // perror("msgrcv() failed");
    } else {
      if (-1 == process_num(msg.pid)) {
        break;
      }
      if (!run_queue.empty()) {
        process tmp_node = run_queue.front();
        run_queue.pop_front();
        tmp_node.remain_io = msg.io_msg;
        tmp_node.status = WAIT;
        wait_queue.push_back(tmp_node);
      }
    }
  }
}

void child_work() {
  struct sigaction old1; // 저장을 위한 임시 신호
  struct sigaction old2; // 저장을 위한 임시 신호
  sigemptyset(&old1.sa_mask);
  sigemptyset(&old2.sa_mask);
  srand(time(NULL));
  pchild = new child_process(getpid(), (rand() % 20 + getpid() % 9 + 6),
                   (rand() % 20 + getpid() % 5 + 4)); // 난수 생성

  memset(&usr_sigact1, 0, sizeof(usr_sigact1));
  usr_sigact1.sa_handler = &remain_cpu_handler; 
  sigemptyset(&usr_sigact1.sa_mask);
  sigaction(SIGUSR1, &usr_sigact1, 0); //SIGUSR 1에따른 remain_cpu_handler 발생.

  memset(&usr_sigact2, 0, sizeof(usr_sigact2));
  usr_sigact2.sa_handler = (void (*)(int))burst_handler;

  sigemptyset(&usr_sigact2.sa_mask);
  sigaction(SIGUSR2, &usr_sigact2, 0); //SIGUSR 2에 따른 burst_handler 발생.
  while(1);
  sigaction(SIGUSR1, &old1, NULL);
  sigaction(SIGUSR2, &old2, NULL);
}

void remain_cpu_handler(int sig) {
  int msqid;
  struct msg_buffer msg;
  key_t key;
  key = ftok(".", 'A');

  if (-1 == (msqid = msgget(key, IPC_CREAT | 0644))) { 
    perror("msgget() failed");
    exit(1);
  } else {
    if (pchild->cpu_burst != 0) {
      pchild->cpu_burst--; // 현재 실행중인 프로세스 cpu burst time 감소
      printf("REMAINING CPU BURST : %d\n", pchild->cpu_burst);
      fprintf(fp, "REMAINING CPU BURST : %d\n", pchild->cpu_burst);
      if (pchild->cpu_burst == 0) {
        // cpu burst time을 다 사용하면 자식프로세스는
        // 부모 프로세스에게 next I/O-burst time을 보낸다.
        msg.m_type = 4;
        msg.pid = getpid();
        msg.io_msg = pchild->io_burst;
        msgsnd(msqid, &msg, sizeof(struct msg_buffer), 0);
      }
    }
  }
}

void burst_handler(int sig) {
  srand(time(NULL));
  pchild->cpu_burst = (rand() % 20 * getpid() % 9 + 1);
  pchild->io_burst = 6;(rand() % 20 * getpid() % 7 + 1);
}

void timer_start() {
  struct sigaction alrm_sigact;
  struct itimerval timer;

  sigemptyset(&alrm_sigact.sa_mask);
  memset(&alrm_sigact, 0, sizeof(alrm_sigact));
  alrm_sigact.sa_handler = &per_tik_handler; // 매 시간마다 발생되는 신호
  sigaction(SIGALRM, &alrm_sigact, NULL);

  timer.it_value.tv_sec = 0;
  timer.it_value.tv_usec = 30000; // 처음 신호까지의 시간
  timer.it_interval.tv_sec = 0;
  timer.it_interval.tv_usec = 30000; // 다음 부터 주기적인 신호 시간

  setitimer(ITIMER_REAL, &timer, NULL); // 타이머 시작
}

void per_tik_handler(int sig) {
  total_tik++;
  printf("-------------------------------------------------\n");
  printf("Tik Times        : %d\n", total_tik);
  fprintf(fp,
          "---------------------------------------------------\nTime : %d\n",
          total_tik);
  if (!run_queue.empty()) {
    printf("Current Process :  %ld\n", run_queue.front().pid);
    fprintf(fp, "Current Process :  %ld\n", run_queue.front().pid);
    printf("RUN QUEUE DUMP  : ");
    fprintf(fp, "RUN QUEUE DUMP  : ");
    for (int i = 0; i < run_queue.size(); i++) {
      printf(" (%ld)", run_queue[i].pid);
      fprintf(fp, " (%ld)", run_queue[i].pid);
    }
    printf("\n");
    fprintf(fp, "\n");
  }

  if (!wait_queue.empty()) {
    printf("WAIT QUEUE DUMP : ");
    fprintf(fp, "WAIT QUEUE DUMP : ");
    for (int i = 0; i < wait_queue.size(); i++) {
      printf(" (%ld)", wait_queue[i].pid);
      fprintf(fp, " (%ld)", wait_queue[i].pid);
    }
    printf("\n");
    fprintf(fp, "\n");
  }
  if (!done_queue.empty()) {
    printf("DONE QUEUE DUMP : ");
    fprintf(fp, "DONE QUEUE DUMP : ");
    for (int i = 0; i < done_queue.size(); i++) {
      printf(" (%ld)", done_queue[i].pid);
      fprintf(fp, " (%ld)", done_queue[i].pid);
    }
    printf("\n");
    fprintf(fp, "\n");
  }

  if (!run_queue.empty()) {
    process &cur = run_queue.front();
    cur.remain_quantum--;
    if (total_tik >= MAX_TIK) {
      fprintf(fp, "TIME OUT : TIK EXCEEDED(10000)\n");
      for (int i = 0; i < CHILDNUMBER; i++) {
        kill(processes[i].pid, SIGKILL);
      }
      kill(getpid(), SIGKILL);
    }
    if (cur.remain_quantum == 0 ) {
      process tmp = run_queue.front();
      run_queue.pop_front();
      tmp.remain_quantum = QUANTUM;
      done_queue.push_back(tmp);

      if (run_queue.empty()) {
        deque<process> tmp_q;
        tmp_q = run_queue;
        run_queue = done_queue;
        done_queue = tmp_q;
      } 
    }
    kill(cur.pid, SIGUSR1);
  }

  if (!wait_queue.empty()) {
  deque<process>::iterator cur = wait_queue.begin();
    for (int i=0; i<wait_queue.size(); i++) {
      cur->remain_io--;
      deque<process>::iterator tmp = cur;
      if (cur->remain_io <= 0) {
        tmp->status = READY;
        tmp->remain_quantum = QUANTUM;
        run_queue.push_back(*tmp);
        wait_queue.pop_front();
        kill(tmp->pid, SIGUSR2);
      }
      cur++;
    }
  }
}

int process_num(long pid) {
  for (int i = 0; i < CHILDNUMBER; i++) {
    if (pid == processes[i].pid) {
      return i;
    }
  }
  return -1;
}