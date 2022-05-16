#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
    // set_cpu(5);
    // int pids[6];
    // int f_pid = getpid();

    int n_forks = 6;

    int pids[n_forks];

    for(int i = 0; i < n_forks; i++){
        if (fork() != 0){
            pids[i] = 1;
        }
        
    }
    for (int i=0; i< 2000; i++);


    // pids[getpid() - 2] = 1;

    wait(0);
    // printf("%d ", get_cpu());
    if (pids[0] && pids[1] && pids[2] && pids[3] && pids[4] && pids[5]){
        for (int i=0; i < 8; i++){
            printf("%d ",cpu_process_count(i));
        }
    }
    

    // if(getpid() == f_pid){
    //     for (int i = 0; i < 64; i++){
    //         printf("%d  ", pids[i]);
    //     }
    // }
    // if (getpid() % 10 == 0)
    // {
    //     printf("_");
    // }

    // if (getpid() > 120)
    // {
    //     printf("%d ", getpid() % 10);
    // }
    
    
    exit(0);
}