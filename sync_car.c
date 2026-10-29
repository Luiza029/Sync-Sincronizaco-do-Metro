#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

struct station{
    pthread_mutex_t mutex;
    pthread_cond_t carro_chegou;
    pthread_cond_t carro_pode_sair;

    int passageiros_esperando;
    int assentos_livre;
    int passageiros_sem_board;

};

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

}

int main(void){

    return 0;
}