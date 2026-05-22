//acest fisier defineste layout-ul memoriei mapate folosite pentru comunicarea intre procesul Manager si procesul Worker si utilizeaza semafoare POSIX pentru sincronizare

#ifndef IPC_FORMAT_H
#define IPC_FORMAT_H

#include <stdint.h>
#include <stdbool.h>
#include <semaphore.h>

#define IPC_VERSION 1  //versiunea formatului ipc
#define MAX_PATH_LEN 1024  //lungime maxima cale fisier
#define MAX_WORKERS 16  //numarul maxim de workeri
#define JOB_QUEUE_CAPACITY 256  //cate job-uri pot sta in coada
#define RESULT_CHANNEL_CAPACITY 64  //cate records in canalul fiecarui worker
#define SHA256_HEX_LEN 65  //64 hex chars + '\0'

#define INV4_MAGIC "INV4"
#define INV4_VERSION 1

#define DB_START 0
#define DB_PARTIAL_COMPLET 1
#define DB_COMPLET 2
//retine metadatele unui fisier descoperit in timpul scanarii
typedef struct { 
    char path[MAX_PATH_LEN]; //calea absoluta
    uint64_t size; // Marime in bytes
    int64_t mtime; // timestamp al ultimei modificari
    uint32_t mode; // permisiunile din struct stat
    uint32_t uid; // user id
    uint32_t gid; // group ID
    char sha256[SHA256_HEX_LEN]; // Hash SHA256
} file_record_t;


typedef struct {
    char path[MAX_PATH_LEN]; // calea absoluta a directorului
    uint32_t depth; // adancimea curenta in arbore
} job_t;

//statistici worker
typedef struct {
    uint32_t worker_id; //id-ul logic (0,1,..)
    int32_t pid; // pid proces
    int32_t exit_status; //codul de iesire al workerului
 
    uint64_t jobs_processed; //nr de directoare procesate
    uint64_t files_emitted; //nr de fisiere trimise
    uint64_t bytes_sum; //suma size-urilor fisierelor
     
    uint64_t time_ms; //timp de executie real in ms
    uint64_t user_cpu_t; //timp cpu user in ms
    uint64_t sys_cpu_t; //timp cpu kernel in ms

    bool active; //daca workerul mai ruleaza
} worker_stats_t;

//coada de directoare de scanat
typedef struct {
    job_t buffer[JOB_QUEUE_CAPACITY]; //array fix de joburi

    uint32_t head; //indexul urmatoarei citiri
    uint32_t tail; //indexul urmatoarei scrieri
    uint32_t count; //cate joburi sunt in coada

    sem_t mutex; //exclusiv pentru head tail count
    sem_t slots_libere; //numar locuri libere
    sem_t slots_ocupate; //numar locuri ocupate

    uint32_t jobs_in_progress; //cati workeri lucreaza acum
} job_queue_t;


//canal de file_records pentru un worker (fiecare worker scrie in zona lui apoi managerul citeste din toate)
typedef struct { 
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
    //header    
    char magic[4]; //fixat la "IPC4" pentru validare
    uint32_t version;
    uint32_t num_workers;
    bool terminate_flag;//semnal de oprire

    //sincronizare
    sem_t header_mutex;

    //job queue
    job_queue_t job_queue;

    //rezultate per worker
    result_channel_t result_channels[MAX_WORKERS];

    //statistici per worker
    worker_stats_t worker_stats[MAX_WORKERS]; 
} ipc_shared_t;

// structuri pentru db
// header-ul db-ului final
typedef struct {
    char magic[4]; //INV4
    uint32_t format_version; // 1
    uint8_t complete; //0 - in progres , 1 - e gata
    uint64_t file_record_count; // nr fisiere
    uint32_t worker_count; //nr workeri
} inv4_header_t;

// statistici per worker in db
typedef struct {
    uint32_t worker_id;
    int32_t pid;
    int32_t exit_status;
    uint64_t jobs_processed;
    uint64_t files_emitted;
    uint64_t bytes_sum;
    uint64_t time_ms;
    uint64_t user_cpu_t;
    uint64_t sys_cpu_t;
} inv4_worker_stat_t;

#endif
