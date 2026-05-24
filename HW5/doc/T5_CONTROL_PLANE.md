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

