#include <time.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>        /* For mode constants */
#include <fcntl.h>           /* For O_* constants */

#include "err.h"


#define NAP_TIME 2            //sleep
#define BUFF_SIZE 1          //bufor

#define LICZBA_PRODUCENTOW    2
#define LICZBA_KONSUMENTOW    3
#define LICZBA_PRODUKTOW      6

#define LICZBA_SEMAFOROW      3

struct produkt {
    pid_t producent;
    int czas_konsumpcji;
};

typedef struct produkt produkt;

void print_table(produkt *prod, int len) {

    int i;

    printf("Proces %d, tablica pod adresem %p:\n", getpid(), prod);
    for (i = 0; i < len; ++i)
        printf("|(%d, %d)", prod[i].czas_konsumpcji, prod[i].producent);
    printf("|\n\n");

    return;
}


void producent(sem_t *mut, sem_t *sprod, sem_t *skons, produkt *produkty, int *ile_w_buforze);

void konsument(sem_t *mut, sem_t *sprod, sem_t *skons, produkt *produkty, int *ile_w_buforze);


int main() {

    char buff[BUFF_SIZE] = "Ala ma kota";
    void *mapped_mem_all;
    produkt *produkty;
    int fd_memory = -1; /* deskryptor dla pamięci*/
    int flags, prot;
    volatile sem_t *mut;
    volatile sem_t *sprod;
    volatile sem_t *skons;
    volatile int *ile_w_buforze;
    pid_t pid;

    printf("Wielkość strony to %lu\n", sysconf(_SC_PAGE_SIZE));

    prot = PROT_READ | PROT_WRITE;
    flags = MAP_SHARED | MAP_ANONYMOUS; // nie ma pliku, fd winno być -1

    mapped_mem_all = mmap(NULL,
                          LICZBA_SEMAFOROW * sizeof(sem_t) + sizeof(int) + BUFF_SIZE * sizeof(produkt),
                          prot,
                          flags,
                          fd_memory,
                          0);

    if (mapped_mem_all == MAP_FAILED)
        syserr("mmap");

    //podział pamięci na semafor i bufor
    mut = (sem_t *) mapped_mem_all;
    sprod = (sem_t *) (mapped_mem_all + sizeof(sem_t));
    skons = (sem_t *) (mapped_mem_all + 2 * sizeof(sem_t));

    ile_w_buforze = (int *) (mapped_mem_all + LICZBA_SEMAFOROW * sizeof(sem_t));

    produkty = (produkt *) (mapped_mem_all + sizeof(int) + LICZBA_SEMAFOROW * sizeof(sem_t));

    if (sem_init(mut, 1, 1)) //todo a co jesli pshared==1 zamiast 0
        syserr("sem_init");
    if (sem_init(sprod, 1, BUFF_SIZE)) //todo a co jesli pshared==1 zamiast 0
        syserr("sem_init");
    if (sem_init(skons, 1, 0)) //todo a co jesli pshared==1 zamiast 0
        syserr("sem_init");

    for (int i = 0; i < LICZBA_PRODUCENTOW + LICZBA_KONSUMENTOW; ++i) {
        switch (pid = fork()) {
            case -1:
                syserr("fork");
            case 0:
                sleep(NAP_TIME);
                if (i < LICZBA_PRODUCENTOW) {
                    for (int j = 0; j < LICZBA_PRODUKTOW / LICZBA_PRODUCENTOW; ++j) {
                        producent(mut, sprod, skons, produkty, ile_w_buforze);
                    }
                } else {
                    for (int k = 0; k < LICZBA_PRODUKTOW / LICZBA_KONSUMENTOW; ++k) {
                        konsument(mut, sprod, skons, produkty, ile_w_buforze);
                    }
                }
                return 0;
            default:
                if (i < LICZBA_PRODUCENTOW) {
//                    printf("Pid bufora %d, pid producenta: %d\n", getpid(), pid);
                } else {
//                    printf("Pid bufora %d, pid konsumenta: %d\n", getpid(), pid);
                }
                break;
        }
    }

    //wymuszenie zasnięcia na semaforze
//    sleep(5);

    //sprzątanie
    for (int i = 0; i < LICZBA_PRODUCENTOW + LICZBA_KONSUMENTOW; ++i) {
        wait(0);
    }
    sem_destroy(mut);
    sem_destroy(sprod);
    sem_destroy(skons);
    munmap(mapped_mem_all, LICZBA_SEMAFOROW * sizeof(sem_t) + sizeof(int) +
                           BUFF_SIZE * sizeof(produkt)); // i tak zniknie, kiedy proces zginie

    return 0;
}

void konsument(sem_t *mut, sem_t *sprod, sem_t *skons, produkt *produkty, int *ile_w_buforze) {
    /// Pamiec na pobrany produkt.
    produkt *prod = malloc(sizeof(produkt));

    /// Semafory.
    if (sem_wait(skons))
        syserr("sem_wait");
    if (sem_wait(mut))
        syserr("mut_wait");

    /// Wyswietlam zawartosc bufora.
//    print_table(produkty, BUFF_SIZE);

//    printf("dupa");

    /// Kopiuje element.
//    printf("Proces %d, Zabieram z bufora\n", getpid());
    memcpy(prod, &produkty[*ile_w_buforze - 1], sizeof(produkt));

    /// Zeruje wartosci.
    produkty[*ile_w_buforze - 1].producent = 0;
    produkty[*ile_w_buforze - 1].czas_konsumpcji = 0;

    /// Zmniejszam ilosc
    --*ile_w_buforze;
//    print_table(produkty, BUFF_SIZE);

    /// Oddaje semafory
    sem_post(sprod);
    sem_post(mut);

    /// Swoje sprawy.
//    printf("Konsumuje produkt (%d, %d)", prod->czas_konsumpcji, prod->producent);
    printf("Ja, %d, otrzymałam/em (%d, %d)\n", getpid(), prod->czas_konsumpcji, prod->producent);

    if (sleep(prod->czas_konsumpcji)) {
        syserr("sleep interrupted");
    }

    /// Dealokacja zasobow.
    free(prod);
}

void producent(sem_t *mut, sem_t *sprod, sem_t *skons, produkt *produkty, int *ile_w_buforze) {
    int seed;
    time_t tt;
    seed = (int) time(&tt);
    srand(seed);

    produkt *prod = malloc(sizeof(produkt));
    prod->producent = getpid();
    prod->czas_konsumpcji = 1 + rand() % 4;


    if (sem_wait(sprod))
        syserr("sem_wait");
    if (sem_wait(mut))
        syserr("mut_wait");

    printf("Ja, %d, stworzyłam/em (%d, %d)\n", getpid(), prod->czas_konsumpcji, getpid());

//    print_table(produkty, BUFF_SIZE);

//    printf("Proces %d, Odkładam produkt do bufora.\n", getpid());
    memcpy(produkty + *ile_w_buforze, prod, sizeof(produkt));

//    memcpy(&produkty[*ile_w_buforze], &prod, sizeof(produkt));
    ++*ile_w_buforze;

//    print_table(produkty, BUFF_SIZE);

    if (sem_post(skons))
        syserr("sem_post_skons");
    if (sem_post(mut))
        syserr("sem_post_mut");

    free(prod);

//    sleep(NAP_TIME); //todo jak bylo bez tego to niektorzy konsumenci sie nie budzili
}
