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
	DIR* dir = opendir(job.path); //deschidere directoriu
	if(dir == NULL){
		perror("Eroare deschidere director");
		return;
	}
	struct dirent* entry;
	while((entry = readdir(dir)) != NULL){
		//entry->d_name - numele intrarii
		if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0){
			continue;
		}
		//construire cale completa
		char full_path[MAX_PATH_LEN * 2];
		snprintf(full_path, sizeof(full_path), "%s/%s", job.path, entry->d_name);
		
		//aflarea tipului cu lstat
		struct stat st;
		if(lstat(full_path, &st) == -1) continue; //procesul poate fi disparut
		
		if(S_ISLNK(st.st_mode)) continue; //e symlink
		if(S_ISDIR(st.st_mode)){
			if(job.depth < max_depth){
				job_t nou_job;
				strncpy(nou_job.path, full_path, MAX_PATH_LEN-1);
				nou_job.path[MAX_PATH_LEN - 1] = '\0';
                        	nou_job.depth = job.depth + 1;

				//adaugare in coada
				sem_wait(&ipc->job_queue.slots_libere);
				sem_wait(&ipc->job_queue.mutex);
				ipc->job_queue.buffer[ipc->job_queue.tail] = nou_job;
				ipc->job_queue.tail = (ipc->job_queue.tail + 1) % JOB_QUEUE_CAPACITY;
				ipc->job_queue.count ++;
				sem_post(&ipc->job_queue.mutex);
				sem_post(&ipc->job_queue.slots_ocupate);
			}
		}
		if(S_ISREG(st.st_mode)) printf("[Worker %d] Fisier regular: %s\n", worker_id, full_path);
	}
	closedir(dir);
}


int main(int argc, char *argv[]) {
    int worker_id = -1;
    char *ipc_path = NULL;
    uint32_t max_depth = UINT32_MAX; //e practic infinit si nu va opri recursivitatea
    // 1. Parsarea argumentelor venite din exec()
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

    printf("[Worker %d] Lansat in executie. Atasare la canalul %s...\n", worker_id, ipc_path);

    // 2. Maparea la aceeasi zona IPC partajata
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

    // 3. Verificarea legaturii
    if (strncmp(ipc->magic, "IPC4", 4) == 0) {
        printf("[Worker %d] Conectat. Magic number IPC4 confirmat.\n", worker_id);
    } else {
        printf("[Worker %d] Eroare la citirea structurii comune!\n", worker_id);
    }
    
    job_t my_job;
    while(1){
	// verificare conditie de terminare (nu mai este sursa de joburi noi)
	sem_wait(&ipc->job_queue.mutex);
	if(ipc->job_queue.count == 0 && ipc->job_queue.jobs_in_progress == 0){
		sem_post(&ipc->job_queue.mutex);
		break;
	}
	sem_post(&ipc->job_queue.mutex);

	sem_wait(&ipc->job_queue.slots_ocupate);

	// verificare pt semnal fals de la manager (dupa sem_post)
	// previne workerul din a citi din coada goala
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
	
	//marchez ca am terminat acest job
	sem_wait(&ipc->job_queue.mutex);
	ipc->job_queue.jobs_in_progress--;
	sem_post(&ipc->job_queue.mutex);

    }
    
    


    // 4. Eliberarea resurselor locale inainte de exit
    if (munmap(ipc, sizeof(ipc_shared_t)) == -1) {
        perror("[Worker] Eroare munmap la iesire");
    }
    
    printf("[Worker %d] Ciclu de viata incheiat. Gata pentru exit.\n", worker_id);
    return 0;
}
