#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <pthread.h>
#include <unistd.h>

#define HYDROGEN_PRODUCERS 10
#define OXYGEN_PRODUCERS 10

pthread_cond_t condition[4];
pthread_mutex_t mutex[3];

int required[2], count;

#define HYDROGEN 0
#define OXYGEN 1
#define FINISHED 2
#define READY 3

void wait(int c, int m) { pthread_cond_wait(&condition[c], &mutex[m]); }

#define WAIT(c) wait(c, c)

void lock(int m) { pthread_mutex_lock(&mutex[m]); }

void unlock(int m) { pthread_mutex_unlock(&mutex[m]); }

void sendSignal(int c) { pthread_cond_signal(&condition[c]); }

void broadcastSignal(int c) { pthread_cond_broadcast(&condition[c]); }

void errorExit(const char *message, ...) {
    va_list args;
    va_start(args, message);
    fprintf(stderr, message, args);
    va_end(args);
    exit(1);
}

unsigned mySeed;

struct producer { int atom, ID; };

const char symbol[2] = {'H', 'O'};

#define PRODUCER_TAG "[%c#%03d]"
#define CONSUMER_TAG "<~H2O~>"

void *produceAtoms(void *arg) {
    struct producer *this = arg;
    while (1) {
        printf(PRODUCER_TAG" is producing a new atom...\n", symbol[this->atom], this->ID);
        sleep(rand_r(&mySeed) % 5 + 5); /* Producing an atom takes from 5 to 9 seconds. */
        printf(PRODUCER_TAG" produced a new atom!\n", symbol[this->atom], this->ID);

        lock(this->atom);
        /* If required atoms have been supplied already, producer waits until it receives
         * a signal indicating that new atoms are required. */
        while (!required[this->atom]) WAIT(this->atom);
        printf(PRODUCER_TAG" will deliver atom #%d\n", symbol[this->atom], this->ID, required[this->atom]--);
        /* Read current `count` value before sending the signal to ensure that producer is up-to-date.
         * Reading the `count` value before unlocking `this->atom` ensures exclusive access to the variable. */
        int currentCount = count;
        /* Check whether this atom was the last one required. If so, send the signal that
         * the required amount of atoms has been delivered. Checking for the `not required`
         * condition is useful proportionally to the amount of required atoms because
         * it prevents unnecessary wake-up calls to consumer thread (e.g if the program
         * needed to be modified to produce sugar molecules - C6H12O6 - this condition
         * prevents hydrogen producers from waking up the consumer 11 times for no reason) */
        if (!required[this->atom]) sendSignal(READY);
        unlock(this->atom);

        lock(FINISHED);
        /* Waits until producing the molecule has been finished. */
        while (currentCount == count) WAIT(FINISHED);
        unlock(FINISHED);
    }
}

struct producers { int atom, producers; };

pthread_attr_t attributes;

void *initializeProducers(void *arg) {
    struct producers *info = arg;
    /* Initializing corresponding mutex and condition variable. */
    pthread_mutex_init(&mutex[info->atom], NULL);
    pthread_cond_init(&condition[info->atom], NULL);
    /* Information about producer threads can be ignored because they are supposed to
     * run in an endless loop - therefore cannot be joined. */
    pthread_t ignored;
    for (int p = 0; p < info->producers; ++p) {
        struct producer *this = malloc(sizeof(struct producer));
        this->atom = info->atom;
        this->ID = p;
        if (pthread_create(&ignored, &attributes, produceAtoms, this))
            errorExit(PRODUCER_TAG" - creating producer failed!\n", symbol[info->atom], p);
    }
    pthread_exit(NULL);
}

void startProducers() {
    mySeed = time(NULL);

    /* Producer threads will be created in a detached state. */
    pthread_attr_init(&attributes);
    pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);

    /* Initializing some of the mutexes and condition variables.
     * The rest will be initialized by corresponding `initializeProducers` routines */
    pthread_mutex_init(&mutex[FINISHED], NULL);
    pthread_cond_init(&condition[FINISHED], NULL);
    pthread_cond_init(&condition[READY], NULL);

    pthread_t hydrogenProducersInitializer;
    struct producers hydrogen = {HYDROGEN, HYDROGEN_PRODUCERS};
    if (pthread_create(&hydrogenProducersInitializer, NULL, initializeProducers, &hydrogen))
        errorExit("Creating hydrogen producers failed\n");

    pthread_t oxygenProducersInitializer;
    struct producers oxygen = {OXYGEN, OXYGEN_PRODUCERS};
    if (pthread_create(&oxygenProducersInitializer, NULL, initializeProducers, &oxygen))
        errorExit("Creating oxygen producers failed\n");

    pthread_join(hydrogenProducersInitializer, NULL);
    pthread_join(oxygenProducersInitializer, NULL);
}

void gather(int atom) {
    lock(atom);
    /* Waiting until the required amount of atoms is reduced to 0. */
    while (required[atom]) wait(READY, atom);
    unlock(atom);
}

void require(int atom, int amount) {
    lock(atom);
    required[atom] = amount;
    /* Sending signal to `amount` number of producers instead of broadcasting it
     * prevents the rest of the producers from waking up unnecessarily.
     * Useful if the number of producers is much greater than the required `amount`. */
    while (amount--) sendSignal(atom);
    unlock(atom);
}

int main() {
    startProducers();
    while (1) {
        printf(CONSUMER_TAG" Creating new molecule...\n");

        require(HYDROGEN, 2);
        require(OXYGEN, 1);

        gather(HYDROGEN);
        gather(OXYGEN);

        lock(FINISHED);
        printf(CONSUMER_TAG" Water created! Molecules created so far: %d\n", ++count);
        /* Wakes up any producer thread that delivered an atom for this molecule. */
        broadcastSignal(FINISHED);
        unlock(FINISHED);
    }
}
