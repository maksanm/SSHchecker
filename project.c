//Oswiadczam, ze niniejsza praca stanowiaca 
//podstawe do uznania osiagniecia efektow uczenia sie
//z przedmitu SOP1 zostala wykonana przeze mnie samodzielnie.
//Maksim Makaranka
//308826
#define _GNU_SOURCE
#include <stddef.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ftw.h>
#include <dirent.h>

#define MAXSIZE 1024
#define ERR(source) (perror(source),\
		     fprintf(stderr,"%s:%d\n",__FILE__,__LINE__),\
		     exit(EXIT_FAILURE))

enum type{C, MF, BZ2, GZ, XZ, ZIP, UNR};

typedef struct error_write //struktura dla watku zapisujacego logi bledow
{
    pthread_t tid;
    pthread_t* checker_tid;
    pthread_mutex_t* mxError;
    sigset_t *mask;
    char* error;
    char* path;
    char* ename;
} error_write_t;

typedef struct check //structura dla watku sprawdzajacego poprawnosc plikow zawartych w archiwum
{
    pthread_t tid;
    pthread_mutex_t* mxError;
    pthread_t* error_tid;
    sigset_t *mask;
    char* error;
    int* m;
    char* path;
    char* text;
} check_t;

typedef struct download //funkcja dla watku kopijujacego pliki z serwera
{
    pthread_t tid;
    pthread_t* error_tid;
    pthread_t* checker_tid;
    char* server;
    char* path;
} download_t;

void ReadArguments(int argc, char** argv, int* m, char* text, char* server, char* path, char* ename);
void* Errors(void* args);
void* Checks(void* args);
void* Downloads(void* args);
int filetype(char* name, int m);
int fileext(char *name, char *ext);
void checkosw(char* name, check_t* checker);
void error_message(char* name, char* msg, check_t* checker);
void dirins(char *name, check_t *checker);

void usage(char* name){
	fprintf(stderr, "USAGE:%s [-m] [-o text] [-s server] [-d path] [-b name]\n", name);
	exit(EXIT_FAILURE);
}

int main(int argc, char** argv)
{
    int m;
    char text[MAXSIZE];
    char server[MAXSIZE];
    char path[MAXSIZE];
    char ename[MAXSIZE];
    char error[MAXSIZE];
    double* subresult;
    strcpy(error, "");
    ReadArguments(argc, argv, &m, text, server, path, ename);
    sigset_t oldMask, newMask;
	sigemptyset(&newMask);
	sigaddset(&newMask, SIGUSR1);
    sigaddset(&newMask, SIGQUIT);
    pthread_mutex_t mxError = PTHREAD_MUTEX_INITIALIZER;
    download_t downloader;
    check_t checker;
    error_write_t error_writer;
    downloader.error_tid = &(error_writer.tid);
    downloader.checker_tid = &(checker.tid);
    downloader.server = server;
    downloader.path = path;
    checker.error_tid = &(error_writer.tid);
    checker.mxError = &mxError;
    checker.mask = &newMask;
    checker.m = &m;
    checker.error = error;
    checker.path = path;
    checker.text = text;
    error_writer.checker_tid = &checker.tid;
    error_writer.mxError = &mxError;
    error_writer.mask = &newMask;
    error_writer.error = error;
    error_writer.path = path;
    error_writer.ename = ename;
    char cwd[MAXSIZE];
    getcwd(cwd, MAXSIZE);
    chdir(path);
    if (pthread_sigmask(SIG_BLOCK, &newMask, &oldMask)) ERR("SIG_BLOCK error");
    if (pthread_create(&(error_writer.tid), NULL, Errors, &error_writer)) ERR("Couldn't create thread error_writer");
    if (pthread_create(&(checker.tid), NULL, Checks, &checker)) ERR("Couldn't create thread checker");
    if (pthread_create(&(downloader.tid), NULL, Downloads, &downloader)) ERR("Couldn't create thread downloader");
    if (pthread_join(downloader.tid, (void*)&subresult)) ERR("Couldn't join thread downloader");
    if (pthread_join(checker.tid, (void*)&subresult)) ERR("Couldn't join thread checker");
    if (pthread_join(error_writer.tid, (void*)&subresult)) ERR("Couldn't join thread error_writer");
    chdir(cwd);
    return EXIT_SUCCESS;
}

void ReadArguments(int argc, char** argv, int* m, char* text, char* server, char* path, char* ename) //funkcja pobierajaca argumenty z terminalu
{
    int mf = 0, of = 0, sf = 0, df = 0, bf = 0;
    if (argc > 10) ERR("Wrong arguments number");
    *m = 0;
    strcpy(text, "Oswiadczam");
    strcpy(server, "nowakj@ssh.mini.pw.edu.pl:/home2/samba/sobotkap/unix/");
    getcwd(path, MAXSIZE);
    strcpy(ename, "errors.log");
    char c;
    while ((c = getopt(argc, argv, "mo:s:d:b:")) != -1)
        switch(c)
        {
            case 'm': 
                if(!mf)
                {
                    *m = 1;
                    mf++;
                    break;
                }
                else usage(argv[0]);
            case 'o': 
                if(!of)
                {
                    strcpy(text, optarg);
                    of++;
                    break;
                }
                else usage(argv[0]);
            case 's':
                if(!sf)
                { 
                    strcpy(server, optarg);
                    sf++;
                    break;
                }
                else usage(argv[0]);
            case 'd': 
                if(!df)
                {
                    strcpy(path, optarg);
                    df++;
                    break;
                }
                else usage(argv[0]);
            case 'b': 
                if(!bf)
                {
                    strcpy(ename, optarg);
                    bf++;
                    break;
                }
                else usage(argv[0]);
            default : 
                printf("\n%c\n", c);
                usage(argv[0]);
        }
}

void* Errors(void* args) //funkcja watku zapisujacego logi bledow
{
    error_write_t* error_writer = args;
    int signo;
    int quit = 0, o;
    if (sigwait(error_writer->mask, &signo)) ERR("sigwait error");
    if (signo != SIGUSR1) ERR("Unexpected signal");
    if ((o = open(error_writer->ename, O_WRONLY | O_CREAT | O_APPEND, 0777)) < 0) ERR("open error");
    while(!quit)
    {
        pthread_kill(*error_writer->checker_tid, SIGUSR1);
        if(sigwait(error_writer->mask, &signo)) ERR("sigwait error");
		switch (signo) 
        {
            case SIGUSR1 :
                pthread_mutex_lock(error_writer->mxError);
                if (write(o, error_writer->error, strlen(error_writer->error)) <= 0) ERR("write error");
                strcpy(error_writer->error, "");
                pthread_mutex_unlock(error_writer->mxError);
                break;
            case SIGQUIT :
                pthread_mutex_lock(error_writer->mxError);
                if (strcmp(error_writer->error, "") != 0)
                    if (write(o, error_writer->error, strlen(error_writer->error)) <= 0) ERR("write error");
                pthread_mutex_unlock(error_writer->mxError);
                quit = 1;
                break;
            default : ERR("Unexpected error");
        }
    }
    return NULL;
}

void* Checks(void* args) //funkcja watku sprawdzajacego poprawnosc zawartosci archiwum
{
    check_t* checker = args;
    int signo;
    DIR *dirp;
    struct stat filestat;
    struct dirent *dp;
    if (sigwait(checker->mask, &signo)) ERR("sigwait error");
    if (signo != SIGUSR1) ERR("Unexpected signal");
    else pthread_kill(*checker->error_tid, SIGUSR1);
    if ((dirp = opendir(".")) == NULL) ERR("opendir error");
    do
    {
        errno = 0;
        if ((dp = readdir(dirp)) != NULL)
        {
            if (lstat(dp->d_name, &filestat)) ERR("lstat error");
            if (S_ISDIR(filestat.st_mode) && strcmp(dp->d_name, ".") && strcmp(dp->d_name, "..")) dirins(dp->d_name, checker);
        }
    } while (dp != NULL);
    if (errno != 0) ERR("readdir error");
    if (pthread_kill(*checker->error_tid,SIGQUIT)) ERR("pthread_kill error");
    if (closedir(dirp)) ERR("closedir error");
    return NULL;
}

void dirins(char *name, check_t *checker) //funkcja sprawdzajaca poprawnosc plikow w katalogie
{
    int c = 0, m = 0, e = 0;
    DIR *dirp;
    struct stat filestat;
    struct dirent *dp;
    if ((dirp = opendir(name)) == NULL) ERR("opendir error");
    do
    {
        errno = 0;
        if ((dp = readdir(dirp)) != NULL)
        {
            if (lstat(dp->d_name, &filestat)) ERR("lstat error");
            if (S_ISREG(filestat.st_mode))
            {
                switch (filetype(dp->d_name, *checker->m))
                {
                    case C:
                        checkosw(dp->d_name, checker);
                        c++;
                        e++;
                        break;
                    case MF:
                        m++;
                        e++;
                        break;
                    case BZ2:
                        break;
                    case GZ:
                        break;
                    case XZ:
                        break;
                    case ZIP:
                        break;
                    case UNR:
                        error_message(dp->d_name, "Niespodziewany rodzaj pliku", checker);
                        e++;
                        break;
                    default: ERR("Unexpected error");
                }
            }
        }
    } while (dp != NULL);
    if (errno != 0) ERR("readdir error");
    if (closedir(dirp)) ERR("closedir error");
    if (e == 0) 
    {
        error_message("", "Katalog jest pusty", checker);
        return;
    }
    if (c == 0) error_message("", "Brak pliku .c", checker);
    if (m == 0 &&*checker->m) error_message("", "Nie znaleziono makefile", checker);
    if (m > 1 && *checker->m) error_message("", "Wiecej niz jeden makefile", checker);
    }

void* Downloads(void* args) //funkcja watku kopiujacego pliki z serwera
{
    download_t* downloader = args;
    char command[MAXSIZE] = "scp ";
    strcat(command, downloader->server);
    strcat(command, " ");
    strcat(command, downloader->path);
    if(system(command) != 0) ERR("system error");
    DIR *dirp;
	struct dirent *dp;
    struct stat filestat;
    char* dir;
    if((dirp = opendir(downloader->path)) == NULL) ERR("opendir error");
    do
    {
        errno = 0;
		if ((dp = readdir(dirp)) != NULL) 
        {
            if (lstat(dp->d_name, &filestat)) ERR("lstat error");
            if (S_ISREG(filestat.st_mode))
            {
                switch (filetype(dp->d_name, 0)) 
                {
                    case BZ2:
                        if ((dir = (char*)malloc(strlen(dp->d_name) - strlen(".tar.bz2") + 1)) == NULL ) ERR("Memory not allocated");
                        strncpy(dir, dp->d_name, strlen(dp->d_name) - strlen(".tar.bz2"));
                        dir[strlen(dp->d_name) - strlen(".tar.bz2")] = '\0';
                        if (lstat(dir, &filestat) == -1)
                        mkdir(dir, 0777);
                        strcpy(command, "tar -xjf ");
                        strcat(command, dp->d_name);
                        strcat(command, " -C ");
                        strcat(command, dir);
                        if(system(command) != 0) ERR("system error");
                        break;
                    case GZ:
                        if ((dir = (char*)malloc(strlen(dp->d_name) - strlen(".tar.gz") + 1)) == NULL ) ERR("Memory not allocated");
                        strncpy(dir, dp->d_name, strlen(dp->d_name) - strlen(".tar.gz"));
                        dir[strlen(dp->d_name) - strlen(".tar.gz")] = '\0';
                        if (lstat(dir, &filestat) == -1)
                        mkdir(dir, 0777);
                        strcpy(command, "tar -xzf ");
                        strcat(command, dp->d_name);
                        strcat(command, " -C ");
                        strcat(command, dir);
                        if(system(command) != 0) ERR("system error");
                        break;
                    case XZ:
                        if ((dir = (char*)malloc(strlen(dp->d_name) - strlen(".tar.xz") + 1)) == NULL ) ERR("Memory not allocated");
                        strncpy(dir, dp->d_name, strlen(dp->d_name) - strlen(".tar.xz"));
                        dir[strlen(dp->d_name) - strlen(".tar.xz")] = '\0';
                        if (lstat(dir, &filestat) == -1)
                        mkdir(dir, 0777);
                        strcpy(command, "tar -xJf ");
                        strcat(command, dp->d_name);
                        strcat(command, " -C ");
                        strcat(command, dir);
                        if(system(command) != 0) ERR("system error");
                        break;
                    case ZIP:
                        if ((dir = (char*)malloc(strlen(dp->d_name) - strlen(".zip")) + 1) == NULL ) ERR("Memory not allocated");
                        strncpy(dir, dp->d_name, strlen(dp->d_name) - strlen(".zip"));
                        dir[strlen(dp->d_name) - strlen(".zip")] = '\0';
                        if (lstat(dir, &filestat) == -1)
                        mkdir(dir, 0777);
                        strcpy(command, "unzip ");
                        strcat(command, dp->d_name);
                        strcat(command, " -C ");
                        strcat(command, dir);
                        if(system(command) != 0) ERR("system error");
                        break;
                }
          
            }
        }
    }while (dp != NULL);
    free(dir);
    if(closedir(dirp)) ERR("closedir error");
    pthread_kill(*downloader->checker_tid, SIGUSR1);
    return NULL;
}

int filetype(char* name, int m) //funkcja zwracajaca typ pliku
{
    if (fileext(name, ".c") == 1) return C;
    if (m && (strcmp(name, "Makefile") == 0 || strcmp(name, "makefile") == 0)) return MF;
    if (fileext(name, ".tar.bz2")) return BZ2;
    if (fileext(name, ".tar.gz")) return GZ;
    if (fileext(name, ".tar.xz")) return XZ;
    if (fileext(name, ".zip")) return ZIP;
    return UNR;
}

int fileext(char* name, char* ext) //funkcja sprawdzajaca czy plik o nazwie name posiada rozszerszenie ext
{
    size_t l = strlen(name);
    size_t extl = strlen(ext);
    if(strcmp(name + l - extl, ext) == 0 && l > extl) return 1;
    return 0;
}

void checkosw(char* name, check_t* checker) //funkcja sprawdzajaca czy plik zrodlowy posiada oswiadczenie
{
    char s[MAXSIZE];
    int i, c, osw = 0;
    if ((i = open(name, O_RDONLY)) < 0) ERR("open error");
    while((c = read(i, s, MAXSIZE)) > 0)
    {
        if (c < 0) ERR("read error");
        if (strstr(s, checker->text))
        {
            osw++;
            break;
        }
    }
    if (!osw) error_message(name, "Oswiadczenie nie jest poprawne", checker);
    close(i);
}

void error_message(char* name, char* msg, check_t* checker) //funkcja tworzaca zawartosc logu
{
    char log[MAXSIZE];
    char buf[MAXSIZE];
    int signo;
    time_t real;
    struct tm* local;
    time(&real);
    local = localtime(&real);
    if(local->tm_mday < 10)
        strcpy(log, "[0");
    else
        strcpy(log, "[");
    sprintf(buf, "%d", local->tm_mday);
    strcat(log, buf);
    if(local->tm_mon < 10)
        strcat(log, ".0");
    else
        strcat(log, ".");
    sprintf(buf, "%d", local->tm_mon);
    strcat(log, buf);
    strcat(log, ".");
    sprintf(buf, "%d", local->tm_year + 1900);
    strcat(log, buf);
    if(local->tm_hour < 10)
        strcat(log, "_0");
    else
        strcat(log, "_");
    sprintf(buf, "%d", local->tm_hour);
    strcat(log, buf);
    if(local->tm_min < 10)
        strcat(log, ":0");
    else
        strcat(log, ":");
    sprintf(buf, "%d", local->tm_min);
    strcat(log, buf);
    strcat(log, "]");
    strcat(log, " (");
    strcat(log, name);
    strcat(log, ") ");
    strcat(log, msg);
    strcat(log, "\n");
    if(sigwait(checker->mask, &signo)) ERR("sigwait error");
    if(signo != SIGUSR1) ERR("Unexpected signal");
    pthread_mutex_lock(checker->mxError);
    strcpy(checker->error, log);
    pthread_mutex_unlock(checker->mxError);
    pthread_kill(*checker->error_tid, SIGUSR1);
}