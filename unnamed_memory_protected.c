#include <time.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <sys/wait.h>
#include <stdlib.h>

#include "err.h"

#define NAP_TIME            2
#define BUFF_SIZE           1

#define LICZBA_PRODUCENTOW  2
#define LICZBA_KONSUMENTOW  3
#define LICZBA_PRODUKTOW    6
#define LICZBA_SEMAFOROW    3

struct produkt {
    pid_t producent;
    int czas_konsumpcji;
};

typedef struct produkt produkt;

void producent(sem_t *mut, sem_t *sprod, sem_t *skons, produkt *produkty, int *ile_w_buforze);

void konsument(sem_t *mut, sem_t *sprod, sem_t *skons, produkt *produkty, int *ile_w_buforze);

int main() {
    void *mapped_mem_all;
    produkt *produkty;
    int fd_memory = -1; /* deskryptor dla pamięci*/
    int flags, prot;
    sem_t *mut;
    sem_t *sprod;
    sem_t *skons;
    int *ile_w_buforze;

//    printf("Wielkość strony to %lu\n", sysconf(_SC_PAGE_SIZE));

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

    if (sem_init(mut, 1, 1))
        syserr("sem_init");
    if (sem_init(sprod, 1, BUFF_SIZE))
        syserr("sem_init");
    if (sem_init(skons, 1, 0))
        syserr("sem_init");

    for (int i = 0; i < LICZBA_PRODUCENTOW + LICZBA_KONSUMENTOW; ++i) {
        switch (fork()) {
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
                break;
        }
    }

    //sprzątanie
    for (int i = 0; i < LICZBA_PRODUCENTOW + LICZBA_KONSUMENTOW; ++i) {
        wait(0);
    }
    sem_destroy(mut);
    sem_destroy(sprod);
    sem_destroy(skons);
    munmap(mapped_mem_all, LICZBA_SEMAFOROW * sizeof(sem_t) + sizeof(int) + BUFF_SIZE * sizeof(produkt));

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

    /// Kopiuje element.
    memcpy(prod, &produkty[*ile_w_buforze - 1], sizeof(produkt));

    /// Zeruje wartosci.
    produkty[*ile_w_buforze - 1].producent = 0;
    produkty[*ile_w_buforze - 1].czas_konsumpcji = 0;

    /// Zmniejszam ilosc
    --*ile_w_buforze;

    /// Wypisuje komunikat (dlatego w sekcji krytycznej, aby komunikaty pojawialy sie sekwencyjnie,
    /// w kolejnosci chronologicznej, inaczej dwa procesy na raz chcą wypisywac - konsument oraz producent).
    printf("Ja, %d, otrzymałam/em (%d, %d)\n", getpid(), prod->czas_konsumpcji, prod->producent);
    fflush(stdout);

    /// Oddaje semafory
    sem_post(sprod);
    sem_post(mut);


    /// Swoje sprawy.
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
    fflush(stdout);

    memcpy(produkty + *ile_w_buforze, prod, sizeof(produkt));

    ++*ile_w_buforze;

    if (sem_post(skons))
        syserr("sem_post_skons");
    if (sem_post(mut))
        syserr("sem_post_mut");

    free(prod);
}
