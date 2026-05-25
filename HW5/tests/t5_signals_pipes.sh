#!/bin/bash

echo "[TEST T5] Test semnale, pipe-uri si DB incomplet..."

TEST_DIR="tmp/test_t5_scenario"
DB_FILE="data/inventory_t5.db"
IPC_FILE="data/ipc_t5.mmap"
PID_FILE="tmp/manager_t5.pid"
DUMP_OUT="tmp/dump_t5.txt"
STATUS_OUT="tmp/status_t5.txt"

# 1. cream un arbore de test mare ca sa tina managerul ocupat
echo "[TEST T5] 1. Creare arbore de test..."
rm -rf "$TEST_DIR"
mkdir -p "$TEST_DIR"

for i in $(seq 1 5); do
    mkdir -p "$TEST_DIR/dir_$i"
    for j in $(seq 1 10); do
        echo "continut fisier $i-$j cu date suficiente pentru test" > "$TEST_DIR/dir_$i/file_${j}.txt"
    done
    for k in $(seq 1 3); do
        mkdir -p "$TEST_DIR/dir_$i/sub_$k"
        for l in $(seq 1 5); do
            echo "sub continut $i-$k-$l" > "$TEST_DIR/dir_$i/sub_$k/f_${l}.dat"
        done
    done
done

echo "[TEST T5] Arbore creat cu succes."

# 2. pornim managerul cu simulate-work-ms si pid-file
echo "[TEST T5] 2. Pornire manager cu --simulate-work-ms=200..."
./bin/fileops_manager \
    --root "$TEST_DIR" \
    --workers 2 \
    --ipc "$IPC_FILE" \
    --db "$DB_FILE" \
    --simulate-work-ms 200 \
    --pid-file "$PID_FILE" \
    --graceful-timeout 3 \
    > "$STATUS_OUT" 2>&1 &

MANAGER_BG_PID=$!

# 3. asteptam aparitia fisierului PID prin polling
echo "[TEST T5] 3. Asteptare PID file..."
WAIT_COUNT=0
while [ ! -f "$PID_FILE" ] && [ $WAIT_COUNT -lt 30 ]; do
    sleep 0.2
    WAIT_COUNT=$((WAIT_COUNT + 1))
done

if [ ! -f "$PID_FILE" ]; then
    echo "[TEST T5] FAIL: PID file nu a aparut dupa 6 secunde."
    kill $MANAGER_BG_PID 2>/dev/null
    wait $MANAGER_BG_PID 2>/dev/null
    exit 1
fi

MANAGER_PID=$(cat "$PID_FILE")
echo "[TEST T5] Manager PID: $MANAGER_PID"

# 4. trimitem SIGUSR1 si verificam STATUS
echo "[TEST T5] 4. Trimitere SIGUSR1..."
sleep 1
kill -USR1 "$MANAGER_PID" 2>/dev/null
sleep 1

if grep -q "^STATUS " "$STATUS_OUT"; then
    echo "[TEST T5] OK: Linia STATUS a fost gasita."
    grep "^STATUS " "$STATUS_OUT"
else
    echo "[TEST T5] WARN: Linia STATUS nu a fost gasita inca (poate a fost prea rapid)."
fi

# 5. trimitem SIGTERM inainte de finalizare
echo "[TEST T5] 5. Trimitere SIGTERM..."
kill -TERM "$MANAGER_PID" 2>/dev/null

# 6. asteptam terminarea managerului
echo "[TEST T5] 6. Asteptare terminare manager..."
wait $MANAGER_BG_PID 2>/dev/null

# 7. verificam ca nu exista procese zombie
echo "[TEST T5] 7. Verificare procese zombie..."
ZOMBIES=$(ps aux 2>/dev/null | grep "fileops_worker" | grep -v grep | grep "Z" | wc -l)
if [ "$ZOMBIES" -gt 0 ]; then
    echo "[TEST T5] FAIL: Au ramas $ZOMBIES procese zombie."
    exit 1
fi
echo "[TEST T5] OK: Niciun zombie."

# 8. verificam existenta DB-ului
echo "[TEST T5] 8. Verificare existenta DB..."
if [ ! -f "$DB_FILE" ]; then
    echo "[TEST T5] FAIL: DB-ul $DB_FILE nu a fost creat."
    exit 1
fi
echo "[TEST T5] OK: DB-ul exista."

# 9. rulam --verify (trebuie sa accepte si complete!=2)
echo "[TEST T5] 9. Rulare --verify pe DB incomplet..."
./bin/fileops_manager --verify --db "$DB_FILE"
VERIFY_STATUS=$?

if [ $VERIFY_STATUS -ne 0 ]; then
    echo "[TEST T5] FAIL: --verify a esuat pe DB incomplet."
    exit 1
fi
echo "[TEST T5] OK: --verify accepta DB-ul."

# 10. verificam ca --dump arata complete=1 (DB_PART_COMPLETE)
echo "[TEST T5] 10. Verificare --dump complete=1..."
./bin/fileops_manager --dump --db "$DB_FILE" > "$DUMP_OUT"

if grep -q "complete=1" "$DUMP_OUT"; then
    echo "[TEST T5] OK: DB-ul are complete=1 (inventariere incompleta, structural valid)."
elif grep -q "complete=2" "$DUMP_OUT"; then
    echo "[TEST T5] WARN: DB-ul are complete=2 (inventarierea s-a terminat inainte de SIGTERM)."
else
    echo "[TEST T5] FAIL: Campul complete lipseste din dump."
    exit 1
fi

# 11. verificam ca au existat mesaje T5MSG in output
echo "[TEST T5] 11. Verificare mesaje T5MSG..."
if grep -q "T5MSG" "$STATUS_OUT" 2>/dev/null; then
    echo "[TEST T5] INFO: Mesaje T5MSG gasite in output."
fi

echo ""
echo "[TEST T5] SUCCESS! Toate verificarile au trecut."
exit 0
