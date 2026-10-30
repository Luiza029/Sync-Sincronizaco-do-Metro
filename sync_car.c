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
}

void station_wait_for_car(struct station *station){
    pthread_mutex_lock(&station->mutex);
    station->passageiros_esperando++;

    while(station->assentos_livre == 0){
        pthread_cond_wait(&station->carro_chegou, &station->mutex);
    }
    
    station->assentos_livre--;
    station->passageiros_esperando--;
    station->passageiros_sem_board++;

    pthread_mutex_unlock(&station->mutex);
}

void station_on_board(struct station *station){
    pthread_mutex_lock(&station->mutex);

    station->passageiros_sem_board--;

    if(station->passageiros_sem_board == 0 && (station->assentos_livre == 0 || station->passageiros_esperando == 0)){
        pthread_cond_signal(&station->carro_pode_sair);
    }

    pthread_mutex_unlock(&station->mutex);
}
