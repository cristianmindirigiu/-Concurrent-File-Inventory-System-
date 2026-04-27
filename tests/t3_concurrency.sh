#!/bin/bash

#testeaza concurenta si gen. rapoarte de diff


ERORI=0

#curatare

echo "Curatare baze de date vechi.."
rm -f data/index.db data/proc.db
rm -f data/index_v1.db data/index_v2.db
rm -f data/proc_v1.db data/proc_v2.db
rm -f reports/T3_filediff.txt
rm -f reports/T3_procdiff.txt

#lansare 5 instante concurente de fileops_indexer
echo "Lansam 5 instante concurente de fileops_indexer.."
./bin/fileops_indexer --root /etc &
./bin/fileops_indexer --root /usr/include &
./bin/fileops_indexer --root /var/log &
./bin/fileops_indexer --root /tmp &
./bin/fileops_indexer --root /opt &
wait

#verificare data/index.db
echo "Verificam ca data/index.db exista.."

if [ ! -f data/index.db ]
then
	echo "FAIL. data/index.db nu exista"
	ERORI=$((ERORI+1))

fi

#verif daca snapshot-ul e sealed. sealed = 01 , open = 00
STATE=$(xxd -s 16 -l 1 -p data/index.db)

if [ "$STATE" != "01" ]
then 
	echo "Snapshot-ul NU e SEALED"
	ERORI=$((ERORI+1))
fi


#lansare 5 instante concurente de proc_snapshot
echo "Lansam 5 instante concurente de proc_snapshot.."
./bin/proc_snapshot &
./bin/proc_snapshot &
./bin/proc_snapshot &
./bin/proc_snapshot &
./bin/proc_snapshot &
wait

#verificare data/proc.db
echo "Verificam ca data/proc.db exista.."
if [ ! -f data/proc.db ] 
then
	echo "FAIL. data/proc.db nu exista"
	ERORI=$((ERORI+1))
fi



#verificare daca snapshot-ul e sealed
STATE=$(xxd -s 16 -l 1 -p data/proc.db)
if [ "$STATE" != "01" ]
then
	echo "Snapshot-ul NU e SEALED"
	ERORI=$((ERORI+1))
fi




#generare 2snapshot-uri distincte de fisiere pt diff
echo "Generam 2 snapshot-uri distincte de fisiere.."

cp data/index.db data/index_v1.db
rm -f data/index.db


./bin/fileops_indexer --root /etc/cron.d &
./bin/fileops_indexer --root /profile.d &
./bin/fileops_indexer --root /etc/default &
wait

cp data/index.db data/index_v2.db


#rulare db_diff pt fisiere
echo "Rulam db_diff pentru fisiere.."
./bin/db_diff --old data/index_v1.db --new data/index_v2.db --out reports/T3_filediff.txt


#verif ca raportul nu e gol

if [ ! -s reports/T3_filediff.txt ]
then
	echo "Eroare. Raportul este gol"
	ERORI=$((ERORI+1))
fi


#generare 2 snapshoturi diferite de procese pt diff
echo "Generam 2 snapshot-uri distincte de procese.."

cp data/proc.db data/proc_v1.db
rm -f data/proc.db
sleep 2 # ca sa mai apara/dispara procese intre cele 2 snapshoturi

./bin/proc_snapshot &
./bin/proc_snapshot &
./bin/proc_snapshot &
wait

cp data/proc.db data/proc_v2.db


#rulare db pt 
./bin/db_diff --old data/proc_v1.db --new data/proc_v2.db --out reports/T3_procdiff.txt

if [ ! -s reports/T3_procdiff.txt ]
then
	echo "Eroare. Raportul este gol"
	ERORI=$((ERORI+1))
fi



#rezumat
echo "===REZUMAT TEST==="
echo "Total erori: $ERORI"

if [ $ERORI -eq 0 ]
then
	echo "PASS. Toate testele au trecut."
	exit 0
else 
	echo "FAIL. $ERORI verificari au esuat."
	exit 1
fi










