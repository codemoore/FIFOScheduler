#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <sys/queue.h>
#include <unistd.h>

#define JOB_LIMIT 4 //Job limit per job thread
#define JOB_TC 4// Job thread count
#define CPU_TC 8 //cpu thread count
#define IO_TC 4 //io thread count

pthread_mutex_t id_lock; // = PTHREAD_MUTEX_INITILIZER
pthread_mutex_t cpu_lock;
pthread_mutex_t io_lock;
pthread_mutex_t fin_lock;
pthread_mutex_t jf_lock;
pthread_mutex_t op_lock;

pthread_t job_thread[4];
pthread_t cpu_thread[8];
pthread_t io_thread[4];

int curId = 0;
int job_runing = 1;
int jobs_finished = 0;
int op_finished = 0;

TAILQ_HEAD(q1, Job) cpu_head;
TAILQ_HEAD(q2, Job) io_head;
TAILQ_HEAD(q3, Job) fin_head;

struct q1 *qStruct1;
struct q2 *qStruct2;
struct q3 *qStruct3;

typedef struct Job
{
	int job_id;
	int phaseC;
	int current_phase;
	int *type;
	int *duration;
	
	TAILQ_ENTRY(Job) jobs;
} Job;

typedef struct Arg 
{
	int n;
} Arg;

int nextId();
Job *createRandomJob();

int getOpCount() {
	pthread_mutex_lock(&op_lock);
	int c = op_finished;
	pthread_mutex_unlock(&op_lock);
	return c;
}

int getJobFinished() {
	pthread_mutex_lock(&jf_lock);
	int c = jobs_finished;
	pthread_mutex_unlock(&jf_lock);
	return c;
}

void incrementOP(){
	pthread_mutex_lock(&op_lock);
	op_finished++;
	pthread_mutex_unlock(&op_lock);
}

void incrementJobFin(){
	pthread_mutex_lock(&jf_lock);
	jobs_finished++;
	pthread_mutex_unlock(&jf_lock);
}

void cpu_enque(Job *j)
{
	pthread_mutex_lock(&cpu_lock);
	TAILQ_INSERT_TAIL(&cpu_head, j, jobs);
	printf("\n Job %d added to cpu queue\n", j->job_id);	
	pthread_mutex_unlock(&cpu_lock);
}

Job *cpu_deque()
{
	pthread_mutex_lock(&cpu_lock);
	Job *j = cpu_head.tqh_first;
	if (j) {
		TAILQ_REMOVE(&cpu_head, cpu_head.tqh_first, jobs);
		printf("\n Job %d removed from cpu queue\n", j->job_id);
	} 
	pthread_mutex_unlock(&cpu_lock);
	return j;
}

void io_enque(Job *j)
{
	pthread_mutex_lock(&io_lock);
	TAILQ_INSERT_TAIL(&io_head, j, jobs);
	pthread_mutex_unlock(&io_lock);
	printf("\n Job %d added to io queue\n", j->job_id);	
}

Job *io_deque()
{
	pthread_mutex_lock(&io_lock);
	Job *j = io_head.tqh_first;
	if(j) {
		TAILQ_REMOVE(&io_head, io_head.tqh_first, jobs);
		printf("\n Job %d removed from io queue\n", j->job_id);	
	} 
	pthread_mutex_unlock(&io_lock);
	return j;
}

void fin_enque(Job *j)
{
	pthread_mutex_lock(&fin_lock);
	TAILQ_INSERT_TAIL(&fin_head, j, jobs);
	pthread_mutex_unlock(&fin_lock);
	//printf("\n Job %d added to finished queue\n", j->job_id);	
}

Job *fin_deque()
{
	pthread_mutex_lock(&fin_lock);
	Job *j = fin_head.tqh_first;
	if(j) {
		TAILQ_REMOVE(&fin_head, fin_head.tqh_first, jobs);
		//printf("\n Job %d removed from io queue\n", j->job_id);
	} 
	pthread_mutex_unlock(&fin_lock);
	return j;
}

void *cpu(void *arg)
{
	int n = ((Arg *)arg)->n;
	while(getOpCount() < JOB_TC * JOB_LIMIT) {	
		Job *j = cpu_deque();
		if (j) {
			printf("\nCPU %d Running job %d for %d secs\n", n, j->job_id, j->duration[j->current_phase]);
			//sleep(j->duration[j->current_phase]);
			j->current_phase++;	
			if(j->phaseC > j->current_phase) {
				if(j->type[j->current_phase]) { //if 1 then io
					io_enque(j);
				} else { //else 0 then cpu
					cpu_enque(j);
				}
			} else { //add to finished queue 
				fin_enque(j);
				incrementOP();
				printf("\n Job %d added to finished queue\n", j->job_id);				
			}	
		}
	}
	printf("\nCPU %d stoping\n", n);
	return NULL;
}

void *io(void *arg)
{
	int n = ((Arg *)arg)->n;
	while(getOpCount() < JOB_TC * JOB_LIMIT) {	
		Job *j = io_deque();
		
		if(j) {
			printf("\nIO %d Running job %d for %d secs\n", n, j->job_id, j->duration[j->current_phase]);
			//sleep(j->duration[j->current_phase]);		
			j->current_phase++;		
			if(j->phaseC > j->current_phase) {
				if(j->type[j->current_phase]) { //if 1 then io
					io_enque(j);
				} else { //else 0 then cpu
					cpu_enque(j);
				}
			} else { //add to finished queue 
					fin_enque(j);
					incrementOP();
					printf("\n Job %d added to finished queue\n", j->job_id);				
			}
		}
	}
	printf("\nIO %d stoping\n", n);
	return NULL;
}

void *job_creator(void *arg)
{
	int n = ((Arg *)arg)->n;
	printf("JOb creator %d", n);
	int *created_jobs = (int *)malloc(sizeof(int) * JOB_LIMIT);
	int jobC = 0;
	int i;
	while (getJobFinished() < JOB_TC * JOB_LIMIT) {
		//free job if it's finished and from this 
		Job *jf = fin_deque();	
		if (jf){
			for(i = 0; i < jobC; i++) {
				if(created_jobs[i] == jf->job_id) {
					printf("\n Job %d removed from finished queue\n", jf->job_id);	
					printf("\nJob Creator %d freeing Job %d\n", n, jf->job_id);
					free(jf);
					incrementJobFin();
				} else {
					fin_enque(jf);	
				}
			}
			jf = fin_deque();
		}
		
		if(job_runing) {			
			Job *j = createRandomJob();
			created_jobs[jobC] = j->job_id;
			printf("\nJob Creator %d creating job %d \n", n, j->job_id);
			cpu_enque(j);
			jobC++;
			if(jobC >= JOB_LIMIT) {
				printf("\nJob Creator %d stoping job creation\n", n);
				job_runing = 0;
			}
			sleep(2);
		}	
	}
	return NULL;
}


Job *createRandomJob()
{
	int i;
	int phaseC = (rand() % 4) + 3;
	if(!(phaseC % 2)) {//make sure phaseC is odd	
		phaseC++;
	}
	
	Job *j = (Job *)malloc(sizeof(Job));
	j->job_id = nextId();
	j->phaseC = phaseC;
	j->duration = malloc(sizeof(int) * phaseC);
	j->type = malloc(sizeof(int) * phaseC);

	for(i=0; i<phaseC; i++) {
		j->duration[i] = (rand() % 15) + 2;
		j->type[i] = i % 2;
	}	
	return j;
}

int nextId() 
{
	int temp;
	pthread_mutex_lock(&id_lock);
	temp = curId++;
	pthread_mutex_unlock(&id_lock);
	return temp;
}


int main (int argc, char *argv[])
{
	srand(time(NULL));
	TAILQ_INIT(&cpu_head);
	TAILQ_INIT(&io_head);
	TAILQ_INIT(&fin_head);
	int i;
	for (i = 0; i < JOB_TC; i++) {
		Arg *args = (Arg *)malloc(sizeof(Arg));
		args->n = i;
		pthread_create(&job_thread[i], NULL, job_creator, (void *)args);
	}
	for (i = 0; i < CPU_TC; i++) {
		Arg *args = (Arg *)malloc(sizeof(Arg));
		args->n = i;
		pthread_create(&cpu_thread[i], NULL, cpu, (void *)args);
	}
	for (i = 0; i < IO_TC; i++) {
		Arg *args = (Arg *)malloc(sizeof(Arg));
		args->n = i;
		pthread_create(&io_thread[i], NULL, io, (void *)args);
	}
	
	for (i = 0; i < JOB_TC; i++) {
		pthread_join(job_thread[i], NULL);
	}
	for (i = 0; i < CPU_TC; i++) {
		pthread_join(cpu_thread[i], NULL);
	}	
	for (i = 0; i < IO_TC; i++) {
		pthread_join(io_thread[i], NULL);
	}
	return 0;
}

