#define _GNU_SOURCE
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "sync_car.h"

static const char *teste_atual = "";
static int falhas_teste = 0;
static int total_falhas = 0;
static int total_testes = 0;

#define CHECK(cond, ...)                                    \
    do {                                                    \
        if (!(cond)) {                                      \
            printf("      -> ");                            \
            printf(__VA_ARGS__);                            \
            printf("\n");                                   \
            falhas_teste++;                                 \
        }                                                   \
    } while (0)

static void alarme(int sig)
{
    (void) sig;
    /* printf não é async-signal-safe, mas aqui só queremos a mensagem antes de sair */
    printf("[TRAVOU] %s: passou do tempo limite (deadlock ou thread que nunca acorda)\n",
           teste_atual);
    fflush(stdout);
    _exit(1);
}

static int ler(int *p) { return __atomic_load_n(p, __ATOMIC_SEQ_CST); }
static void somar(int *p, int v) { __atomic_add_fetch(p, v, __ATOMIC_SEQ_CST); }
static void gravar(int *p, int v) { __atomic_store_n(p, v, __ATOMIC_SEQ_CST); }

static void dormir_ms(int ms) { usleep(ms * 1000); }

/* Espera (no teste, não no seu código) até *p >= alvo, ou até estourar o tempo. */
static int esperar_ate(int *p, int alvo, int timeout_ms)
{
    for (int t = 0; t < timeout_ms; t++) {
        if (ler(p) >= alvo)
            return 1;
        dormir_ms(1);
    }
    return ler(p) >= alvo;
}

static double cpu_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/* Contexto de cada teste: uma estação + contadores observados pelo teste. */
struct ctx {
    struct station st;
    int retornaram;      /* passageiros que saíram do wait_for_car */
    int vagao_saiu;      /* 1 quando o load_car retornou */
    int assentos;        /* argumento do próximo load_car */
    pthread_t vagao_tid;
};

static struct ctx *novo_ctx(void)
{
    /* Nunca liberamos: passageiros que sobram de um teste ficam dormindo
     * nessa estação para sempre, e ela precisa continuar existindo. */
    struct ctx *c = calloc(1, sizeof(*c));
    if (!c) { perror("calloc"); exit(1); }
    station_init(&c->st);
    return c;
}

/* Passageiro "controlado": só chama wait_for_car.
 * Quem chama on_board é o main, na hora que o teste quiser
 * (igual ao teste do professor). */
static void *passageiro(void *arg)
{
    struct ctx *c = arg;
    station_wait_for_car(&c->st);
    somar(&c->retornaram, 1);
    return NULL;
}

static void criar_passageiros(struct ctx *c, int n)
{
    for (int i = 0; i < n; i++) {
        pthread_t t;
        if (pthread_create(&t, NULL, passageiro, c) != 0) {
            perror("pthread_create");
            exit(1);
        }
        pthread_detach(t);
    }
    /* dá tempo de todos chegarem na estação e dormirem */
    dormir_ms(100);
}

static void *vagao(void *arg)
{
    struct ctx *c = arg;
    station_load_car(&c->st, c->assentos);
    gravar(&c->vagao_saiu, 1);
    return NULL;
}

static void iniciar_vagao(struct ctx *c, int assentos)
{
    c->assentos = assentos;
    gravar(&c->vagao_saiu, 0);
    if (pthread_create(&c->vagao_tid, NULL, vagao, c) != 0) {
        perror("pthread_create");
        exit(1);
    }
}

/* Espera o vagão partir. Retorna 1 se partiu dentro do tempo. */
static int esperar_vagao(struct ctx *c, int timeout_ms)
{
    if (!esperar_ate(&c->vagao_saiu, 1, timeout_ms))
        return 0;
    pthread_join(c->vagao_tid, NULL);
    return 1;
}

static void on_board_n(struct ctx *c, int n)
{
    for (int i = 0; i < n; i++)
        station_on_board(&c->st);
}

static void rodar(const char *nome, void (*f)(void), int limite_s)
{
    teste_atual = nome;
    falhas_teste = 0;
    total_testes++;
    alarm(limite_s);
    f();
    alarm(0);
    if (falhas_teste == 0) {
        printf("[OK]     %s\n", nome);
    } else {
        printf("[FALHOU] %s\n", nome);
        total_falhas++;
    }
    fflush(stdout);
}

/* ------------------------------------------------------------
 * Testes
 * ------------------------------------------------------------ */

/* Vagão chega e não tem ninguém: deve partir na hora, com qualquer count. */
static void t_vagao_sem_passageiros(void)
{
    struct ctx *c = novo_ctx();
    int counts[] = {0, 1, 10, 1000};
    for (int i = 0; i < 4; i++) {
        iniciar_vagao(c, counts[i]);
        CHECK(esperar_vagao(c, 500),
              "load_car(%d) sem passageiros não retornou", counts[i]);
    }
}

/* Passageiro sem vagão na estação não pode sair do wait_for_car. */
static void t_passageiro_sem_vagao_espera(void)
{
    struct ctx *c = novo_ctx();
    criar_passageiros(c, 5);
    dormir_ms(200);
    CHECK(ler(&c->retornaram) == 0,
          "%d passageiro(s) saíram do wait_for_car sem vagão na estação",
          ler(&c->retornaram));
}

/* Vagão cheio (0 assentos) com fila: parte na hora, ninguém embarca. */
static void t_vagao_zero_assentos_com_fila(void)
{
    struct ctx *c = novo_ctx();
    criar_passageiros(c, 5);
    iniciar_vagao(c, 0);
    CHECK(esperar_vagao(c, 500), "load_car(0) com fila não retornou");
    dormir_ms(100);
    CHECK(ler(&c->retornaram) == 0,
          "%d passageiro(s) embarcaram num vagão com 0 assentos",
          ler(&c->retornaram));
}

/* Vagão parte com assentos sobrando. Passageiro que chega DEPOIS
 * não pode embarcar nele (as "portas" precisam fechar). */
static void t_portas_fechadas(void)
{
    struct ctx *c = novo_ctx();
    iniciar_vagao(c, 5);
    CHECK(esperar_vagao(c, 500), "load_car(5) sem passageiros não retornou");

    criar_passageiros(c, 1);
    dormir_ms(200);
    CHECK(ler(&c->retornaram) == 0,
          "passageiro embarcou em vagão que já tinha partido "
          "(assentos livres não foram zerados ao partir?)");

    /* o próximo vagão leva ele normalmente */
    iniciar_vagao(c, 1);
    CHECK(esperar_ate(&c->retornaram, 1, 500),
          "passageiro não embarcou no vagão seguinte");
    on_board_n(c, ler(&c->retornaram));
    CHECK(esperar_vagao(c, 500), "vagão seguinte não partiu");
}

/* Requisito 5: vários passageiros saem do wait_for_car antes de
 * qualquer on_board. E o vagão espera TODOS sentarem. */
static void t_embarque_simultaneo(void)
{
    struct ctx *c = novo_ctx();
    criar_passageiros(c, 10);
    iniciar_vagao(c, 10);

    CHECK(esperar_ate(&c->retornaram, 10, 1000),
          "só %d de 10 passageiros saíram do wait_for_car sem nenhum on_board "
          "(embarque não é simultâneo?)", ler(&c->retornaram));
    dormir_ms(50);
    CHECK(!ler(&c->vagao_saiu), "vagão partiu antes de qualquer on_board");

    on_board_n(c, 9);
    dormir_ms(100);
    CHECK(!ler(&c->vagao_saiu), "vagão partiu com 1 passageiro ainda sem sentar");

    on_board_n(c, 1);
    CHECK(esperar_vagao(c, 500), "vagão não partiu depois do último on_board");
}

/* Mais passageiros que assentos: embarcam exatamente 'assentos'. */
static void t_mais_passageiros_que_assentos(void)
{
    struct ctx *c = novo_ctx();
    criar_passageiros(c, 10);

    iniciar_vagao(c, 3);
    CHECK(esperar_ate(&c->retornaram, 3, 1000),
          "esperava 3 embarques, veio %d", ler(&c->retornaram));
    dormir_ms(200);
    CHECK(ler(&c->retornaram) == 3,
          "vagão de 3 assentos levou %d passageiros", ler(&c->retornaram));
    CHECK(!ler(&c->vagao_saiu), "vagão partiu antes dos on_board");

    on_board_n(c, 3);
    CHECK(esperar_vagao(c, 500),
          "vagão cheio não partiu (assentos acabaram, mas ainda tem gente na fila)");

    /* os 7 que sobraram vão no próximo */
    iniciar_vagao(c, 7);
    CHECK(esperar_ate(&c->retornaram, 10, 1000),
          "segundo vagão: esperava 10 no total, veio %d", ler(&c->retornaram));
    on_board_n(c, 7);
    CHECK(esperar_vagao(c, 500), "segundo vagão não partiu");
    dormir_ms(100);
    CHECK(ler(&c->retornaram) == 10,
          "total de embarques deveria ser 10, foi %d", ler(&c->retornaram));
}

/* Menos passageiros que assentos: vagão parte sem esperar os assentos vazios. */
static void t_menos_passageiros_que_assentos(void)
{
    struct ctx *c = novo_ctx();
    criar_passageiros(c, 3);
    iniciar_vagao(c, 10);
    CHECK(esperar_ate(&c->retornaram, 3, 1000),
          "esperava 3 embarques, veio %d", ler(&c->retornaram));
    on_board_n(c, 3);
    CHECK(esperar_vagao(c, 500),
          "vagão ficou esperando para encher em vez de partir "
          "(ninguém mais esperando)");
}

/* Passageiro que chega no meio do embarque, com assento sobrando,
 * embarca no mesmo vagão, e o vagão espera por ele. */
static void t_chega_durante_embarque(void)
{
    struct ctx *c = novo_ctx();
    criar_passageiros(c, 2);
    iniciar_vagao(c, 5);
    CHECK(esperar_ate(&c->retornaram, 2, 1000),
          "esperava 2 embarques, veio %d", ler(&c->retornaram));

    criar_passageiros(c, 1);
    CHECK(esperar_ate(&c->retornaram, 3, 1000),
          "passageiro que chegou durante o embarque não entrou no vagão");
    CHECK(!ler(&c->vagao_saiu), "vagão partiu antes dos on_board");

    on_board_n(c, 3);
    CHECK(esperar_vagao(c, 500), "vagão não partiu");
}

/* Acordar espúrio: acordamos todo mundo "à toa".
 * Com while no cond_wait ninguém pode avançar. Com if, avança. */
static void t_acordar_espurio(void)
{
    struct ctx *c = novo_ctx();

    /* passageiros sem vagão, acordados à toa */
    criar_passageiros(c, 5);
    for (int i = 0; i < 100; i++) {
        pthread_mutex_lock(&c->st.mutex);
        pthread_cond_broadcast(&c->st.carro_chegou);
        pthread_mutex_unlock(&c->st.mutex);
    }
    dormir_ms(200);
    CHECK(ler(&c->retornaram) == 0,
          "%d passageiro(s) saíram do wait_for_car num acordar espúrio "
          "(if em vez de while?)", ler(&c->retornaram));

    /* vagão esperando on_board, acordado à toa */
    iniciar_vagao(c, 5);
    CHECK(esperar_ate(&c->retornaram, 5, 1000),
          "esperava 5 embarques, veio %d", ler(&c->retornaram));
    for (int i = 0; i < 100; i++) {
        pthread_mutex_lock(&c->st.mutex);
        pthread_cond_broadcast(&c->st.carro_pode_sair);
        pthread_mutex_unlock(&c->st.mutex);
    }
    dormir_ms(200);
    CHECK(!ler(&c->vagao_saiu),
          "vagão partiu num acordar espúrio, antes dos on_board (if em vez de while?)");

    on_board_n(c, 5);
    CHECK(esperar_vagao(c, 500), "vagão não partiu");
}

/* Busy-wait: com todo mundo esperando, o processo quase não deve usar CPU. */
static void t_sem_busy_wait(void)
{
    struct ctx *c = novo_ctx();
    criar_passageiros(c, 50);

    double antes = cpu_ms();
    dormir_ms(500);
    double gasto = cpu_ms() - antes;
    CHECK(gasto < 50,
          "passageiros esperando vagão gastaram %.0f ms de CPU em 500 ms (busy-wait?)",
          gasto);

    iniciar_vagao(c, 50);
    CHECK(esperar_ate(&c->retornaram, 50, 1000),
          "esperava 50 embarques, veio %d", ler(&c->retornaram));

    antes = cpu_ms();
    dormir_ms(500);
    gasto = cpu_ms() - antes;
    CHECK(gasto < 50,
          "vagão esperando on_board gastou %.0f ms de CPU em 500 ms (busy-wait?)",
          gasto);

    on_board_n(c, 50);
    CHECK(esperar_vagao(c, 500), "vagão não partiu");
}

/* Duas estações não podem interferir uma na outra. */
static void t_estacoes_independentes(void)
{
    struct ctx *a = novo_ctx();
    struct ctx *b = novo_ctx();

    criar_passageiros(a, 3);
    iniciar_vagao(b, 10);
    CHECK(esperar_vagao(b, 500), "vagão vazio da estação B não partiu");
    dormir_ms(100);
    CHECK(ler(&a->retornaram) == 0,
          "passageiro da estação A embarcou no vagão da estação B "
          "(variáveis globais em vez de campos da struct?)");

    iniciar_vagao(a, 3);
    CHECK(esperar_ate(&a->retornaram, 3, 1000), "estação A: embarque falhou");
    on_board_n(a, 3);
    CHECK(esperar_vagao(a, 500), "estação A: vagão não partiu");
}

/* ------------------------------------------------------------
 * Estresse: passageiros chegando aleatoriamente enquanto vagões passam.
 * Aqui cada passageiro chama on_board ele mesmo, com atraso aleatório.
 * ------------------------------------------------------------ */

#define ESTRESSE_PASSAGEIROS 500

static struct station est;
static int no_vagao;          /* saíram do wait_for_car no vagão atual */
static int chamaram_on_board; /* desses, quantos já chamaram on_board */
static int terminaram;        /* passageiros que terminaram tudo */

static void *passageiro_estresse(void *arg)
{
    (void) arg;
    station_wait_for_car(&est);
    somar(&no_vagao, 1);
    usleep(random() % 200);          /* demora para sentar */
    somar(&chamaram_on_board, 1);    /* antes do on_board: o vagão não pode sair antes disso */
    station_on_board(&est);
    somar(&terminaram, 1);
    return NULL;
}

static void *gerador_passageiros(void *arg)
{
    (void) arg;
    for (int i = 0; i < ESTRESSE_PASSAGEIROS; i++) {
        pthread_t t;
        if (pthread_create(&t, NULL, passageiro_estresse, NULL) != 0) {
            perror("pthread_create");
            exit(1);
        }
        pthread_detach(t);
        if (random() % 4 == 0)
            usleep(random() % 300);
    }
    return NULL;
}

static void t_estresse(void)
{
    station_init(&est);
    no_vagao = chamaram_on_board = terminaram = 0;

    pthread_t g;
    pthread_create(&g, NULL, gerador_passageiros, NULL);

    int embarcados = 0, vagoes = 0, erros = 0;
    while (embarcados < ESTRESSE_PASSAGEIROS && erros < 5) {
        int assentos = random() % 20;   /* inclui vagões com 0 assentos */
        station_load_car(&est, assentos);
        vagoes++;

        /* Com o vagão já partido, ninguém novo pode ter saído do wait_for_car. */
        int n = __atomic_exchange_n(&no_vagao, 0, __ATOMIC_SEQ_CST);
        int ob = __atomic_exchange_n(&chamaram_on_board, 0, __ATOMIC_SEQ_CST);

        if (n > assentos) {
            CHECK(0, "vagão %d: %d assentos mas levou %d passageiros", vagoes, assentos, n);
            erros++;
        }
        if (ob != n) {
            CHECK(0, "vagão %d partiu com %d passageiro(s) sem ter chamado on_board",
                  vagoes, n - ob);
            erros++;
        }
        embarcados += n;
        if (random() % 3 == 0)
            usleep(random() % 500);
    }
    pthread_join(g, NULL);

    CHECK(embarcados == ESTRESSE_PASSAGEIROS,
          "embarcaram %d de %d passageiros", embarcados, ESTRESSE_PASSAGEIROS);
    CHECK(esperar_ate(&terminaram, ESTRESSE_PASSAGEIROS, 2000),
          "só %d passageiros terminaram o on_board", ler(&terminaram));
    printf("         (%d vagões, %d passageiros)\n", vagoes, embarcados);
}

/* ------------------------------------------------------------ */

int main(void)
{
    srandom(getpid() ^ time(NULL));
    signal(SIGALRM, alarme);
    setvbuf(stdout, NULL, _IOLBF, 0);

    rodar("vagão sem passageiros parte na hora",       t_vagao_sem_passageiros, 3);
    rodar("passageiro sem vagão fica esperando",       t_passageiro_sem_vagao_espera, 3);
    rodar("vagão com 0 assentos e fila parte na hora", t_vagao_zero_assentos_com_fila, 3);
    rodar("portas fecham quando o vagão parte",        t_portas_fechadas, 3);
    rodar("embarque simultâneo (requisito 5)",         t_embarque_simultaneo, 5);
    rodar("mais passageiros que assentos",             t_mais_passageiros_que_assentos, 5);
    rodar("menos passageiros que assentos",            t_menos_passageiros_que_assentos, 3);
    rodar("passageiro chega durante o embarque",       t_chega_durante_embarque, 3);
    rodar("acordar espúrio (while, não if)",           t_acordar_espurio, 5);
    rodar("sem busy-wait",                             t_sem_busy_wait, 5);
    rodar("estações independentes",                    t_estacoes_independentes, 3);
    rodar("estresse",                                  t_estresse, 30);

    printf("\n%d de %d testes passaram\n", total_testes - total_falhas, total_testes);
    return total_falhas == 0 ? 0 : 1;
}
