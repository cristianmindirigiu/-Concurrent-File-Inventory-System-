
# Formatul Bazei de Date Binare Finale (Inventory DB)

Baza de date produsa de procesul manager (workerii nu scriu direct in ea) salveaza o imagine (snapshot) a tututor fisierelor regulate descoperite de sistem pe disc.

## 1. Reguli de generare si Atomicitiate
Pentru ca fisierul `.db` sa fie mereu viabil si sa nu prindem baze de date trunchiate (ex. daca pica managerul la jumatatea operatiei), scrierea se face 100% **atomic**.
Managerul creeaza mai intai un fisier temporar (`data/inventory.db.tmp`). Dupa ce toata scrierea s-a terminat cu succes si am pus flag-ul de complet, abia la sfarsit se face `rename()` peste fisierul original.

## 2. Layout Baza de Date
Formatul este extrem de liniar, gandit pentru parsare secventiala simpla in cod C (`fread` si `fwrite`):

### A. Header Minim
Se gaseste fix la offset-ul `0` (inceputul fisierului) si e dat de structura `inv4_header_t`.
- `magic`: Sirul "INV4". Daca nu e INV4, aplicatia da direct abort.
- `format_version`: Este `1`.
- `complete`: Flag ce indica ca exportul s-a terminat ok. Initial e 0 pe parcursul salvarii fisierului `.tmp`, apoi devine 1 inainte de `rename()`. Poate ramane `0` daca se planuieste oprirea executiei pentru extensia din Tema 5.
- `file_record_count`: Numarul exact de fisiere inregistrate mai jos.
- `worker_count`: Cati workeri au contribuit cu munca.

### B. Inregistrarile de Fisiere (File Records)
Imediat dupa header urmam array-ul de tip `file_record_t`, pus unul dupa altul exact `file_record_count` ori. 
Fiecare bloc contine calea integrala a fisierului, informatii din structura POSIX `stat` (dimensiune, timestamp modificare, uid/gid) si semnatura criptografica SHA256 calculata in format hexazecimal (string).

### C. Statisticile de la Workeri (Trailer/Worker Stats)
La coada bazei de date (sau imediat dupa ultima inregistrare fisier), scriem un sir de structuri `inv4_worker_stat_t`, fix `worker_count` de multe ori.
Aceasta zona raporteaza cate joburi/fisiere/bytes a inghitit fiecare worker individual. Foarte important, raporteaza timpii preluati intern din `getrusage()` (User CPU Time, Sys CPU Time) si durata Wall Time.

## 3. Criterii de Validitate (`--verify`)
Comanda de validare a manager-ului declara baza de date ca fiind corupta sau invalida daca:
- Header-ul nu se poate citi complet.
- Codul magic nu este perfect identic cu "INV4".
- Flag-ul `complete` nu este `1`.
- Marimea totala a fisierului calculata din structurile de operare nu "bate" cu marimea fizica in bytes a bazei de date (formula cu sizeof adunate).