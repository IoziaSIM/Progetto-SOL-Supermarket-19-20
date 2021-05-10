#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <errno.h>
#include <sys/socket.h> 
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include "./lib/structs.c"
#include "./lib/config.c"
#include "./lib/rwn.c"

#define UNIX_PATH_MAX 108   
#define SOCKNAME "./mysock"

static config* txt;
static int fd_skt = -1;
static int fd_c;
static pid_t superm_pid;
static pid_t dir_pid;
volatile sig_atomic_t sigquit = 0;
volatile sig_atomic_t chiusura = 0;

static void handler (int signum)
{

	if (signum == SIGHUP) 
	    	kill(superm_pid, SIGHUP);

	if (signum == SIGQUIT) {
		sigquit = 1;
		kill(superm_pid, SIGQUIT);
	}

	if(signum == SIGUSR1)
		kill(superm_pid, SIGUSR1);  

	if(signum == SIGUSR2)
		chiusura = 1;
}

void RunSocket ()
{
	pid_t* buf;
	struct sockaddr_un sock;
	
	strncpy(sock.sun_path, SOCKNAME, UNIX_PATH_MAX);
	sock.sun_family=AF_UNIX;
	
	if ((fd_skt = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {		
		fprintf(stderr,"Server: Impossibile creare la socket!\n");
        	exit(EXIT_FAILURE);
    	}  

   	if((bind(fd_skt, (struct sockaddr *)&sock, sizeof(sock))) == -1) {
    		fprintf(stderr,"Server: Errore bind!\n");
        	exit(EXIT_FAILURE);
    	}

    if((listen(fd_skt, SOMAXCONN)) == -1) {
    	fprintf(stderr,"Server: Errore listen!\n");
       	exit(EXIT_FAILURE);

    }

    if((fd_c = accept(fd_skt,NULL,0)) == -1) {
    	fprintf(stderr,"Server: Errore accept!\n");
       	exit(EXIT_FAILURE);
    }	

    /* SCAMBIO PID */
    buf = malloc(sizeof(pid_t));

    if((readn(fd_c, (pid_t*) buf, sizeof(pid_t))) == -1) {
    	fprintf(stderr,"PID Server: Read fallita!\n");
    	exit(EXIT_FAILURE);
    }

    superm_pid = buf[0];
    buf[0]= dir_pid;
	
	if((writen(fd_c, (pid_t*) buf, sizeof(pid_t))) == -1) {
		fprintf(stderr,"PID Server: Write fallita!\n");
       	exit(EXIT_FAILURE);
	}

	free(buf);

}


void* man_casse(void* arg) 
{
	int* data; 
	int* buf;
	int chiudi, none, num=1;
		
	while(sigquit == 0 && chiusura == 0 && num != 0){
	
		data = malloc(txt->K * sizeof(int));
		
		/*LETTURA DEL NUMERO DEI CLIENTI PRESENTI NELLE CODE DELLE CASSE */

		if((num = readn(fd_c, (int*) data, txt->K * sizeof(int))) < 0) {
			fprintf(stderr, "man_casse: Read fallita!\n");
			exit(EXIT_FAILURE);
		}

		if(sigquit == 0 && chiusura == 0 && num != 0) { 
	

			buf = calloc(2, sizeof(int));
			chiudi = 0;
			none = 0;

			for(int i=0; i<txt->K; i++) {

				if (data[i] == 0 || data[i] == 1) {
					chiudi ++;
					if (chiudi == txt->S1) {
						none = 1;
						buf[0] = 1;
						/* INVIO CODICE DI CHIUSURA CASSA */
						if((writen (fd_c, (int*) buf, 2*sizeof(int))) < 0) {
							fprintf(stderr,"man_casse: Write chiusura fallita!\n");
        					exit(EXIT_FAILURE);
						}
						break;
					}
				}
			
				else if (data[i] >= txt->S2) {
					none = 1;
					buf[1] = 1;
					/* INVIO CODICE DI APERTURA CASSA */
					if((writen (fd_c, (int*) buf, 2*sizeof(int))) < 0) {
						fprintf(stderr,"man_casse: Write apertura fallita!\n");
        				exit(EXIT_FAILURE);
					}
					break;
				}

			}

			/*INVIO CODICE DI NON FAR NULLA*/
			if(none == 0)
				if((writen (fd_c, (int*) buf, 2*sizeof(int))) < 0) {
					fprintf(stderr,"man_casse: Write nulla fallita!\n");
        			exit(EXIT_FAILURE);
        		}

        	free(buf);

		}

		free(data);
	}

	pthread_exit(NULL);
}


int main (int argc, const char* argv[]) 
{
	pthread_t managercasse;
	struct sigaction sa;
	pid_t pid;

	/* INIZIALIZZAZIONE */
	if (argc!=2) {
        	fprintf(stderr,"Usage: %s ./config1.txt or %s ./config2.txt\n", argv[0], argv[0]);
        	exit(EXIT_FAILURE);
    	}
    
    if ((strcmp(argv[1],"./config1.txt")) == 0 || (strcmp(argv[1],"./config2.txt")) == 0) {
       	if ((txt = test(argv[1])) == NULL)
      		exit(EXIT_FAILURE);
    }
    
    else {
       	fprintf(stderr,"Inserire un file di configurazione come argomento!\n");
       	exit(EXIT_FAILURE);
    }

    if((strcmp(argv[1],"./config2.txt")) == 0) {
		switch(pid = fork())  {
			case -1: /* errore */
				{
					fprintf(stderr,"Fork fallita!\n");
        			exit(EXIT_FAILURE);
				}
		
			case 0: /* figlio */
				{ 
					/* AVVIA PROCESSO SUPERMERCATO */
					execl ("./supermercato.o", "supermercato.o", argv[1], NULL);
					fprintf(stderr,"Esecuzione fallita\n");
					exit(EXIT_FAILURE); 
				}
		
			default: /* padre */
				{
					/* GESTIONE SEGNALI */
					memset(&sa, 0, sizeof(sa));
					sa.sa_handler = handler;
					sa.sa_flags = SA_RESTART;
					if(sigaction(SIGQUIT, &sa, NULL))
						fprintf(stderr,"Errore SIGQUIT!\n");
					if(sigaction(SIGHUP, &sa, NULL))
						fprintf(stderr,"Errore SIGHUP!\n");
					if(sigaction(SIGUSR1, &sa, NULL))
						fprintf(stderr,"Errore SIGUSR1!\n");
					if(sigaction(SIGUSR2, &sa, NULL))
						fprintf(stderr,"Errore SIGUSR2!\n");
				
					dir_pid = getpid();
					RunSocket();

					/* CREAZIONE THREAD CASSE */
					if((pthread_create(&managercasse, NULL, man_casse, NULL)) != 0) {		
						fprintf(stderr,"Creazione thread manager casse fallita!\n");
   			     		exit(EXIT_FAILURE);
   					}
   			
   					if ((pthread_join(managercasse,NULL) != 0))
       					fprintf(stderr,"Join thread manager casse fallita!\n");
				
       				if((waitpid(superm_pid,NULL,0)) == -1) {
       					fprintf(stderr,"Waitpid fallita per errore %d!\n", errno);
   			     		exit(EXIT_FAILURE);
       				}

				}
    	}
    }

    else if((strcmp(argv[1],"./config1.txt")) == 0) {

    	/* GESTIONE SEGNALI */
		memset(&sa, 0, sizeof(sa));
		sa.sa_handler = handler;
		sa.sa_flags = SA_RESTART;
		if(sigaction(SIGQUIT, &sa, NULL))
			fprintf(stderr,"Errore SIGQUIT!\n");
		if(sigaction(SIGHUP, &sa, NULL))
			fprintf(stderr,"Errore SIGHUP!\n");
		if(sigaction(SIGUSR1, &sa, NULL))
			fprintf(stderr,"Errore SIGUSR1!\n");
		if(sigaction(SIGUSR2, &sa, NULL))
			fprintf(stderr,"Errore SIGUSR2!\n");
		
		/* CREAZIONE SOCKET */
		dir_pid = getpid();
		RunSocket();

		/* CREAZIONE THREAD MANAGER CASSE */
		if((pthread_create(&managercasse, NULL, man_casse, NULL)) != 0) {		
			fprintf(stderr,"Creazione thread manager casse fallita!\n");
     		exit(EXIT_FAILURE);
		}
  			
		if ((pthread_join(managercasse,NULL) != 0))
			fprintf(stderr,"Join thread manager casse fallita!\n");

		while(chiusura == 0);

    }
	/*CHIUSURA SOCKET */
    close(fd_skt);
	close(fd_c);


    fprintf(stdout, "Sto chiudendo il direttore...\n");
	return 0;
}
