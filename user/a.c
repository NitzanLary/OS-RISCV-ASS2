#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
// #include "kernel/proc.h"




int
t1(){
    // int pids[64];
    // int f_pid = getpid();
    int n_forks = 6;
    for(int i = 0; i < n_forks; i++){
        fork();

    }

    // pids[getpid() - 2] = 1;
    wait(0);
    ////pushpush?
    // if(getpid() == f_pid){
    //     for (int i = 0; i < 64; i++){
    //         printf("%d ", pids[i]);
    //     }
    // }
    if (getpid() > 60)
    {
        printf("%d ", getpid() % 10);
    }
    return 0;
}

// int t2(){
//     struct proc_ll q = {-1, -1};
//     int n_forks = 2;
//     for (int i=0; i < n_forks; i++){
//         enque(&q, getpid());
//         fork();
//     }
// }

int
main(int argc, char *argv[])
{
    t1();
    exit(0);
}