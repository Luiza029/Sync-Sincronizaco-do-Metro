# Sincronização do Metrô (sync_car)
## Arquivos

`sync_car.c`: Implementação das funções station_init, station_load_car, station_wait_for_car e station_on_board. 
`sync_car.h`: Declaração da struct station (mutex, variáveis de condição e contadores) e dos protótipos das funções. 
`sync_car_test.c`: Programa de teste fornecido pelo professor. 
Makefile: Compila o projeto e gera o executável sync_car. 

## Como compilar

Requisitos: `gcc` e `make`.

```bash
make
```

Gera o executável `sync_car`.

Para apagar os arquivos compilados (`.o` e o executável):

```bash
make clean
```

## Como executar e testar

```bash
./sync_car
```

ou

```bash
make run
```

## Sistema operacional

Desenvolvido e testado em **[Ubuntu XX.XX]** rodando no **WSL2** (Windows [10/11]),
com **[gcc versão X.X.X]** e GNU Make.