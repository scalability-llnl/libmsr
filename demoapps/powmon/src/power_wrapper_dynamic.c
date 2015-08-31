#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "ghighres.c"
#include "rapl.h"
#include "../config.h"

#include <sys/ipc.h>
#include <sys/shm.h>


// RAPL
static double total_joules=0.0;
static double limit_joules=0.0;
static double max_watts=0.0;
static double min_watts=1024.0;

static unsigned long start;
static unsigned long end;
static FILE* logfile=NULL;
static double watt_cap=0.0;
static volatile int poll_num=0;
static volatile int poll_dir=5;

static pthread_mutex_t mlock;
static int* shmseg;
static int shmid;

static int running = 1;

#include "common.c"

void* power_set_measurement(void* arg) {
    unsigned long poll_num=0;
    struct mstimer timer;
    set_rapl_power(watt_cap,watt_cap);
    double watts = watt_cap;
    // according to the Intel docs, the counter wraps a most once per second
    // 100 ms should be short enough to always get good information
    init_msTimer(&timer, 1500);
    init_data();
    start = now_ms();

    poll_num++;
    timer_sleep(&timer);
    while(running) {
        take_measurement();
        if(poll_num%5==0) {
            if(watts >= watt_cap) {
                poll_dir = -5;
            }
            if(watts <= 30) {
                poll_dir = 5;
            }
            watts += poll_dir;
            set_rapl_power(watts,watts);
        }
        poll_num++;
        timer_sleep(&timer);
    }
}

int main(int argc, char** argv) {
    printf("starting wrapper\n");
    if( argc < 3 ) {
        printf("usage: %s <watt_cap> <executable> <args>...\n",argv[0]);
        return 1;
    }

    if( highlander()) {
        // Start the log file
        int logfd;
        char hostname[64];
        gethostname(hostname,64);

        char* fname;
        asprintf(&fname,"%s.power.dat",hostname);

        logfd = open(fname, O_WRONLY|O_CREAT|O_EXCL|O_NOATIME|O_NDELAY, S_IRUSR|S_IWUSR);
        if( logfd < 0 ) {
            printf( "Fatal Error: %s on %s cannot open the appropriate fd.\n",argv[0], hostname);
            return 1;
        }
        logfile = fdopen(logfd,"w");

        read_rapl_init();

        // Set the cap
        watt_cap = strtod(argv[1], NULL);
        set_rapl_power(watt_cap, watt_cap);

        // Start power measurement thread
        pthread_t mthread;
        pthread_create(&mthread, NULL, power_set_measurement, NULL);

        // Fork
        pid_t app_pid = fork();
        if(app_pid == 0) {
            // I'm the child
            execvp(argv[2],&argv[2]);
            return 1;
        }
        // Wait
        waitpid(app_pid, NULL, 0);
        sleep(1);

        highlander_wait();
        
        // Stop power measurement thread
        running = 0;
        take_measurement();
        end = now_ms();

        // Output summary data
        char* msg;
        asprintf(&msg, "host: %s\npid: %d\ntotal: %lf\nallocated: %lf\nmax_watts: %lf\nmin_watts: %lf\nruntime ms: %lu\n,start: %lu\nend: %lu\n",hostname,app_pid,total_joules,limit_joules,max_watts,min_watts,end-start,start,end);

        fprintf(logfile,"%s",msg);
        fclose(logfile);
        close(logfd);
        shmctl(shmid,IPC_RMID,NULL);
        shmdt(shmseg);
    } else {
        // Fork
        pid_t app_pid = fork();
        if(app_pid == 0) {
            // I'm the child
            execvp(argv[2],&argv[2]);
            return 1;
        }
        // Wait
        waitpid(app_pid, NULL, 0);
     
        highlander_wait();
    }

    return 0;
}
