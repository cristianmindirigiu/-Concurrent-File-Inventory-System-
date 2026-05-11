/**
 * @file ipc_format.h
 * @brief Definițiile structurilor pentru memoria partajată (IPC) și baza de date binară.
 * * Acest fișier definește layout-ul memoriei mapate (mmap) folosite pentru 
 * comunicarea între procesul Manager și procesele Worker. Utilizează semafoare
 * POSIX pentru sincronizare și buffer-uri circulare (Ring Buffers) pentru transferul de date.
 */

#ifndef IPC_FORMAT_H
#define IPC_FORMAT_H

#include <stdint.h>
#include <stdbool.h>
#include <semaphore.h>

#define IPC_VERSION 1  // Versiunea formatului IPC
#define MAX_PATH_LEN 1024  // Lungime maximă cale fișier
#define MAX_WORKERS 16  // Numărul maxim de workeri suportați
#define JOB_QUEUE_CAPACITY 256  // Capacitatea cozii de directoare
#define RESULT_CHANNEL_CAPACITY 64  // Capacitatea canalului de rezultate per worker
#define SHA256_HEX_LEN 65  // 64 caractere hex + terminatorul '\0'

#define INV4_MAGIC "INV4"
#define INV4_VERSION 1

/**
 * @struct file_record_t
 * @brief Reține metadatele unui fișier descoperit în timpul scanării.
 */
typedef struct { 
    char path[MAX_PATH_LEN]; // Calea absolută
    uint64_t size; // Mărime în bytes
    int64_t mtime; // Timestamp al ultimei modificări
    uint32_t mode; // Permisiunile din struct stat
    uint32_t uid; // User ID
    uint32_t gid; // Group ID
    char sha256[SHA256_HEX_LEN]; // Hash SHA256
} file_record_t;

/**
 * @struct job_t
 * @brief Reprezintă un task (director de scanat) din coada de job-uri.
 */
typedef struct {
    char path[MAX_PATH_LEN]; // Calea absolută a directorului
    uint32_t depth; // Adâncimea curentă în arbore
} job_t;

/**
 * @struct worker_stats_t
 * @brief Reține metricele de performanță în timp real pentru un worker.
 */
typedef struct {
    uint32_t worker_id; // ID-ul logic (0, 1, 2...)
    int32_t pid; // PID-ul procesului
    int32_t exit_status; // Codul de ieșire al workerului
 
    uint64_t jobs_processed; // Număr de directoare procesate
    uint64_t files_emitted; // Număr de fișiere (file_records) trimise
    uint64_t bytes_sum; // Suma dimensiunilor fișierelor
     
    uint64_t time_ms; // Timp de execuție real (Wall time) în ms
    uint64_t user_cpu_t; // Timp CPU în User Mode (microsecunde)
    uint64_t sys_cpu_t; // Timp CPU în Kernel Mode (microsecunde)

    bool active; // Flag care indică dacă workerul mai este în execuție
} worker_stats_t;

/**
 * @struct job_queue_t
 * @brief Buffer circular (Producer-Consumer) pentru directoarele ce trebuie scanate.
 * @note Este protejat de `mutex` și controlat prin semafoarele `slots_libere` și `slots_ocupate`.
 */
typedef struct {
    job_t buffer[JOB_QUEUE_CAPACITY]; // Array fix de job-uri

    uint32_t head; // Indexul următoarei citiri
    uint32_t tail; // Indexul următoarei scrieri
    uint32_t count; // Câte job-uri sunt active în coadă

    sem_t mutex; // Asigură excluderea mutuală pentru head, tail și count
    sem_t slots_libere; // Contorizează locurile libere (evită overflow)
    sem_t slots_ocupate; // Contorizează job-urile disponibile (evită underflow)

    uint32_t jobs_in_progress; // Numărul de workeri care scanează activ un director
} job_queue_t;

/**
 * @struct result_channel_t
 * @brief Canal de comunicare (1-la-1) între un Worker specific și Manager.
 * @note Fiecare worker scrie doar în canalul său, eliminând contenția (blocajele).
 */
typedef struct { 
    file_record_t buffer[RESULT_CHANNEL_CAPACITY];
       
    uint32_t head;
    uint32_t tail;
    uint32_t count;

    sem_t mutex;
    sem_t slots_libere;
    sem_t slots_ocupate;
    
    bool closed; // Setat pe true de worker la terminarea ciclului de viață
} result_channel_t;

/**
 * @struct ipc_shared_t
 * @brief Structura principală mapată în memoria partajată.
 */
typedef struct {
    // --- Header ---    
    char magic[4]; // Fixat la "IPC4" pentru validare
    uint32_t version;
    uint32_t num_workers;
    bool terminate_flag; // Semnal universal de oprire

    // --- Sincronizare Globală ---
    sem_t header_mutex;

    // --- Conducte de Date ---
    job_queue_t job_queue; // Canal Manager -> Workeri (și Worker -> Worker)
    result_channel_t result_channels[MAX_WORKERS]; // Canale Workeri -> Manager

    // --- Statistici ---
    worker_stats_t worker_stats[MAX_WORKERS]; 
} ipc_shared_t;

// structuri pentru baza de data binara
/**
 * @struct inv4_header_t
 * @brief Antetul fișierului binar generat la finalul inventarierii.
 */
typedef struct {
    char magic[4]; // Fixat la "INV4"
    uint32_t format_version; // Versiunea formatului (1)
    uint8_t complete; // 0 - în progres, 1 - valid/finalizat
    uint64_t file_record_count; // Numărul total de fișiere persistate
    uint32_t worker_count; // Numărul de workeri utilizați
} inv4_header_t;

/**
 * @struct inv4_worker_stat_t
 * @brief Trailer-ul cu statisticile finale ale fiecărui worker, stocat la finalul DB-ului.
 */
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