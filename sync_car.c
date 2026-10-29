#include "sync_car.h"
#include <stdio.h>
#include <stdlib.h>

void station_init(struct station *station){
    pthread_mutex_init(&station->mutex, NULL);
    pthread_cond_init(&station->carro_chegou, NULL);
    pthread_cond_init(&station->carro_pode_sair, NULL);

    station->passageiros_esperando = 0;
    station->assentos_livre = 0;
    station->passageiros_sem_board = 0;
}

void station_load_car(struct station *station, int count){
    pthread_mutex_lock(&station->mutex);

    while(station->assentos_livre == 0){
        pthread_cond_wait(&station->carro_chegou, &station->mutex);
    }
    station->assentos_livre--;

    pthread_mutex_unlock(&station->mutex);
}

void station_wait_for_car(struct station *station){

}
