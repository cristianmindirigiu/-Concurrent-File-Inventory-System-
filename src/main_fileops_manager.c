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
    bool do_verify = false;
    bool do_dump = false;
    
    // 1. Analiza argumentelor CLI
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--root") == 0 && i + 1 < argc) root_dir = argv[++i];
        else if (strcmp(argv[i], "--workers") == 0 && i + 1 < argc) num_workers = atoi(argv[++i]);
        else if (strcmp(argv[i], "--ipc") == 0 && i + 1 < argc) ipc_path = argv[++i];
        else if (strcmp(argv[i], "--db") == 0 && i + 1 < argc) db_path = argv[++i];
        else if (strcmp(argv[i], "--max-depth") == 0 && i+1 < argc){
            max_depth = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--verify") == 0) do_verify = true;
        else if (strcmp(argv[i], "--dump") == 0) do_dump = true;
    }
    
    //VERIFY - verific magic, versiunea, complete si dimensiunea fisierului
    if(do_verify){
        FILE* f = fopen(db_path, "rb");
        if(f == NULL){
            fprintf(stderr, "[Verify] Eroare, nu se poate deschide %s\n", db_path);
            return 1;
        }

        inv4_header_t h;
        if(fread(&h, sizeof(inv4_header_t), 1, f) != 1){
            fprintf(stderr, "[Verify] Eroare, fisier prea mic\n");
            fclose(f);
            return 1;
        }
	//verif magic
        if(memcmp(h.magic, INV4_MAGIC, 4)!=0){
            fprintf(stderr, "[Verify] Eroare, magic invalid (asteptat INV4)\n");
            fclose(f);
            return 1;
        }
	//verif format version
        if(h.format_version != INV4_VERSION){
            fprintf(stderr, "[Verify] Eroare, versiune necunoscuta (%u)\n", h.format_version);
            fclose(f);
            return 1;
        }
	//verif complete
        if(h.complete != DB_PART_COMPLETE && h.complete != DB_FULL_COMPLETE){
            fprintf(stderr, "[Verify] Eroare, DB incomplet (complete=%u)\n", h.complete);
            fclose(f);
            return 1;
        }
	//verif dimensiune fisier
        fseek(f, 0, SEEK_END);
        long file_size = ftell(f);
        long expected_size = sizeof(inv4_header_t) + h.file_record_count * sizeof(file_record_t) + h.worker_count * sizeof(inv4_worker_stat_t);

        if(file_size != expected_size){
            fprintf(stderr, "[Verify] Eroare, dimensiunea incorecta (actual = %ld, asteptat = %ld)\n", file_size, expected_size);
            fclose(f);
            return 1;
        }

        fclose(f);
        printf("[Verify] OK, %s este valid (%lu records , %u workeri)\n", db_path, h.file_record_count, h.worker_count);
        return 0;
    }

    //DUMP - citesc headerul si afisez campurile intr-un format fix
    if(do_dump){
        FILE* f = fopen(db_path, "rb");
        if(f == NULL){
            fprintf(stderr, "[Dump] Eroare, nu se poate deschide %s\n", db_path);
            return 1;
        }

        inv4_header_t h;
        if(fread(&h, sizeof(inv4_header_t), 1, f) != 1){
            fprintf(stderr, "[Dump] Eroare: fisier prea mic\n");
            fclose(f);
            return 1;
        }

        printf("---HEADER---\n");
        printf("magic=%.4s\n", h.magic);
        printf("version=%u\n", h.format_version);
        printf("complete=%u\n", h.complete);
        printf("file_record_count=%lu\n", h.file_record_count);
        printf("worker_count=%u\n", h.worker_count);

        printf("\n--- FILE RECORDS ---\n");
        for(uint64_t i = 0; i < h.file_record_count; i++){
            file_record_t rec;
            if(fread(&rec, sizeof(file_record_t), 1, f) == 1){
                printf("[%lu] %s | Size: %lu bytes | SHA256: %s\n", i, rec.path, rec.size, rec.sha256);
            }
        }

        printf("\n--- WORKER STATS ---\n");
        for(uint32_t i = 0; i < h.worker_count; i++){
            inv4_worker_stat_t ws;
            if(fread(&ws, sizeof(inv4_worker_stat_t), 1, f) == 1){
                printf("Worker %u (PID: %d) | Exit Status: %d | Jobs: %lu | Files Emitted: %lu | Bytes: %lu | Wall Time: %lu ms | CPU User: %lu us | CPU Sys: %lu us\n",
                       ws.worker_id, ws.pid, ws.exit_status, ws.jobs_processed, ws.files_emitted, ws.bytes_sum, ws.time_ms, ws.user_cpu_t, ws.sys_cpu_t);
            }
        }

        fclose(f);
        return 0;
    }
    
    if (!root_dir || num_workers < 1) {
        fprintf(stderr, "[Manager] Utilizare: %s --root <dir> --workers <N>\n", argv[0]);
        return 1;
    }

    printf("[Manager] Incepem inventarierea pe '%s' cu %d workeri.\n", root_dir, num_workers);

    // asiguram crearea folderelor
    system("mkdir -p data reports");


    //2 . initializarea memoriei partajate IPC
    int fd = open(ipc_path, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fd == -1) { perror("[Manager] Eroare creare fisier IPC"); return 1; }
    
    size_t ipc_size = sizeof(ipc_shared_t);
    if (ftruncate(fd, ipc_size) == -1) { perror("[Manager] Eroare ftruncate IPC"); close(fd); return 1; }
    
    ipc_shared_t *ipc = mmap(NULL, ipc_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (ipc == MAP_FAILED) { perror("[Manager] Eroare mmap"); close(fd); return 1; }
    close(fd); //dupa mapare , nu mai avem nevoie de descriptor deschis
    //initializam zonele de baza
    memcpy(ipc->magic, "IPC4", 4);
    ipc->num_workers = num_workers;
    ipc->terminate_flag = false;
    ipc->version = IPC_VERSION;
    
    //initializare semafoare
    sem_init(&ipc->header_mutex, 1, 1);
    //job queue
    ipc->job_queue.head = 0;
    ipc->job_queue.tail = 0;
    ipc->job_queue.count = 0;
    ipc->job_queue.jobs_in_progress = 0;

    sem_init(&ipc->job_queue.mutex, 1, 1);
    sem_init(&ipc->job_queue.slots_libere, 1 , JOB_QUEUE_CAPACITY);
    sem_init(&ipc->job_queue.slots_ocupate, 1, 0);
    
    // result channels (1 pentru fiecare worker)
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

    //adaugare job in queue
    printf("[Manager] Adaug jobul in coada: %s\n", root_dir);
    sem_wait(&ipc->job_queue.slots_libere);//astept un loc liber
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
    
    //3. crearea si lansarea workerilor prin fork() si exec()
    for (int i = 0; i < num_workers; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("[Manager] Eroare fork");
            return 1;
        } else if (pid == 0) {
            char id_str[16];
            snprintf(id_str, sizeof(id_str), "%d", i);
            execl("./bin/fileops_worker", "fileops_worker", "--worker-id", id_str, "--ipc", ipc_path, "--max-depth", depth_str, NULL);
            perror("[Manager-Fiu] Eroare execl");
            exit(1);
        }
    }
    
    int workeri_activi = num_workers;
    bool joburi_gata = false;
    uint64_t fisiere_gasite = 0;
    
    uint64_t max_records = 100000;
    file_record_t* all_records = malloc(max_records * sizeof(file_record_t));
    if(all_records == NULL){
        perror("[Manager] Eroare alocare memorie pt records");
        return 1;
    }
    uint64_t total_records = 0;

    printf("[Manager] Incep monitorizarea canalelor IPC...\n");

    while (workeri_activi > 0) {
        bool idle = true; //pp ca nu avem ce face tura asta
	
	//verificam daca e timpul sa trimitem semnalul de terminare a cautarii
        if (!joburi_gata) {
            sem_wait(&ipc->job_queue.mutex);
            if (ipc->job_queue.count == 0 && ipc->job_queue.jobs_in_progress == 0) {
                joburi_gata = true;
                
                // semnalizare corecta foldere
                ipc->terminate_flag = true; 

                printf("[Manager] Scanarea e gata. Se trimite semnal de terminare workerilor.\n");
		//trezim toti workerii care asteapta joburi (ca sa poata da break)
                for(int j = 0; j < num_workers; j++) {
                    sem_post(&ipc->job_queue.slots_ocupate);
                }
            }
            sem_post(&ipc->job_queue.mutex);
        }
	//colectare rezultatele trimise de workeri
        for (int i = 0; i < num_workers; i++) {
            if (sem_trywait(&ipc->result_channels[i].slots_ocupate) == 0) {
                idle = false;
                sem_wait(&ipc->result_channels[i].mutex);
                // daca semaforul s-a activat doar pentru a spune ca s-a inchis canalul
                if (ipc->result_channels[i].count == 0 && ipc->result_channels[i].closed) {
                    sem_post(&ipc->result_channels[i].mutex);
                    workeri_activi--; //am marcat acest worker ca incheiat
                    continue; 
                }
		
		//extragere rez
                file_record_t rec = ipc->result_channels[i].buffer[ipc->result_channels[i].head];
                ipc->result_channels[i].head = (ipc->result_channels[i].head + 1) % RESULT_CHANNEL_CAPACITY;
                ipc->result_channels[i].count--;
                
                sem_post(&ipc->result_channels[i].mutex);
                sem_post(&ipc->result_channels[i].slots_libere);

                fisiere_gasite++;
		//salveaza recordul in array
                if (total_records < max_records){
                    all_records[total_records++] = rec;
                }
            }
        }
        
        if (idle) usleep(1000); 
    }

    printf("[Manager] Toate canalele au fost epuizate si inchise. Total fisiere receptionate: %lu\n", fisiere_gasite);

    printf("[Manager] Colectam statusurile finale ale proceselor...\n");
    for (int i = 0; i < num_workers; i++) {
        int status;
        pid_t child_pid = wait(&status);
        
        for(int j = 0; j < num_workers; j++) {
            if (ipc->worker_stats[j].pid == child_pid) {
                if (WIFEXITED(status)) {
                    ipc->worker_stats[j].exit_status = WEXITSTATUS(status);
                    printf("[Manager] Worker (PID: %d, ID: %d) s-a incheiat normal cu exit code %d.\n", child_pid, j, WEXITSTATUS(status));
                } else {
                    ipc->worker_stats[j].exit_status = -1;
                    printf("[Manager] Worker (PID: %d, ID: %d) s-a incheiat anormal.\n", child_pid, j);
                }
                ipc->worker_stats[j].active = false;
                break;
            }
        }
    }
    
    //scrierea db-ului
    //scriu intr-un fisier temp apoi fac rename
    //
    //construirea caii fisierului temp
    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", db_path);

    //deschiderea fisierului tmp pentru scriere binara
    FILE* db_file = fopen(tmp_path, "wb");
    if(db_file == NULL){
        perror("[Manager] Eroare la deschiderea fisierului temporar");
        free(all_records);
        return 1;
    }
    
    // decidem valoarea complete
    uint8_t db_complete;
    if (shutdown_initiat) {
        db_complete = DB_PART_COMPLETE; // 1 = inventariere intrerupta dar DB valid structural
    } else {
        db_complete = DB_FULL_COMPLETE; // 2 = inventariere terminata normal
    }

    //construirea si scrierea header-ului
    inv4_header_t header;
    memcpy(header.magic, INV4_MAGIC, 4);
    header.format_version = INV4_VERSION;
    header.complete = DB_START; // scriem initial 0 in fisierul temporar
    header.file_record_count = total_records;
    header.worker_count = num_workers;

    fwrite(&header, sizeof(inv4_header_t), 1, db_file);
    //scriu toate records
    fwrite(all_records, sizeof(file_record_t), total_records, db_file);
	
    //construiesc si scriu worker stats
    for(int i = 0; i < num_workers; i++){
        inv4_worker_stat_t ws;
        ws.worker_id = ipc->worker_stats[i].worker_id;
        ws.pid = ipc->worker_stats[i].pid;
        ws.exit_status = ipc->worker_stats[i].exit_status;
        ws.jobs_processed = ipc->worker_stats[i].jobs_processed;
        ws.files_emitted = ipc->worker_stats[i].files_emitted;
        ws.bytes_sum = ipc->worker_stats[i].bytes_sum;
        ws.time_ms = ipc->worker_stats[i].time_ms;
        ws.user_cpu_t = ipc->worker_stats[i].user_cpu_t;
        ws.sys_cpu_t = ipc->worker_stats[i].sys_cpu_t;

        fwrite(&ws , sizeof(inv4_worker_stat_t), 1, db_file);
    }
    
    //setez statusul final corect si resetez headerul
    header.complete = db_complete;
    rewind(db_file);
    fwrite(&header, sizeof(inv4_header_t), 1, db_file);
	
    //inchid fisierul si fac rename
    fclose(db_file);
    if(rename(tmp_path, db_path) == -1){
        perror("[Manager] Eroare rename DB");
        free(all_records);
        return 1;
    }
    printf("[Manager] DB scris cu succes: %s (%lu records, complete=%u)\n", db_path, total_records, db_complete);

    sem_destroy(&ipc->header_mutex);
    sem_destroy(&ipc->job_queue.mutex);
    sem_destroy(&ipc->job_queue.slots_libere);
    sem_destroy(&ipc->job_queue.slots_ocupate);

    for(uint32_t i = 0; i < num_workers; i++){
        sem_destroy(&ipc->result_channels[i].mutex);
        sem_destroy(&ipc->result_channels[i].slots_libere);
        sem_destroy(&ipc->result_channels[i].slots_ocupate);
    }

    if (munmap(ipc, ipc_size) == -1) {
        perror("[Manager] Eroare munmap");
    }
    free(all_records);

    printf("[Manager] Toate procesele worker s-au terminat cu succes.\n");
    return 0;
}
