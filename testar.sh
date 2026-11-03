N=${1:-300}
FALHOU=0

passo() { echo; echo "=== $1"; }
ok()    { echo "    OK"; }
erro()  { echo "    ERRO: $1"; FALHOU=1; }

passo "1. make clean + make (sem warnings)"
make clean > /dev/null
if ! make > build.log 2>&1; then
    cat build.log; erro "não compilou"; exit 1
fi
if grep -qi "warning" build.log; then
    cat build.log; erro "compilou com warnings"
else
    ok
fi

passo "2. teste do professor, $N vezes"
for i in $(seq 1 "$N"); do
    if ! ./sync_car > saida.log 2>&1; then
        cat saida.log; erro "falhou na execução $i"; break
    fi
done
[ "$i" = "$N" ] && grep -q "Looks good" saida.log && ok

if command -v taskset > /dev/null; then
    M=$((N / 3))
    passo "3. teste do professor em 1 núcleo só, $M vezes (outra ordem de threads)"
    for i in $(seq 1 "$M"); do
        if ! taskset -c 0 ./sync_car > saida.log 2>&1; then
            cat saida.log; erro "falhou na execução $i"; break
        fi
    done
    [ "$i" = "$M" ] && ok
fi

passo "4. compilando teste_extra"
if gcc -Wall -Wextra -pthread sync_car.c teste_extra.c -o teste_extra; then
    ok
else
    erro "teste_extra não compilou"; exit 1
fi

passo "5. teste_extra (detalhado)"
./teste_extra || erro "teste_extra falhou"

passo "6. teste_extra mais 15 vezes (5 delas em 1 núcleo)"
for i in $(seq 1 15); do
    if [ "$i" -le 5 ] && command -v taskset > /dev/null; then
        CMD="taskset -c 0 ./teste_extra"
    else
        CMD="./teste_extra"
    fi
    if ! $CMD > extra.log 2>&1; then
        grep -E "FALHOU|TRAVOU|->" extra.log; erro "falhou na execução $i"; break
    fi
done
[ "$i" = "15" ] && ok

passo "7. ThreadSanitizer (procura acesso a contador sem o mutex)"
if gcc -g -fsanitize=thread -pthread sync_car.c teste_extra.c -o teste_tsan 2> /dev/null; then
    ./teste_tsan > tsan.log 2>&1
    if grep -q "WARNING: ThreadSanitizer" tsan.log; then
        grep -A 20 "WARNING: ThreadSanitizer" tsan.log | head -40
        erro "ThreadSanitizer achou condição de corrida"
    elif grep -q "testes passaram" tsan.log; then
        ok
    else
        tail -5 tsan.log; erro "teste_tsan não terminou"
    fi
else
    echo "    (pulado: seu gcc não tem suporte a -fsanitize=thread)"
fi

passo "8. make clean remove os arquivos compilados"
make clean > /dev/null
if [ -e sync_car ] || ls ./*.o > /dev/null 2>&1; then
    erro "sobrou executável ou .o depois do make clean"
else
    ok
fi

rm -f build.log saida.log extra.log tsan.log teste_extra teste_tsan

echo
if [ "$FALHOU" = 0 ]; then
    echo "TUDO PASSOU"
else
    echo "ALGO FALHOU (veja acima)"
fi
exit $FALHOU
