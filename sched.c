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

FILE *fileHandle;

int curId = 0;
int job_runing = 1;
int ops_finished = 0;

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
	int c = ops_finished;
	pthread_mutex_unlock(&op_lock);
	return c;
}

/*int getJobFinished() {
	pthread_mutex_lock(&jf_lock);
	int c = jobs_finished;
	pthread_mutex_unlock(&jf_lock);
	return c;
}*/

void incrementOP(){
	pthread_mutex_lock(&op_lock);
	ops_finished++;
	pthread_mutex_unlock(&op_lock);
}

/*void incrementJobFin(){
	pthread_mutex_lock(&jf_lock);
	jobs_finished++;
	pthread_mutex_unlock(&jf_lock);
}*/

void cpu_enque(Job *j)
{
	pthread_mutex_lock(&cpu_lock);
	TAILQ_INSERT_TAIL(&cpu_head, j, jobs);
	fprintf(fileHandle, "\n Job %d added to cpu queue\n", j->job_id);	
	pthread_mutex_unlock(&cpu_lock);
}

Job *cpu_deque()
{
	pthread_mutex_lock(&cpu_lock);
	Job *j = cpu_head.tqh_first;
	if (j) {
		TAILQ_REMOVE(&cpu_head, cpu_head.tqh_first, jobs);
		fprintf(fileHandle, "\n Job %d removed from cpu queue\n", j->job_id);
	} 
	pthread_mutex_unlock(&cpu_lock);
	return j;
}

void io_enque(Job *j)
{
	pthread_mutex_lock(&io_lock);
	TAILQ_INSERT_TAIL(&io_head, j, jobs);
	pthread_mutex_unlock(&io_lock);
	fprintf(fileHandle, "\n Job %d added to io queue\n", j->job_id);	
}

Job *io_deque()
{
	pthread_mutex_lock(&io_lock);
	Job *j = io_head.tqh_first;
	if(j) {
		TAILQ_REMOVE(&io_head, io_head.tqh_first, jobs);
		fprintf(fileHandle, "\n Job %d removed from io queue\n", j->job_id);	
	} 
	pthread_mutex_unlock(&io_lock);
	return j;
}

void fin_enque(Job *j)
{
	pthread_mutex_lock(&fin_lock);
	TAILQ_INSERT_TAIL(&fin_head, j, jobs);
	pthread_mutex_unlock(&fin_lock);
}

Job *fin_deque()
{	
	pthread_mutex_lock(&fin_lock);
	Job *j = fin_head.tqh_first;
	if(j) {
		TAILQ_REMOVE(&fin_head, fin_head.tqh_first, jobs);
	} 
	pthread_mutex_unlock(&fin_lock);
	return j;
}

void searchAndRemove(int *a, int *count, Job *j)
{
	int i, k;
	for(i=0; i<count[0]; i++) {
		if(a[i] == j->job_id) {
			//remove id from a, and move everything over
			count[0]--;
			for(k = i; k < count[0] - 1; j++) {
				a[k] = a[k+1];
			}
			//free job
			fprintf(stdout, "Freeing Job %d", j->job_id);
			fprintf(fileHandle, "Freeing Job %d", j->job_id);
			//incrementJobFin();
			free(j);
			break;
		}
		fin_enque(j);
	}
}

void *cpu(void *arg)
{
	int n = ((Arg *)arg)->n;
	while(getOpCount() < JOB_TC * JOB_LIMIT) {	
		Job *j = cpu_deque();
		if (j) {
			fprintf(fileHandle, "\nCPU %d Running job %d for %d secs\n", n, j->job_id, j->duration[j->current_phase]);
			sleep(j->duration[j->current_phase]);
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
				fprintf(fileHandle, "\n Job %d added to finished queue\n", j->job_id);		
			}	
		}
	}
	fprintf(fileHandle, "\nCPU %d stoping\n", n);
	return NULL;
}

void *io(void *arg)
{
	int n = ((Arg *)arg)->n;
	while(getOpCount() < JOB_TC * JOB_LIMIT) {	
		Job *j = io_deque();
		
		if(j) {
			fprintf(fileHandle, "\nIO %d Running job %d for %d secs\n", n, j->job_id, j->duration[j->current_phase]);
			sleep(j->duration[j->current_phase]);
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
					fprintf(fileHandle, "\n Job %d added to finished queue\n", j->job_id);
					printf("Jobs Finished: %d", getOpCount());		
			}
		}
	}
	fprintf(fileHandle, "\nIO %d stoping\n", n);
	return NULL;
}

void *job_creator(void *arg)
{
	int n = ((Arg *)arg)->n;
	int *created_jobs = (int *)malloc(sizeof(int) * JOB_LIMIT);
	int *jobC = (int *)malloc(sizeof(int));
	jobC[0] = 0;	
	while (getOpCount() < JOB_TC * JOB_LIMIT) {
		//free job if it's finished and from this		
		Job *jf = fin_deque();	
		if (jf){
			searchAndRemove(created_jobs, jobC, jf);
		}		
		if(job_runing) {						
			Job *j = createRandomJob();
			created_jobs[jobC[0]] = j->job_id;
			fprintf(fileHandle, "\nJob Creator %d creating job %d \n", n, j->job_id);
			cpu_enque(j);
			jobC++;
			if(jobC[0] >= JOB_LIMIT) {
				fprintf(fileHandle, "\nJob Creator %d stoping job creation\n", n);
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
		j->duration[i] = (rand() % 10) + 1;
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
	fileHandle = fopen("sched.txt", "w");
	//fileHandle = stdout;
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

