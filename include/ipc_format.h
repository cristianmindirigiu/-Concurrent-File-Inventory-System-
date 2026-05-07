#ifndef IPC_FORMAT_H
#define IPC_FORMAT_H

#include <stdint.h>
#include <stdbool.h>

/* * Aceasta este structura memoriei partajate pentru Commit 1.
 * În Commit-ul 2 o vom extinde masiv prin adăugarea Job Queue-ului, 
 * Canalelor de Rezultate și a Semafoarelor de sincronizare.
 */
typedef struct {
    char magic[4]; // Fixat la "IPC4" pentru validare
    uint32_t num_workers;
    bool terminate_flag;
} ipc_shared_t;

#endif
