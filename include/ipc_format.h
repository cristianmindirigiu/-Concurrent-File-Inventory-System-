#ifndef IPC_FORMAT_H
#define IPC_FORMAT_H

#include <stdint.h>
#include <stdbool.h>
#include <semaphore.h> //pt semafoare


#define IPC_VERSION 1  //versiunea formatului ipc
#define MAX_PATH_LEN 1024  // lungime maxima cale fisier
#define MAX_WORKERS 16  // nr maximum de workeri
#define JOB_QUEUE_CAPACITY 256  // cate job-uri pot sta in coada
#define RESULT_CHANNEL_CAPACITY 64  //cate records in canalul fiecarui worker
#define SHA256_HEX_LEN 65  //64 hex chars + '\0'


/* * Aceasta este structura memoriei partajate pentru Commit 1.
 * În Commit-ul 2 o vom extinde masiv prin adăugarea Job Queue-ului, 
 * Canalelor de Rezultate și a Semafoarelor de sincronizare.
 */
typedef struct{ 
    char path[MAX_PATH_LEN]; //calea absoluta
    uint64_t size; //marime in bytes
    int64_t mtime; //timestamp al ultimei mod
    uint32_t mode; // permisiunile din struct stat
    uint32_t uid; // user id
    uint32_t gid; //group id
    char sha256[SHA256_HEX_LEN]; // hash sha256
} file_record_t; // un fisier descoperit

typedef struct{
    char path[MAX_PATH_LEN]; // calea absoluta a directorului
    uint32_t depth; // adancimea in arbore
} job_t;

typedef struct{
    uint32_t worker_id; //id-ul logic (0,1,2...)
    int32_t pid; // pid-ul procesului
    int32_t exit_status; // ce a returnat workerul
 
    uint64_t jobs_processed; // nr de joburi procesate
    uint64_t files_emitted; // cate file_records a trimis
    uint64_t bytes_sum; //suma size-urilor fisierelor
     
    uint64_t time_ms; //timp real in ms
    uint64_t user_cpu_t; // timp cpu user in microsecunde
    uint64_t sys_cpu_t; // timp cpu kernel in microsecunde

    bool active; //daca workerul mai ruleaza
} worker_stats_t;

//coada de directoare de scanat
typedef struct{
    job_t buffer[JOB_QUEUE_CAPACITY]; //array fix de joburi

    uint32_t head; //indexul urmatoarei citiri
    uint32_t tail; //indexul urmatoarei scrieri
    uint32_t count; //cate joburi sunt in coada

    sem_t mutex; //exclusiv pt head tail count
    sem_t slots_libere; // nr locuri libere 
    sem_t slots_ocupate; // nr locuri ocupate

    uint32_t jobs_in_progress; //cati workeri lucreaza acum


} job_queue_t;

 


typedef struct{ // canal de file_records pt un worker (fiecare worker scrie in zona lui apoi managerul citeste din toate
    file_record_t buffer[RESULT_CHANNEL_CAPACITY];
       
    uint32_t head;
    uint32_t tail;
    uint32_t count;

    sem_t mutex;
    sem_t slots_libere;
    sem_t slots_ocupate;
	
    bool closed; //worker seteaza true cand termina

} result_channel_t;



typedef struct {
    // header	
    char magic[4]; // Fixat la "IPC4" pentru validare
    uint32_t version;
    uint32_t num_workers;
    bool terminate_flag;

    //sincronizare 
    sem_t header_mutex;

    //job queue
    job_queue_t job_queue;

    //rezultate per worker
    result_channel_t result_channels[MAX_WORKERS];

    //statistici per worker
    worker_stats_t worker_stats[MAX_WORKERS]; 

} ipc_shared_t;

#endif
