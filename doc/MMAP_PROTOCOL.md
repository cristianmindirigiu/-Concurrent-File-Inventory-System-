# Protocolul de Comunicare IPC (Memory Mapped)

[cite_start]Acest document descrie modul in care procesul manager si procesele worker comunica si se sincronizeaza prin intermediul unui fisier mapat in memorie cu `mmap()` si flag-ul `MAP_SHARED`[cite: 26]. 

[cite_start]Layout-ul fișierului IPC este proiectat sa fie stabil, ferit de race conditions și să ofere mecanisme de backpressure[cite: 124, 137].

## Structura globala a memoriei (Layout)
Memoria mapata contine o singura instanta de tip `ipc_shared_t`, care este delimitata in felul urmator:

1. **IPC Header**: Contine date de identificare (`magic` setat pe "IPC4"), versiunea, numarul de workeri si un semafor general `header_mutex`. Mai contine si variabila `terminate_flag`, folosita de manager ca sa opreasca ordonat workerii.
2. **Job Queue (Coada de directoare)**: Un singur Ring Buffer (buffer circular) comun, protejat prin semafoare, unde se pun directoarele ce urmeaza a fi scanate.
3. **Canale de Rezultate (Result Channels)**: Un array de Ring Buffers, cate unul **pentru fiecare worker**. Fiecare worker scrie doar pe canalul lui, iar managerul citeste din toate. 
4. **Worker Stats**: Un array de structuri actualizate in timp real de catre workeri cu informatii despre performanta (fisiere procesate, CPU etc.).

## Principiul de Buffer Circular (Ring Buffer)
Toate cozile de date sunt implementate ca buffere circulare. [cite_start]Exista un vector de marime fixa, un index `head` (urmatoarea citire) si un `tail` (urmatoarea scriere)[cite: 70, 71]. [cite_start]Ele avanseaza mereu cu operatia *modulo capacitatea cozii*[cite: 72]. Cand un index atinge capacitatea maxima, se intoarce automat la indexul 0.

## Sincronizarea si Backpressure-ul
[cite_start]Pentru a preveni coruperea cozilor si a implementa regula de backpressure, se folosesc semafoare POSIX direct in zona partajata[cite: 140, 141]:

* **Evitarea Overflow-ului (Backpressure)**: Fiecare coada are un semafor `slots_libere`. Producatorul ii da `sem_wait`. [cite_start]Daca bufferul e plin (0 slots_libere), producatorul se blocheaza din executie[cite: 74, 137]. [cite_start]Asta garanteaza ca un worker nu va pierde si nu va suprascrie date daca managerul citeste prea incet[cite: 138].
* **Evitarea Underflow-ului**: Fiecare coada are un semafor `slots_ocupate`. Consumatorul da `sem_wait`. Daca e 0, asteapta in liniste pana apare ceva pe teava.
* **Excluderea mutuala**: Modificarea variabilelor `head`, `tail` sau `count` se face mereu incadrata de `sem_wait(&mutex)` si `sem_post(&mutex)`.

## Mecanismul de Oprire Determinista
Managerul considera ca scanarea e gata cand ambele conditii sunt indeplinite simultan in sectiunea critica a cozii de job-uri:
1. `count == 0` (Nu mai e nimic in asteptare).
2. `jobs_in_progress == 0` (Niciun worker nu mai scaneaza fizic vreo ramura pe disc).

Atunci, managerul seteaza `terminate_flag = true` si le face post workerilor adormiti pe semaforul de citire ca sa poata evalua steagul si sa isi dea `exit()`.