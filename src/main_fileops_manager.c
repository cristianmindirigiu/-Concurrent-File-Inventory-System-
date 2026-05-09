#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "../include/ipc_format.h"
#include <limits.h>

int main(int argc, char *argv[]) {
    char *root_dir = NULL;
    int num_workers = 0;
    char *ipc_path = "data/ipc.mmap";
    char *db_path = "data/inventory.db";
    int max_depth = -1; //nespecificat - valoarea negativa = lipsa argument
    
    // 1. Parsarea argumentelor CLI
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--root") == 0 && i + 1 < argc) root_dir = argv[++i];
        else if (strcmp(argv[i], "--workers") == 0 && i + 1 < argc) num_workers = atoi(argv[++i]);
        else if (strcmp(argv[i], "--ipc") == 0 && i + 1 < argc) ipc_path = argv[++i];
        else if (strcmp(argv[i], "--db") == 0 && i + 1 < argc) db_path = argv[++i];
        else if (strcmp(argv[i], "--max-depth") == 0 && i+1 < argc){
            max_depth = atoi(argv[++i]);
        }
    }
    
    // Verificam argumentele minime in modul de inventariere
    if (!root_dir || num_workers < 1) {
        fprintf(stderr, "[Manager] Utilizare: %s --root <dir> --workers <N>\n", argv[0]);
        return 1;
    }

    printf("[Manager] Incepem inventarierea pe '%s' cu %d workeri.\n", root_dir, num_workers);

    // 2. Initializarea memoriei partajate IPC (mmap)
    int fd = open(ipc_path, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fd == -1) { 
        perror("[Manager] Eroare creare fisier IPC"); 
        return 1; 
    }
    
    size_t ipc_size = sizeof(ipc_shared_t);
    if (ftruncate(fd, ipc_size) == -1) { 
        perror("[Manager] Eroare ftruncate IPC"); 
        close(fd); return 1; 
    }
    
    ipc_shared_t *ipc = mmap(NULL, ipc_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ipc == MAP_FAILED) { 
        perror("[Manager] Eroare mmap"); 
        close(fd); return 1; 
    }
    close(fd); // Dupa mapare, nu mai avem nevoie de descriptor deschis

    // Initializam zonele de baza
    memcpy(ipc->magic, "IPC4", 4);
    ipc->num_workers = num_workers;
    ipc->terminate_flag = false;
    ipc->version = IPC_VERSION;

    //INITIALIZARE SEMAFOARE
    sem_init(&ipc->header_mutex, 1, 1);

    //job queue
    ipc->job_queue.head = 0;
    ipc->job_queue.tail = 0;
    ipc->job_queue.count = 0;
    ipc->job_queue.jobs_in_progress = 0;

    sem_init(&ipc->job_queue.mutex, 1, 1);
    sem_init(&ipc->job_queue.slots_libere, 1 , JOB_QUEUE_CAPACITY);
    sem_init(&ipc->job_queue.slots_ocupate, 1, 0);

    // result channels (1 pt fiecare worker)
    for(uint32_t i = 0; i < num_workers; i++){
        ipc->result_channels[i].head = 0;
        ipc->result_channels[i].tail = 0;
        ipc->result_channels[i].count = 0;
        ipc->result_channels[i].closed = false;

        sem_init(&ipc->result_channels[i].mutex, 1, 1);
        sem_init(&ipc->result_channels[i].slots_libere, 1 , RESULT_CHANNEL_CAPACITY);
        sem_init(&ipc->result_channels[i].slots_ocupate, 1 , 0);    
    }

    //worker stats
    for(uint32_t i = 0; i < num_workers; i++){
        ipc->worker_stats[i].worker_id = i;
        ipc->worker_stats[i].pid = 0;
        ipc->worker_stats[i].exit_status = 0;
        ipc->worker_stats[i].jobs_processed = 0;
        ipc->worker_stats[i].files_emitted = 0;
        ipc->worker_stats[i].bytes_sum = 0;
        ipc->worker_stats[i].time_ms = 0;
        ipc->worker_stats[i].user_cpu_t = 0;
        ipc->worker_stats[i].sys_cpu_t = 0;
        ipc->worker_stats[i].active = true;
    }
    printf("[Manager] Semafoare si structuri initializate\n");
    
    //adaugare job in job_queue
    printf("[Manager] Adaug jobul in coada: %s\n", root_dir);

    sem_wait(&ipc->job_queue.slots_libere); //astept un loc liber
    sem_wait(&ipc->job_queue.mutex);
    strncpy(ipc->job_queue.buffer[ipc->job_queue.tail].path, root_dir, MAX_PATH_LEN - 1); 
    ipc->job_queue.buffer[ipc->job_queue.tail].path[MAX_PATH_LEN - 1] = '\0';  
    ipc->job_queue.buffer[ipc->job_queue.tail].depth = 0; 
    ipc->job_queue.tail = (ipc->job_queue.tail + 1) % JOB_QUEUE_CAPACITY;
    ipc->job_queue.count++;
    sem_post(&ipc->job_queue.mutex); 
    sem_post(&ipc->job_queue.slots_ocupate); 

    //pregateste argumentele pt workeri
    char depth_str[16];
    snprintf(depth_str, sizeof(depth_str), "%d", max_depth == -1 ? INT_MAX : max_depth);
    
    // 3. Crearea si lansarea workerilor prin fork() + execl()
    for (int i = 0; i < num_workers; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("[Manager] Eroare fork");
            return 1;
        } else if (pid == 0) {
            char id_str[16];
            snprintf(id_str, sizeof(id_str), "%d", i);
            
            execl("./bin/fileops_worker", "fileops_worker", 
                  "--worker-id", id_str, 
                  "--ipc", ipc_path,
                  "--max-depth", depth_str,
                  NULL);
            
            perror("[Manager-Fiu] Eroare execl");
            exit(1);
        }
    }
    
    // ---> BUCLE HIBRIDA DE SUPRAVEGHERE SI CONSUM (Commit 3) <---
    int workeri_activi = num_workers;
    bool joburi_gata = false;
    uint64_t fisiere_gasite = 0;

    printf("[Manager] Incep monitorizarea canalelor IPC...\n");

    while (workeri_activi > 0) {
        bool idle = true; // Presupunem ca nu avem ce face tura asta

        // 1. Verificam daca e timpul sa trimitem semnalul de terminare a cautarii
        if (!joburi_gata) {
            sem_wait(&ipc->job_queue.mutex);
            if (ipc->job_queue.count == 0 && ipc->job_queue.jobs_in_progress == 0) {
                joburi_gata = true;
                printf("[Manager] Scanarea e gata. Se trimite semnal de terminare workerilor.\n");
                // Trezim toti workerii care asteapta joburi (ca sa poata da break si inchide)
                for(int j = 0; j < num_workers; j++) {
                    sem_post(&ipc->job_queue.slots_ocupate);
                }
            }
            sem_post(&ipc->job_queue.mutex);
        }

        // 2. Colectam non-blocking rezultatele trimise de workeri
        for (int i = 0; i < num_workers; i++) {
            if (sem_trywait(&ipc->result_channels[i].slots_ocupate) == 0) {
                idle = false;
                sem_wait(&ipc->result_channels[i].mutex);
                
                // Daca semaforul s-a activat doar pentru a ne spune ca s-a inchis canalul
                if (ipc->result_channels[i].count == 0 && ipc->result_channels[i].closed) {
                    sem_post(&ipc->result_channels[i].mutex);
                    workeri_activi--; // Am marcat acest worker ca "incheiat"
                    continue; 
                }

                // Extragem rezultatul
                file_record_t rec = ipc->result_channels[i].buffer[ipc->result_channels[i].head];
                ipc->result_channels[i].head = (ipc->result_channels[i].head + 1) % RESULT_CHANNEL_CAPACITY;
                ipc->result_channels[i].count--;
                
                sem_post(&ipc->result_channels[i].mutex);
                sem_post(&ipc->result_channels[i].slots_libere);

                // Deocamdata (Commit 3) doar printam ce a tras din conducta
                fisiere_gasite++;
                printf("[Manager] Extras: %s | Size: %lu (din Worker %d)\n", rec.path, rec.size, i);
            }
        }
        
        // Daca niciun canal nu avea date si joburile inca nu sunt gata, dormim putin
        if (idle) {
            usleep(1000); 
        }
    }

    printf("[Manager] Toate canalele au fost epuizate si inchise. Total fisiere receptionate: %lu\n", fisiere_gasite);

    // 4. Asteptarea incheierii (waitpid implicit global)
    for (int i = 0; i < num_workers; i++) {
        int status;
        pid_t child_pid = wait(&status);
        if (WIFEXITED(status)) {
            printf("[Manager] Worker (PID: %d) s-a incheiat normal cu exit code %d.\n", child_pid, WEXITSTATUS(status));
        } else {
            printf("[Manager] Worker (PID: %d) s-a incheiat anormal.\n", child_pid);
        }
    }

    //distrugere semafoare
    sem_destroy(&ipc->header_mutex);
    sem_destroy(&ipc->job_queue.mutex);
    sem_destroy(&ipc->job_queue.slots_libere);
    sem_destroy(&ipc->job_queue.slots_ocupate);

    for(uint32_t i = 0; i < num_workers; i++){
        sem_destroy(&ipc->result_channels[i].mutex);
        sem_destroy(&ipc->result_channels[i].slots_libere);
        sem_destroy(&ipc->result_channels[i].slots_ocupate);
    }

    // 5. Curatarea zonei mapate
    if (munmap(ipc, ipc_size) == -1) {
        perror("[Manager] Eroare munmap");
    }
    printf("[Manager] Toate procesele worker s-au terminat cu succes.\n");
    
    return 0;
}
