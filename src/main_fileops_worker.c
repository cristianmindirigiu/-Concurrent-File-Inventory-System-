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

void compute_sha256(const char *path, char *output_buffer) {
    char cmd[MAX_PATH_LEN + 32];
    snprintf(cmd, sizeof(cmd), "sha256sum \"%s\" 2>/dev/null", path);
    FILE *fp = popen(cmd, "r");
    if (fp) {
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

void scaneaza_director(ipc_shared_t* ipc, job_t job, uint32_t max_depth, int worker_id){
    DIR* dir = opendir(job.path); 
    if(dir == NULL){
        perror("Eroare deschidere director");
        return;
    }
    struct dirent* entry;
    while((entry = readdir(dir)) != NULL){
        if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0){
            continue;
        }
        
        char full_path[MAX_PATH_LEN * 2];
        snprintf(full_path, sizeof(full_path), "%s/%s", job.path, entry->d_name);
        
        struct stat st;
        if(lstat(full_path, &st) == -1) continue; 
        
        if(S_ISLNK(st.st_mode)) continue; 
        if(S_ISDIR(st.st_mode)){
            if(job.depth < max_depth){
                job_t nou_job;
                strncpy(nou_job.path, full_path, MAX_PATH_LEN-1);
                nou_job.path[MAX_PATH_LEN - 1] = '\0';
                nou_job.depth = job.depth + 1;

                sem_wait(&ipc->job_queue.slots_libere);
                sem_wait(&ipc->job_queue.mutex);
                ipc->job_queue.buffer[ipc->job_queue.tail] = nou_job;
                ipc->job_queue.tail = (ipc->job_queue.tail + 1) % JOB_QUEUE_CAPACITY;
                ipc->job_queue.count ++;
                sem_post(&ipc->job_queue.mutex);
                sem_post(&ipc->job_queue.slots_ocupate);
            }
        }
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

            // ACTUALIZARE STATISTICI IN TIMP REAL
            ipc->worker_stats[worker_id].files_emitted++;
            ipc->worker_stats[worker_id].bytes_sum += st.st_size;

            sem_wait(&ipc->result_channels[worker_id].slots_libere);
            sem_wait(&ipc->result_channels[worker_id].mutex);

            ipc->result_channels[worker_id].buffer[ipc->result_channels[worker_id].tail] = rec;
            ipc->result_channels[worker_id].tail = (ipc->result_channels[worker_id].tail + 1) % RESULT_CHANNEL_CAPACITY;
            ipc->result_channels[worker_id].count++;

            sem_post(&ipc->result_channels[worker_id].mutex);
            sem_post(&ipc->result_channels[worker_id].slots_ocupate);
        }
    }
    closedir(dir);
}

int main(int argc, char *argv[]) {
    // START CRONOMETRU
    struct timeval start_time, end_time;
    gettimeofday(&start_time, NULL);

    int worker_id = -1;
    char *ipc_path = NULL;
    uint32_t max_depth = UINT32_MAX; 
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--worker-id") == 0 && i + 1 < argc) worker_id = atoi(argv[++i]);
        else if (strcmp(argv[i], "--ipc") == 0 && i + 1 < argc) ipc_path = argv[++i];
        else if (strcmp(argv[i], "--max-depth") == 0 && i + 1 < argc){
            max_depth = (uint32_t)atoi(argv[++i]);
        }
    }

    if (worker_id == -1 || !ipc_path) {
        fprintf(stderr, "[Worker] Eroare: Argumente de lansare lipsa.\n");
        return 1;
    }

    int fd = open(ipc_path, O_RDWR);
    if (fd == -1) { 
        perror("[Worker] Eroare deschidere IPC"); 
        return 1; 
    }

    ipc_shared_t *ipc = mmap(NULL, sizeof(ipc_shared_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ipc == MAP_FAILED) { 
        perror("[Worker] Eroare mmap pe memorie partajata"); 
        close(fd); return 1; 
    }
    close(fd);

    // SETARE PID IN STRUCTURA STATISTICA
    ipc->worker_stats[worker_id].pid = getpid();
    printf("[Worker %d] Lansat in executie cu PID %d. Atasare la canal...\n", worker_id, getpid());

    if (strncmp(ipc->magic, "IPC4", 4) == 0) {
        printf("[Worker %d] Conectat. Magic number IPC4 confirmat.\n", worker_id);
    }
    
    job_t my_job;
    while(1){
        sem_wait(&ipc->job_queue.mutex);
        
        // --- MODIFICARE T4: Dacă Managerul a cerut oprirea ---
        if(ipc->terminate_flag){ 
            sem_post(&ipc->job_queue.mutex);
            break;
        }
        sem_post(&ipc->job_queue.mutex);

        sem_wait(&ipc->job_queue.slots_ocupate);

        sem_wait(&ipc->job_queue.mutex);
        
        // --- MODIFICARE T4: Se verifică terminate_flag inclusiv după trezire ---
        if(ipc->terminate_flag || ipc->job_queue.count == 0){ 
            sem_post(&ipc->job_queue.mutex);
            break;
        }

        my_job = ipc->job_queue.buffer[ipc->job_queue.head];
        ipc->job_queue.head = (ipc->job_queue.head + 1) % JOB_QUEUE_CAPACITY;
        ipc->job_queue.count--;
        ipc->job_queue.jobs_in_progress++;
        sem_post(&ipc->job_queue.mutex);
        sem_post(&ipc->job_queue.slots_libere);
        
        // MARCHEAZA JOB PRELUAT
        ipc->worker_stats[worker_id].jobs_processed++;
        
        scaneaza_director(ipc, my_job, max_depth, worker_id);
        
        sem_wait(&ipc->job_queue.mutex);
        ipc->job_queue.jobs_in_progress--;
        sem_post(&ipc->job_queue.mutex);
    }
    
    // FINALIZARE STATISTICI: TIMPI CPU SI WALL TIME
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    ipc->worker_stats[worker_id].user_cpu_t = usage.ru_utime.tv_sec * 1000000 + usage.ru_utime.tv_usec;
    ipc->worker_stats[worker_id].sys_cpu_t = usage.ru_stime.tv_sec * 1000000 + usage.ru_stime.tv_usec;

    gettimeofday(&end_time, NULL);
    ipc->worker_stats[worker_id].time_ms = (end_time.tv_sec - start_time.tv_sec) * 1000 + (end_time.tv_usec - start_time.tv_usec) / 1000;

    sem_wait(&ipc->result_channels[worker_id].mutex);
    ipc->result_channels[worker_id].closed = true;
    sem_post(&ipc->result_channels[worker_id].mutex);
    sem_post(&ipc->result_channels[worker_id].slots_ocupate);

    if (munmap(ipc, sizeof(ipc_shared_t)) == -1) {
        perror("[Worker] Eroare munmap la iesire");
    }
    
    printf("[Worker %d] Ciclu de viata incheiat. Gata pentru exit.\n", worker_id);
    return 0;
}
