/*
 * file: philosophers.c
 * IOS projekt 2 varianta B, 4/2010
 * Author: Jan Dusek <xdusek17@stud.fit.vutbr.cz>
 * implementace problemu vecericich filosofu
 * synchronizace pomoci front zprav
 */

// library includes
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>
#include <stdbool.h>

// system includes
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <signal.h>
#include <unistd.h>

#define NUM_PHILOSOPHERS 5

#define MSG_SIZE 1
struct my_msgbuf
{
    long int mtype;		            // typ zpravy
    char mtext[MSG_SIZE];		    // text zpravy
};

/// struktura sdilenych zdroju
typedef struct _SHARED_RES_
{
    FILE* pLogFile;                 /// soubor pro logovani zprav filosofu
    int SharedSegID;                /// ID sdilene pameti pro citat akci
    int FileMessQueueID;            /// fronta zprav pro synchornizaci pristupu k pLogFile
    int ChopsMessQueueID;           /// fronta zprav pro synchronizaci pristupu k vidlickam
} SharedRes;

volatile sig_atomic_t g_TermFlag = 0;/// globalni flag inidikujici zda ma proces skoncit

void OnTerminate(int sigNum)        /// signal handler pro SIGTERM a SIGHUP
{
    /// V zadani je ze mame pouzivat -Werror takze samozrejme to nejde skompilovat bez tohoto radku >:(
    /// handler signalu musi brat parametr int ktery je cislo signalu ale v tomto handleru
    /// me cislo nezajima to urcuji sigaction()em kterym signalum tento handler priradim
    sigNum = sigNum;

    g_TermFlag = 1;                 /// nastavi flag na 1. proces vyskoci z hlavni smycky, uklidi, a skonci
}

/**
 * Loguje formatovanou zpravu do sdileneho souboru. Synchronizuje pristup pomoci fronty zprav
 * prints: "numOfAction: fmt"
 * mq musi byt inicializovana 1 zpravou struct my_msgbuf u ktere nezalezi na mtype
 * @param pFile otevreny soubor pro zapis
 * @param pCounter citac akci. volani zvysi citac o 1
 * @param ID fronty zprav ktera bude pouzita pro synchroniza pristupu k souboru
 * @param fmt format jako printf()
 */
void LogToSharedFile(FILE *pFile, int *pCounter, int mqID, char *fmt, ...)
{
    // pomocne zpravy
    struct my_msgbuf recMsg;
    struct my_msgbuf sndMsg = { 1, "" };

    if (fmt == NULL)                        // pokud neni co tisknout
        return;                             // koncime

    msgrcv(mqID, &recMsg, MSG_SIZE, 0, 0);  // lockneme kritickou sekci

    fprintf(pFile, "%d: ", (*pCounter)++);  // tiskneme prefix zpravy. note: pCounter je take sdilene takze musi byt take v kriticke sekci

    // pro vypis pouzijeme vfprintf
    va_list ap;
    va_start(ap, fmt);
    vfprintf(pFile, fmt, ap);
    va_end(ap);

    fflush(pFile);                          // flushneme buffer souboru
    msgsnd(mqID, &sndMsg, MSG_SIZE, 0);     // unlock kriticke sekce
}

/**
 * Zamkne vidlicku. pouzije se fronta zprav.
 * implementace: ziska zpravu typu cislo vidlicky
 * @param mqID ID fronty zprav ktera se pouzije pro synchronizaci
 * @param iChop cislo vidlicky
 */
void LockChopstick(int mqID, int iChop)
{
    // pomocna zprava
    struct my_msgbuf recMsg;

    // behem cekani na zpravu muze proces dostat signal coz zpusobi ze volani vrati error
    // errno bude EINTR. teto budeme ignorovat a budeme cekat dal
    while (msgrcv(mqID, &recMsg, MSG_SIZE, iChop, 0) == -1 && errno == EINTR);
}

/**
 * Odemkne vidlicku. pouzije se fronta zprav
 * implementace: posle do fronty zpravu s mtype iChop
 * @param mqID ID fronty zprav ktera se pouzije pro synchronizaci
 * @param iChop cislo vidlicky
 */
void UnlockChopstick(int mqID, int iChop)
{
    struct my_msgbuf sndMsg = { iChop, "" };

    msgsnd(mqID, &sndMsg, MSG_SIZE, 0);
}

/**
 * Hlavni funkce filosofa
 * Filosof premysli a ji
 * pote co se dinnerCount krat naji skonci
 * Vypisuje svoje akce do souboru
 * @param id filosofa
 * @param dinnerCount pocet veceri co bude mit nez skonci
 * @param pResources sdilene zdroje
 * @return return code 0 == SUCCESS
 */
int PhilosopherMain(int id, int dinnerCount, const SharedRes *pResources)
{
    // pripojime sdilenou pamet do naseho adresoveho prostoru
    int *pCounter = (int*)shmat(pResources->SharedSegID, NULL, 0);

    // podle toho zda filosofovo ID je sude ci liche bude nejdrive brat levou respektive pravou vidlicku
    int firstChop = id % 2 ? id : id % NUM_PHILOSOPHERS + 1;
    int secondChop = id % 2 ? id % NUM_PHILOSOPHERS + 1 : id;

    // dinnerCount krat se naji ci nam byl zaslan ukoncujici signal
    for (int i = 0; i < dinnerCount && !g_TermFlag; i++)
    {
        // thinking
        LogToSharedFile(pResources->pLogFile, pCounter, pResources->FileMessQueueID, "philosopher %d: becomes thinking\n", id);
        usleep(rand() % 100000);                    // sleep 0-100ms

        // --------------------------------------------------------
        // ----------------------- EATING -------------------------
        // --------------------------------------------------------

        // lock first chopstick
        LockChopstick(pResources->ChopsMessQueueID, firstChop);
        LogToSharedFile(pResources->pLogFile, pCounter, pResources->FileMessQueueID, "philosopher %d: picks up a fork %d\n", id, firstChop);
        // lock second chopstick
        LockChopstick(pResources->ChopsMessQueueID, secondChop);
        LogToSharedFile(pResources->pLogFile, pCounter, pResources->FileMessQueueID, "philosopher %d: picks up a fork %d\n", id, secondChop);

        // log to file
        LogToSharedFile(pResources->pLogFile, pCounter, pResources->FileMessQueueID, "philosopher %d: becomes eating\n", id);

        usleep(rand() % 100000);                    // sleep 0-100ms

        // unlock first chopstick
        LogToSharedFile(pResources->pLogFile, pCounter, pResources->FileMessQueueID, "philosopher %d: releases a fork %d\n", id, firstChop);
        UnlockChopstick(pResources->ChopsMessQueueID, firstChop);
        // unlock second chopstick
        LogToSharedFile(pResources->pLogFile, pCounter, pResources->FileMessQueueID, "philosopher %d: releases a fork %d\n", id, secondChop);
        UnlockChopstick(pResources->ChopsMessQueueID, secondChop);
    }

    // oddelame sdilenou pamet z naseho adresoveho prostoru
    shmdt(pCounter);

    // od rodice dite dostalo popisovac souboru a ten musime zavrit
    fclose(pResources->pLogFile);

    return EXIT_SUCCESS;
}

/**
 * Inicializujeme sdilene zdroje
 * pri nejake chybe tiskne chybova hlaseni na stderr pomoci perror()
 * @param pathForKey jmeno souboru pro ftok() pro vytvareni IPC klicu pro sdil.zdroje
 * @retval pResources struktura s ID sdilenych zdroju
 * @return zda bylo uspesne ci nikolive
 */
bool InitializeSharedResources(const char *pathForKey, SharedRes *pResources)
{
    // vytvori IPC klic pro sdilenou pamet
    key_t shmKey = ftok(pathForKey, 'm');
    // vytvori IPC klic pro frontu synchronizujici pristup k logovacimu souboru
    key_t mqFileKey = ftok(pathForKey, 'f');
    // vytvori IPC klic pro frontu pro vidlicky
    key_t mqChopKey = ftok(pathForKey, 'c');
    if (shmKey == -1 || mqFileKey == -1 || mqChopKey == -1) // pokud nejaky klic selhal
    {
        perror("ftok");
        return false;
    }

    // vytvorime sdileny segment pameti
    pResources->SharedSegID = shmget(shmKey, sizeof(int), IPC_CREAT | IPC_EXCL | 0666);
    if (pResources->SharedSegID == -1)
    {
        perror("shmget");
        return false;
    }

    // inicializujeme sdilenou pamet na 1
    int *pShrMem = (int*)shmat(pResources->SharedSegID, NULL, 0);
    *pShrMem = 1;
    shmdt(pShrMem);

    // vytvorime frontu zprav synchronizujici pristup k souboru
    pResources->FileMessQueueID = msgget(mqFileKey, IPC_CREAT | IPC_EXCL | 0666);
    if (pResources->FileMessQueueID == -1)
    {
        shmctl(pResources->SharedSegID, IPC_RMID, NULL);// odstranime sdileny segment
        perror("msgget");
        return false;
    }
    // pokud je ve fronte zpravu budeme povazovat zdroj za volny
    struct my_msgbuf msg = {1, ""};
    msgsnd(pResources->FileMessQueueID, &msg, MSG_SIZE, 0);

    // vytvorime frontu zprav pro vidlicky
    pResources->ChopsMessQueueID = msgget(mqChopKey, IPC_CREAT | IPC_EXCL | 0666);
    if (pResources->ChopsMessQueueID == -1)
    {
        shmctl(pResources->SharedSegID, IPC_RMID, NULL);    // odstranime sdileny segment
        msgctl(pResources->FileMessQueueID, IPC_RMID, NULL);// odstanime frontu zprav
        perror("msgget");
        return false;
    }
    // kazdy typ zpravu reprezentuje vidlicku takze pro 5 filosofu 5 typu zprav
    for (int i = 1; i <= NUM_PHILOSOPHERS; i++)
    {
        struct my_msgbuf msg = { i, "" };
        msgsnd(pResources->ChopsMessQueueID, &msg, MSG_SIZE, 0);
    }

    // otevreme soubor pro zapis akci filosofu
    pResources->pLogFile = fopen("philosophers.out", "w");
    if (pResources->pLogFile == NULL)
    {
        shmctl(pResources->SharedSegID, IPC_RMID, NULL);        // odstranime sdileny segment
        msgctl(pResources->FileMessQueueID, IPC_RMID, NULL);    // odstanime frontu zprav
        msgctl(pResources->ChopsMessQueueID, IPC_RMID, NULL);   // odstanime frontu zprav
        perror("fopen");
        return false;
    }

    return true;
}

/**
 * Deinicializuje sdilene zdroje
 * @param pResources zdroje k deinicializaci
 */
void DeinitializeSharedResources(const SharedRes *pResources)
{
    shmctl(pResources->SharedSegID, IPC_RMID, NULL);        // odstranime sdileny segment
    msgctl(pResources->FileMessQueueID, IPC_RMID, NULL);    // odstanime frontu zprav
    msgctl(pResources->ChopsMessQueueID, IPC_RMID, NULL);   // odstanime frontu zprav
    fclose(pResources->pLogFile);                           // zavreme soubor (pozor je potreba zavrit i v detech)
}

/**
 * ziska pocet pocet jidel filosofu z argumentu programu
 * pozadovane hodnoty jsou 1..INT_MAX
 * @param argc argument count
 * @param argv argument vector
 * @return pocet jidel ci -1 pri chybe
 */
int GetDinnerCount(int argc, char *argv[])
{
    int ret = 0;                                // return value
    if (argc == 2)                              // program vyzaduje 1 argument
    {
        char* rest;
        errno = 0;
        ret = strtol(argv[1], &rest, 10);
        // pokud jsme neprekonvertovali cely argument ci bylo preteceni ci podteceni
        if (*rest != '\0' || errno == ERANGE || ret <= 0)
            return -1;
    }
    else
        return -1;

    return ret;
}

int main(int argc, char *argv[])
{
    // nastavime seed pro pseudorandom generator
    srand(time(NULL));

    // ziskame pocet jidel z argumentu programu
    int dinnerCount = GetDinnerCount(argc, argv);
    if (dinnerCount == -1)
    {
        fprintf(stderr, "Dinner count out of range only values between 1..INT_MAX are valid\n");
        return EXIT_FAILURE;
    }

    // nastavime signal handler OnTerminate na SIGTERM a SIGHUP
    // deti zdedi tento handler
    // tento handler je potreba od volani InitializeSharedResources()
    struct sigaction action;
    action.sa_handler = OnTerminate;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    sigaction(SIGINT, &action, NULL);
    sigaction(SIGHUP, &action, NULL);
    sigaction(SIGTERM, &action, NULL);

    // inicializujeme sdilene zdroje pouzijeme argv[0] jako pathname pro ftok()
    SharedRes resources;
    if (!InitializeSharedResources(argv[0], &resources))
        return EXIT_FAILURE;

    // vytvorime N filosofu jako deti
    for (int i = 1; i <= NUM_PHILOSOPHERS; i++)
    {
        pid_t pID = fork();             // syscall fork()
        if (pID == 0)                   // dite
        {
            // zavolame filosofovu hlavni fci
            return PhilosopherMain(i, dinnerCount, &resources);// vratime zde takze dite se nedostane za tento if
        }
        else if (pID < 0)               // chyba forku
        {
            kill(0, SIGTERM);           // posleme vsem detem signal ze hra skoncila
            DeinitializeSharedResources(&resources);
            perror("fork");
            return EXIT_FAILURE;
        }
        // else {}                      // zadny kod rodice pro kazdy fork()
    }

    // diky return na konci kodu specifickeho pro dite je toto jen kod rodice

    // rodic ceka dokud si vsechny deti neprestanou hrat
    int nChildsTerminated = 0;
    while (nChildsTerminated < NUM_PHILOSOPHERS)
    {
        if (g_TermFlag)                 // pokud rodic dostal signal pro skonceni
            kill(0,SIGTERM);            // posleme detem signal ze koncime

        int childExitStatus;
        // cekame dokud nejake dite neskonci
        pid_t childPID = wait(&childExitStatus);
        if (childPID == -1)
        {
            // pokud wait() skoncilo na necem jinem nez EINTR (ECHILD)
            if (errno != EINTR)
            {
                // uz nemame zadne deti takze muzeme bez okolku skoncit
                DeinitializeSharedResources(&resources);
                perror("wait");
                return EXIT_FAILURE;
            }
            // (else) pri EINTR pokracujeme
        }
        else if (WIFEXITED(childExitStatus))    // pokud dite skoncilo normalne
            nChildsTerminated++;
        else if (WIFSIGNALED(childExitStatus))  // pokud dite bylo zabito signalem napr. SIGKILL
        {
            nChildsTerminated++;
            kill(0, SIGTERM);                   // posleme detem signal ze koncime
        }
    }

    // final clean up
    DeinitializeSharedResources(&resources);

    return EXIT_SUCCESS;
}
