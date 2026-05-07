#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "../include/ipc_format.h"

int main(int argc, char *argv[]) {
    char *root_dir = NULL;
    int num_workers = 0;
    char *ipc_path = "data/ipc.mmap";
    char *db_path = "data/inventory.db";
    
    // 1. Parsarea argumentelor CLI
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--root") == 0 && i + 1 < argc) root_dir = argv[++i];
        else if (strcmp(argv[i], "--workers") == 0 && i + 1 < argc) num_workers = atoi(argv[++i]);
        else if (strcmp(argv[i], "--ipc") == 0 && i + 1 < argc) ipc_path = argv[++i];
        else if (strcmp(argv[i], "--db") == 0 && i + 1 < argc) db_path = argv[++i];
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

    // 3. Crearea si lansarea workerilor prin fork() + execl()
    for (int i = 0; i < num_workers; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("[Manager] Eroare fork");
            return 1;
        } else if (pid == 0) {
            char id_str[16];
            snprintf(id_str, sizeof(id_str), "%d", i);
            
            // Copilul apeleaza exec pentru a deveni worker
            execl("./bin/fileops_worker", "fileops_worker", 
                  "--worker-id", id_str, 
                  "--ipc", ipc_path, 
                  NULL);
            
            // Executa doar daca execl a esuat
            perror("[Manager-Fiu] Eroare execl");
            exit(1);
        }
    }

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

    // 5. Curatarea zonei mapate
    if (munmap(ipc, ipc_size) == -1) {
        perror("[Manager] Eroare munmap");
    }
    printf("[Manager] Toate procesele worker s-au terminat cu succes.\n");
    
    return 0;
}
