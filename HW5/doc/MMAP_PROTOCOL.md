# Protocolul de Comunicare IPC (Memory Mapped)

Procesul manager si procesele worker comunica prin intermediul unui fisier mapat in memorie cu mmap() si MAP_SHARED.

## Structura memoriei (Layout)

Memoria contine o singura instanta ipc_shared_t:

1. IPC Header: magic "IPC4", versiune, numar workeri, terminate_flag, header_mutex
2. Job Queue: un ring buffer comun protejat prin semafoare, unde se pun directoarele de scanat
3. Result Channels: cate un ring buffer per worker. Worker-ul scrie pe canalul lui, managerul citeste din toate.
4. Worker Stats: array de structuri actualizate in timp real de workeri

## Ring Buffer

Vector de marime fixa cu indexi head (citire) si tail (scriere) care avanseaza modulo capacitate.

## Sincronizare si Backpressure

Semafoare POSIX in zona partajata:
- slots_libere: producatorul face sem_wait. Daca buffer-ul e plin, se blocheaza.
- slots_ocupate: consumatorul face sem_wait. Daca e gol, asteapta.
- mutex: protectie head/tail/count

## Oprire Determinista

Managerul verifica in sectiune critica:
1. count == 0 (nimic in asteptare)
2. jobs_in_progress == 0 (niciun worker nu mai scaneaza)

Atunci seteaza terminate_flag = true si face sem_post workerilor adormiti ca sa se trezeasca si sa iasa.

In T5, terminate_flag se seteaza si la primirea SIGINT/SIGTERM de catre manager.
