# T5 Control Plane


Documentatie pentru mecanismele de control adaugate in T5 peste data plane-ul din T4.


## Separarea planurilor

- Data Plane : job queue, canale de rezultate si statistici in mmap (MAP_SHARED) -> neschimbat din T4
- Control Plane : mesaje scurte de progres/eroare prin pipe anonim, worker->manager
- Signal Plane : SIGUSR1, SIGINT, SIGTERM, SIGCHLD

## Pipe anonim (control-fd)

Fiecare worker primeste un file descriptor prin argumentul --control-fd. Acest fd e capatul de scriere al unui pipe anonim creat de manager inainte de fork(). Capetele nefolosite sunt inchise dupa fork().


## Format mesaje

Fiecare mesaj respecta regulile:
- Incepe cu T5MSG
- Se termina cu newline
- Este scris intr-un singur apel write()
- Are dimensiune <= PIPE_BUF


Tipuri de mesaje:
- JOB_DONE : trimis dupa procesarea unui director, el contine worker_id, numar de fisiere gasite si bytes
- WORKER_EXITING : trimis inainte de exit. Contine worker_id si motivul (normal sau shutdown)
- ERROR : trimis la erori locale (de ex. opendir esuat). Contine errno si locul erorii
- HEARTBEAT : dovada de activitate
Ex: T5MSG type=JOB_DONE worker_id=0 jobs=3 files=12 bytes=4096

## Semnale

### SIGUSR1 (manager)
Handlerul setează un flag volatile sig_atomic_t. Bucla principală verifică flag-ul și, dacă e setat, afișează o linie de forma:
STATUS queued_jobs=N active_jobs=N files=N bytes=N workers_alive=N complete=0
Nu se face printf sau malloc în handler.

### SIGINT / SIGTERM (manager)
Initializarea shutdown-ului gratios:
1. Se seteaza terminate_flag in IPC
2. Se trezesc workerii blocati pe semafoare
3. Se trimite SIGTERM fiecarui worker activ
4. Se asteapta graceful-timeout secunde
5. Daca raman workeri dupa timeout, li se trimite SIGKILL
6. Se face waitpid pentru toti copiii
7. Se scrie DB-ul cu complete=1 (DB_PART_COMPLETE)

### SIGTERM (worker)
Worker-ul:
1. Nu mai preia job-uri noi
2. Nu corupe IPC-ul
3. Trimite WORKER_EXITING pe pipe
4. Iese determinist cu exit(0)

### SIGCHLD (manager)
Handlerul seteaza flag. Bucla principala apeleaza waitpid(-1, ..., WNOHANG) in bucla pentru reap complet. Nu raman zombie.

## Semantica complete

- DB_START (0): DB in curs de scriere (nu se mai foloseste in practica, doar intern pe tmp)
- DB_PART_COMPLETE (1): DB valid structural dar inventariere intrerupta (shutdown, kill, eroare)
- DB_FULL_COMPLETE (2): DB valid si inventariere finalizata complet

--verify accepta atat complete=1 cat si complete=2.
--dump afiseaza mereu cheia complete.
