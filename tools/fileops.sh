#!/bin/bash


#FUNCTII

cmd_init() {
	echo "[INIT] Se creeaza structura de directoare..."
	mkdir -p bin src include data logs reports tmp/obj tests doc tools

	echo "[INIT] Se verifica existenta compilatorului GCC..."
	if ! command -v gcc >/dev/null 2>&1
	then
		echo: "Eroare! Compilatorul GCC nu este instalat pe acest sistem."
		return 1
	fi
	echo "Succes. Structura a fost finalizata, iar compilatorul GCC este instalat."
	return 0
}

cmd_clean() {
	echo "[CLEAN] Se sterg toate artefactele generate de [BUILD]..."
	rm -rf tmp/obj/*
	rm -rf bin/*
	echo "[CLEAN] Curatare efectuata cu succes."

}

cmd_run() {
	if [ "$1" != "--" ]
	then
		echo "Eroare. Sintaxta tb sa fie de forma: run -- <exe> [args]"
		return 1
	fi
	shift

	EXE_ARG=$1
	shift

	if [ ! -x "bin/$EXE_ARG" ]
	then
		echo "Eroare. Executabilul 'bin/$EXE_ARG' nu a fost gasit."
		return 1
	fi

	echo "[RUN] Se ruleaza bin/$EXE_ARG cu argumentele $@"
	"bin/$EXE_ARG" "$@"
	return $?
}

cmd_test() {
	echo "[TEST] Se ruleaza testele automate"
	RAPORT="reports/T2_tests.txt"
	> "$RAPORT"

	ERORI=0
	FISIERE_TEST=$(find tests -type f -name "*.sh")

	for test_script in $FISIERE_TEST
	do
		echo "Se ruleaza $test_script: "
		if bash "$test_script"
		then
			echo "$test_script : PASS" >> "$RAPORT"
		else
			echo "$test_script : FAIL" >> "$RAPORT"
			let ERORI++
		fi
	done

	if [ $ERORI -gt 0 ] 
	then
		echo "[TEST] Au esuat $ERORI teste. Verificati $RAPORT"
	else
		echo "[TEST] Toate testele au trecut. Raporartele au fost salvate in $RAPORT"
	fi
}

cmd_build() {
	DIR_SURSA="src"
	if [ "$1" == "--src" ] && [ -n "$2" ]
	then
		DIR_SURSA="$2"
	fi

	if [ ! -d "$DIR_SURSA" ] 
	then
		echo "Eroare, directoriul sursa nu exista."
		return 1
	fi

	mkdir -p tmp/obj bin
	echo "[BUILD] Se cauta fisiere .c in directoriul '$DIR_SURSA'.."

	parcurgere() {
		local folder_cur="$1"
		for elem in "$folder_cur"/*
		do
			if [ -d "$elem" ]; then
				parcurgere "$elem"
			elif [ -f "$elem" ] && [[ "$elem" == *.c ]]; then
				local nume_baza=$(basename "$elem")
				local nume_obj="${nume_baza%.c}.o"
				local cale_obj="tmp/obj/$nume_obj"

				if [ ! -f "$cale_obj" ] || [ "$elem" -nt "$cale_obj" ]; then
					echo "Se compileaza $elem"
					gcc -c "$elem" -o "$cale_obj" -Iinclude -I"$DIR_SURSA" -I"$DIR_SURSA/include"
				else
					echo "Se sare peste $elem(nu a mai fost modificat)"
				fi
			fi
		done
	}
	parcurgere "$DIR_SURSA"

	#Linkage
	FISIERE_MAIN=$(find "$DIR_SURSA" -type f -name "main_*.c")
	if [ -z "$FISIERE_MAIN" ] 
	then
		echo "[BUILD] Nu a fost gasit niciun fisier 'main_*.c'"
		return 0
	fi
	
	MOD_AUXILIARE=$(find tmp/obj -type f -name "*.o" ! -name "main_*.o")

	for main_c in $FISIERE_MAIN
	do
		nume_baza=$(basename "$main_c")
		nume_exe="${nume_baza#main_}"
		nume_exe="${nume_exe%.c}"
		cale_main_obj="tmp/obj/${nume_baza%.c}.o"

		echo "[BUILD] Se genereaza executabilul: bin/$nume_exe"
		gcc "$cale_main_obj" $MOD_AUXILIARE -o "bin/$nume_exe"
	done

	echo "[BUILD] SUCCES."
	return 0
}



#main


if [ $# -eq 0 ] 
then
	echo "Nicio comanda specificata."
	exit 1
fi

COMANDA="$1"
shift

START_TIME=$(date +%s)
FORMAT_DATA=$(date +"%Y%m%d_%H%M%S")
LOG_FILE="logs/fileops_${FORMAT_DATA}.log"


case "$COMANDA" in 
	init) cmd_init "$@" ;;
	build) cmd_build "$@" ;;
	run) cmd_run "$@" ;;
	test) cmd_test "$@" ;;
	clean) cmd_clean "$@" ;;
	*)
		echo "Comanda Necunoscuta '$COMANDA'"
		EXIT_CODE=1
		;;
esac


EXIT_CODE=$?
END_TIME=$(date +%s)
DIFF_TIME=$((END_TIME - START_TIME))

mkdir -p logs
echo "Subcomanda rulata: $COMANDA" > "$LOG_FILE"
echo "Timestamp start: $START_TIME" >> "$LOG_FILE"
echo "Timestamp end: $END_TIME" >> "$LOG_FILE"
echo "Time diff: $DIFF_TIME" >> "$LOG_FILE"
echo "Exit code: $EXIT_CODE" >> "$LOG_FILE"

exit $EXIT_CODE


