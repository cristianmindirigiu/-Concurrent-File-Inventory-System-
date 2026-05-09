#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include "../include/ipc_format.h"
#include <dirent.h>
#include <sys/stat.h>

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
            strcpy(rec.sha256, "TBA_SHA256"); 

            // Punem rezultatul in canalul propriu al worker-ului
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

    if (worker_id == -1 || !ipc_path) return 1;

    int fd = open(ipc_path, O_RDWR);
    if (fd == -1) return 1; 

    ipc_shared_t *ipc = mmap(NULL, sizeof(ipc_shared_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ipc == MAP_FAILED) { close(fd); return 1; }
    close(fd);

    job_t my_job;
    while(1){
        sem_wait(&ipc->job_queue.mutex);
        if(ipc->job_queue.count == 0 && ipc->job_queue.jobs_in_progress == 0){
            sem_post(&ipc->job_queue.mutex);
            break;
        }
        sem_post(&ipc->job_queue.mutex);

        sem_wait(&ipc->job_queue.slots_ocupate);

        sem_wait(&ipc->job_queue.mutex);
        if(ipc->job_queue.count == 0){
            sem_post(&ipc->job_queue.mutex);
            break;
        }

        my_job = ipc->job_queue.buffer[ipc->job_queue.head];
        ipc->job_queue.head = (ipc->job_queue.head + 1) % JOB_QUEUE_CAPACITY;
        ipc->job_queue.count--;
        ipc->job_queue.jobs_in_progress++;
        sem_post(&ipc->job_queue.mutex);
        sem_post(&ipc->job_queue.slots_libere);
        
        scaneaza_director(ipc, my_job, max_depth, worker_id);
        
        sem_wait(&ipc->job_queue.mutex);
        ipc->job_queue.jobs_in_progress--;
        sem_post(&ipc->job_queue.mutex);
    }
    
    sem_wait(&ipc->result_channels[worker_id].mutex);
    ipc->result_channels[worker_id].closed = true;
    sem_post(&ipc->result_channels[worker_id].mutex);
    // Trezim managerul o ultima data ca sa ii spunem ca am terminat si sa ne citeasca statusul 'closed'
    sem_post(&ipc->result_channels[worker_id].slots_ocupate);

    if (munmap(ipc, sizeof(ipc_shared_t)) == -1) perror("[Worker] Eroare munmap la iesire");
    return 0;
}