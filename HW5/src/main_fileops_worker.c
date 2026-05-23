#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include "../include/ipc_format.h"
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <errno.h>

#include <signal.h>

// flag volatile pentru SIGTERM
static volatile sig_atomic_t got_sigterm = 0;

static void handler_sigterm(int sig) {
    (void)sig;
    got_sigterm = 1;
}

// Functia asta cheama comanda de sistem sha256sum ca sa ne calculeze hash-ul fisierului
// Folosim popen ca sa capturam output-ul comenzii direct intr-un buffer, fara sa mai scriem prin alte fisiere temporare.
void compute_sha256(const char *path, char *output_buffer) {
    char cmd[MAX_PATH_LEN + 32];
    snprintf(cmd, sizeof(cmd), "sha256sum \"%s\" 2>/dev/null", path);
    FILE *fp = popen(cmd, "r");
    if (fp) {
        // Citim fix 64 de caractere (plus terminatorul). Daca pica ceva, punem N/A
        if (fgets(output_buffer, 65, fp) == NULL) {
            strcpy(output_buffer, "N/A");
        } else {
            output_buffer[64] = '\0'; 
        }
        pclose(fp);
    } else {
        strcpy(output_buffer, "ERROR");
    }
}

// Functia principala in care worker-ul isi face treaba propriu-zisa: scaneaza un director dat ca job
void scaneaza_director(ipc_shared_t* ipc, job_t job, uint32_t max_depth, int worker_id, int control_fd){
    uint64_t fisiere_in_job = 0;
    uint64_t bytes_in_job = 0;
    DIR* dir = opendir(job.path); 
    if(dir == NULL){
        char buf[512];
	int len = snprintf(buf, sizeof(buf), "T5MSG type=ERROR worker_id=%d errno=%d where=opendir\n", worker_id , errno);
	write(control_fd, buf, len);
        return;
    }
    
    struct dirent* entry;
    // Citim element cu element din director
    while((entry = readdir(dir)) != NULL){
        // verificam daca am primit SIGTERM in timp ce scanam
        if (got_sigterm) break;
        // Ignoram folderele parinte si curente ca sa nu facem bucle infinite
        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0){
            continue;
        }
        
        // Construim calea completa pentru fisierul/directorul gasit
        char full_path[MAX_PATH_LEN * 2];
        snprintf(full_path, sizeof(full_path), "%s/%s", job.path, entry->d_name);
        
        // Folosim lstat ca sa luam informatii despre fisier (dimensiune, drepturi, tip etc.)
        struct stat st;
        if(lstat(full_path, &st) == -1) continue; 
        
        // Daca gasim un symlink, ii dam skip direct
        if(S_ISLNK(st.st_mode)) continue; 
        
        // Daca e director si nu am depasit limita de adancime (max_depth), cream un job nou
        if(S_ISDIR(st.st_mode)){
            if(job.depth < max_depth){
                job_t nou_job;
                strncpy(nou_job.path, full_path, MAX_PATH_LEN-1);
                nou_job.path[MAX_PATH_LEN - 1] = '\0';
                nou_job.depth = job.depth + 1;

                // Aici e partea de sincronizare pentru adaugarea in coada comuna de job-uri.
                // sem_wait pe slots_libere blocheaza procesul daca coada e plina, ca sa nu suprascriem job-uri
                sem_wait(&ipc->job_queue.slots_libere);
                sem_wait(&ipc->job_queue.mutex); // blocam ca sa nu bage alt worker in acelasi timp
                
                ipc->job_queue.buffer[ipc->job_queue.tail] = nou_job;
                ipc->job_queue.tail = (ipc->job_queue.tail + 1) % JOB_QUEUE_CAPACITY;
                ipc->job_queue.count ++;
                
                sem_post(&ipc->job_queue.mutex); // eliberam accesul exclusiv
                sem_post(&ipc->job_queue.slots_ocupate); // semnalizam ca am mai bagat un job
            }
        }
        
        // Daca e un fisier normal (regulat), extragem ce ne trebuie si il trimitem la manager
        if(S_ISREG(st.st_mode)) {
            file_record_t rec;
            memset(&rec, 0, sizeof(rec));
            strncpy(rec.path, full_path, MAX_PATH_LEN - 1);
            rec.size = st.st_size;
            rec.mtime = st.st_mtime;
            rec.mode = st.st_mode;
            rec.uid = st.st_uid;
            rec.gid = st.st_gid;
            
            compute_sha256(full_path, rec.sha256);

            // Actualizam statisticile noastre interne direct in memoria partajata
            ipc->worker_stats[worker_id].files_emitted++;
            ipc->worker_stats[worker_id].bytes_sum += st.st_size;
	    fisiere_in_job++;
	    bytes_in_job += st.st_size;

            // Daca canalul asta e plin, worker-ul asteapta  aici
            // Nu merge mai departe pana nu consuma managerul din rezultate
            sem_wait(&ipc->result_channels[worker_id].slots_libere);
            sem_wait(&ipc->result_channels[worker_id].mutex); // protejam scrierea in canal

            ipc->result_channels[worker_id].buffer[ipc->result_channels[worker_id].tail] = rec;
            ipc->result_channels[worker_id].tail = (ipc->result_channels[worker_id].tail + 1) % RESULT_CHANNEL_CAPACITY;
            ipc->result_channels[worker_id].count++;

            sem_post(&ipc->result_channels[worker_id].mutex); // dam drumul la acces
            sem_post(&ipc->result_channels[worker_id].slots_ocupate); // spunem managerului a are ceva nou de citit
        }
    }
    closedir(dir);

    //scriere mesaj
    char buf[512];
    int len = snprintf(buf, sizeof(buf), "T5MSG type=JOB_DONE worker_id=%d jobs=%lu files=%lu bytes=%lu\n", worker_id, ipc->worker_stats[worker_id].jobs_processed, fisiere_in_job, bytes_in_job);
    write(control_fd, buf, len);
}

int main(int argc, char *argv[]) {
    // Pornim cronometrul ca sa vedem cat traieste worker-ul asta per total
    struct timeval start_time, end_time;
    gettimeofday(&start_time, NULL);

    int worker_id = -1;
    char *ipc_path = NULL;
    uint32_t max_depth = UINT32_MAX; 
    int control_fd = -1; 
    // Citim si parsam ce parametri ne-a pasat managerul la fork + exec
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--worker-id") == 0 && i + 1 < argc) worker_id = atoi(argv[++i]);
        else if (strcmp(argv[i], "--ipc") == 0 && i + 1 < argc) ipc_path = argv[++i];
        else if (strcmp(argv[i], "--max-depth") == 0 && i + 1 < argc){
            max_depth = (uint32_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--control-fd") == 0 && i + 1 < argc) control_fd = atoi(argv[++i]);
    }

    if (worker_id == -1 || !ipc_path) {
        fprintf(stderr, "[Worker] Eroare: Argumente de lansare lipsa.\n");
        return 1;
    }

    // instalare handler SIGTERM
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler_sigterm;
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);

    // Deschidem fisierul comun de memorie (IPC) 
    int fd = open(ipc_path, O_RDWR);
    if (fd == -1) { 
        perror("[Worker] Eroare deschidere IPC"); 
        return 1; 
    }

    // Aici facem mmap ca sa vedem in procesul nostru memoria partajata cu managerul
    ipc_shared_t *ipc = mmap(NULL, sizeof(ipc_shared_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ipc == MAP_FAILED) { 
        perror("[Worker] Eroare mmap pe memorie partajata"); 
        close(fd); return 1; 
    }
    close(fd);

    // Ne punem PID-ul curent in statistici ca sa stie managerul la sfarsit de cine da wait()
    ipc->worker_stats[worker_id].pid = getpid();
    printf("[Worker %d] Lansat in executie cu PID %d. Atasare la canal...\n", worker_id, getpid());

    if (strncmp(ipc->magic, "IPC4", 4) == 0) {
        printf("[Worker %d] Conectat. Magic number IPC4 confirmat.\n", worker_id);
    }
    
    // Bucla principala de viata a worker-ului (consumator de job-uri)
    job_t my_job;
    while(1){
        // verificam SIGTERM
        if (got_sigterm) break;

        sem_wait(&ipc->job_queue.mutex);
        
        // Daca managerul a ridicat steagul de terminare, iesim direct afara
        if(ipc->terminate_flag){ 
            sem_post(&ipc->job_queue.mutex);
            break;
        }
        sem_post(&ipc->job_queue.mutex);

        // Asteptam pana apare ceva in coada de job-uri
        sem_wait(&ipc->job_queue.slots_ocupate);

        sem_wait(&ipc->job_queue.mutex); // Ne asiguram ca doar noi citim/modificam capul cozii acum
        
        // Extra verificare: poate ne-a trezit managerul doar ca sa ne spuna ca e gata
        if(ipc->terminate_flag || ipc->job_queue.count == 0){ 
            sem_post(&ipc->job_queue.mutex);
            break;
        }

        // Extragem job-ul folosind logica de buffer circular (modulo)
        my_job = ipc->job_queue.buffer[ipc->job_queue.head];
        ipc->job_queue.head = (ipc->job_queue.head + 1) % JOB_QUEUE_CAPACITY;
        ipc->job_queue.count--;
        ipc->job_queue.jobs_in_progress++; // Spunem tuturor "sunt ocupat cu un folder"
        
        sem_post(&ipc->job_queue.mutex);
        sem_post(&ipc->job_queue.slots_libere); // Eliberam un slot pentru cine vrea sa puna job-uri noi
        
        // Contorizam job-ul
        ipc->worker_stats[worker_id].jobs_processed++;
        
        // Aici facem treaba fizic
        scaneaza_director(ipc, my_job, max_depth, worker_id, control_fd);
        
        // Marcam ca am terminat cu folderul asta
        sem_wait(&ipc->job_queue.mutex);
        ipc->job_queue.jobs_in_progress--;
        sem_post(&ipc->job_queue.mutex);

        // inca un check la final de tura
        if (got_sigterm) break;
    }
    
    // Ne calculam consumul de CPU si timpul total
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    ipc->worker_stats[worker_id].user_cpu_t = usage.ru_utime.tv_sec * 1000000 + usage.ru_utime.tv_usec;
    ipc->worker_stats[worker_id].sys_cpu_t = usage.ru_stime.tv_sec * 1000000 + usage.ru_stime.tv_usec;

    gettimeofday(&end_time, NULL);
    ipc->worker_stats[worker_id].time_ms = (end_time.tv_sec - start_time.tv_sec) * 1000 + (end_time.tv_usec - start_time.tv_usec) / 1000;

    // Instiintam managerul ca inchidem si nu ii mai trimitem fisiere pe canalul nostru
    sem_wait(&ipc->result_channels[worker_id].mutex);
    ipc->result_channels[worker_id].closed = true;
    sem_post(&ipc->result_channels[worker_id].mutex);
    sem_post(&ipc->result_channels[worker_id].slots_ocupate); // Il trezim o ultima data in caz ca dormea
	
    //trimitere WORKER_EXITING pe pipe
    const char *reason = got_sigterm ? "shutdown" : "normal";
    char buf[512];
    int len = snprintf(buf, sizeof(buf), "T5MSG type=WORKER_EXITING worker_id=%d reason=%s\n", worker_id, reason);
    write(control_fd, buf, len);

    //inchidem pipeul de control
    if(control_fd >= 0) close(control_fd);
    // Decuplam memoria partajata din spatiul procesului nostru
    if (munmap(ipc, sizeof(ipc_shared_t)) == -1) {
        perror("[Worker] Eroare munmap la iesire");
    }
    
    printf("[Worker %d] Ciclu de viata incheiat. Gata pentru exit.\n", worker_id);
    return 0;
}

