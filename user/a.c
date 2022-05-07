#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
    // int pids[64];
    // int f_pid = getpid();
    int n_forks = 7;
    for(int i = 0; i < n_forks; i++){
        fork();

    }

    // pids[getpid() - 2] = 1;
    wait(0);
    ////pushpush?
    // if(getpid() == f_pid){
    //     for (int i = 0; i < 64; i++){
    //         printf("%d  ", pids[i]);
    //     }
    // }
    // if (getpid() % 10 == 0)
    // {
    //     printf("_");
    // }
    if (getpid() > 120)
    {
        printf("%d ", getpid() % 10);
    }
    
    
    exit(0);
}