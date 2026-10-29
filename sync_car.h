#ifndef SYNC_CAR_H
#define SYNC_CAR_H
#include <pthread.h>

struct station{
    pthread_mutex_t mutex;
    pthread_cond_t carro_chegou;
    pthread_cond_t carro_pode_sair;

    int passageiros_esperando;
    int assentos_livre;
    int passageiros_sem_board;

};

void station_init(struct station *station);
void station_load_car(struct station *station, int count);
void station_wait_for_car(struct station *station);
void station_on_board(struct station *station);
#endif