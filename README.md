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

## Testes extras

```bash
chmod +x testar.sh
./testar.sh
```

Ao final deve aparecer `TUDO PASSOU`.

## Sistema operacional

Desenvolvido e testado em **Ubuntu 26.04 LTS** rodando no **WSL2** (Windows 11),
com **gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0** e GNU Make.