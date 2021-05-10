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
#include "./lib/structs.c"
#include "./lib/config.c"
#include "./lib/rwn.c"

#define UNIX_PATH_MAX 108   
#define SOCKNAME "./mysock"

static checkout** cassa;
static time_t sec;
static config* txt;
static FILE* final;
static int cl_attivi;
static int chiusura = 0;
static int apertura = 0;
static int* servito;
static int* loading;
static pthread_mutex_t* mtxservito;
static pthread_mutex_t mtxattivo = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mtxfile = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mtxuscita = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mtxresponso = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t* checkout_cond;  
static pthread_cond_t* client_cond; 
static pthread_cond_t resp_cond = PTHREAD_COND_INITIALIZER;
static long fd_skt = -1;
static pid_t superm_pid;
static pid_t dir_pid; 
volatile sig_atomic_t sighup = 0;    
volatile sig_atomic_t sigquit = 0;
volatile sig_atomic_t cl_uscita = 0;

static void handler (int signum)
{
	if(signum == SIGHUP) sighup = 1;
	if(signum == SIGQUIT) sigquit = 1;
	if(signum == SIGUSR1) cl_uscita = 1;
}

void allocate()
{

	if((loading=calloc(txt->C,sizeof(int))) == NULL){
   		fprintf(stderr, "Allocazione array loading fallita!\n");
   		exit(EXIT_FAILURE); 
  	}

  	if((servito=calloc(txt->K, sizeof(int))) == NULL){
   		fprintf(stderr, "Allocazione array servito fallita!\n");
   		exit(EXIT_FAILURE); 
  	}

  	if((mtxservito=malloc(txt->K * sizeof(pthread_mutex_t))) == NULL){
   		fprintf(stderr, "Allocazione mutex array servito fallita!\n");
   		exit(EXIT_FAILURE); 
  	}
    
  	for (int i=0; i<txt->K; i++){
   		if (pthread_mutex_init(&mtxservito[i], NULL) != 0) {
   			fprintf(stderr, "Inizializzazione mutex array servito fallita!\n");
   			exit(EXIT_FAILURE);                   
   		}
  	}

  	if((checkout_cond=malloc(txt->K * sizeof(pthread_cond_t))) == NULL) {
   		fprintf(stderr, "Allocazione condition variable delle casse fallita!\n");
   		exit(EXIT_FAILURE); 
  	}
    
  	for (int i=0; i<txt->K; i++){
   		if (pthread_cond_init(&checkout_cond[i], NULL) != 0) {
   			fprintf(stderr, "Inizializzazione condition variable delle casse fallita!\n");
   			exit(EXIT_FAILURE);                   
    	}
  	} 

  	if((client_cond=malloc(txt->K * sizeof(pthread_cond_t))) == NULL) {
   		fprintf(stderr, "Allocazione condition variable del cliente fallita!\n");
   		exit(EXIT_FAILURE); 
  	}
    
  	for (int i=0; i<txt->K; i++){
   		if (pthread_cond_init(&client_cond[i], NULL) != 0) {
   			fprintf(stderr, "Inizializzazione condition variable del cliente fallita!\n");
   			exit(EXIT_FAILURE);                   
    	}
  	} 
     
}

void reallocate(int index, int size)
{
	if((loading=realloc(loading, size * sizeof(int))) == NULL){
   		fprintf(stderr, "Riallocazione array laoding fallita!\n");
   		exit(EXIT_FAILURE); 
  	}

  	for(int i=index; i<size; i++) {
  		loading[i] = 0;
  	}	
}

void deallocate()
{
	free(loading);
	free(servito);
	free(mtxservito);
	free(checkout_cond);
	free(client_cond);
} 

void ConnectSocket()
{	
	pid_t* buf;
 	struct sockaddr_un sock;
	
	sock.sun_family=AF_UNIX;
	strncpy(sock.sun_path, SOCKNAME, UNIX_PATH_MAX); 

    if ((fd_skt = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {	
		fprintf(stderr,"Client: Impossibile creare la socket!\n");
        exit(EXIT_FAILURE);
  	}  
  	
        /*CONNESSIONE AL DIRETTORE*/
    while (connect(fd_skt, (const struct sockaddr *)&sock, sizeof(sock)) == -1 ) {  
      	/* non esiste il file */
        if (errno == ENOENT) 
        		sleep(1);
      	else {
      		fprintf(stderr,"Client: Errore connect!\n");
            exit(EXIT_FAILURE); 
        }
    }

    /* SCAMBIO PID */
	buf = malloc(sizeof(pid_t));
	buf[0] = superm_pid;
    
    if((writen(fd_skt, (pid_t*) buf, sizeof(pid_t))) < 0) {
        fprintf(stderr,"PID Client: Write fallita!\n");
        exit(EXIT_FAILURE);
    }

    if((readn(fd_skt, (pid_t*) buf, sizeof(pid_t))) < 0) {
        fprintf(stderr,"PID Client: Read fallita!\n");
        exit(EXIT_FAILURE);
    }

	dir_pid = buf[0];
	free(buf);

}

int alg_decision(customer* cl, int n_cassa)
{
	/* SELEZIONO LA CASSA CON IL MINOR NUMERO DI CLIENTI IN CODA E MI SALVO QUANTI CLIENTI HA*/
	int* len_array = all_code_length(cassa, txt->K);
	int min = 1000000;
	int j = -1;
	for(int i=0; i<txt->K; i++)
		if(len_array[i] != -1 && len_array[i] < min) {
			min = len_array[i];
			j = i;
		}

	free(len_array);

	/* SE HO SELEZIONATO UNA CASSA DIVERSA E IL NUMERO DI CLIENTI IN CODA E' MINORE DELLA CODA ATTUALE, CAMBIO*/
	int pos = cl_position(cassa[n_cassa], cl);
	if(j != n_cassa && min < pos) 
		return j;
	else 
		return n_cassa;
}

void* cliente_t (void* arg)
{
	struct timespec store;
    struct timespec store2;
    struct timespec store3;
    struct timespec store4;
    long time1,time2,time3=-1,time4=-1;
    int randomtime, n_cassa, pos, new_cassa, check=0;
    int n_visita[txt->K];
    int id = *((int*)arg);
    unsigned int seed;
  
	/* IMPOSTO DATI CLIENTE */
    customer* cl  = malloc(sizeof(customer)); 
    cliente_init (cl,id);

    free(arg); 
     
    seed = cl->id + sec; 

    for(int i=0; i<txt->K; i++) 
		n_visita[i] = 0;
	
	/* PRENDO IL TEMPO DI ENTRATA DEL CLIENTE */
    clock_gettime(CLOCK_REALTIME, &store);
    time1 = (store.tv_sec)*1000 + (store.tv_nsec) / 1000000;
	   	
    /*TEMPO PER GLI ACQUISTI */
	while (1){
		if((randomtime = rand_r(&seed) % (txt->T)) > 10)
			break;
	} 
	
	if(sigquit == 0) {
   		struct timespec ms = {(randomtime/1000),((randomtime%1000)*1000000)};
    	nanosleep(&ms,NULL);
    	/* NUMERO DI PRODOTTI */ 
		cl->n_acquisti = rand_r(&seed) % (txt->P); 
    }

	if (cl->n_acquisti != 0 && sigquit == 0) {    /* SE HA ACQUISTATO QUALCOSA */
    	
	    /* SCELTA CASUALE CASSA */
	    while(check == 0) {
    		n_cassa = rand_r(&seed) % (txt->K);
    		if (cassa[n_cassa]->aperto != 0) 
	       		check = 1;
	    }   
	    n_visita[n_cassa]=1;
	        	
	    if(sigquit == 0) {
	    	/* IL CLIENTE SI AGGIUNGE IN CODA */
	    	cliente_push(&cassa[n_cassa], &cl);  

	    	/* PRENDO IL TEMPO DI INIZIO ATTESA IN CODA */
	   		 clock_gettime(CLOCK_REALTIME, &store3); 
	   		 time3 = (store3.tv_sec)*1000 + (store3.tv_nsec) / 1000000;  
		        	
		
			/* SEGNALE PER SVEGLIARE CASSACHE UN CLIENTE SI STA AGGIUNGENDO IN CODA */
	    	pthread_cond_signal(&checkout_cond[n_cassa]);
	    	 
	    	/* DURANTE L'ATTESA DEL CLIENTE NELLE CODE*/
	    	while(loading[cl->id] == 0 && sigquit == 0) {

			/*IN CASO DI CHIUSURA DELLA CASSA DEL CLIENTE*/
	    		if(cassa[n_cassa]->aperto == 0 && sigquit == 0) {
	    			check = 0;
	    			/* SCELTA CASUALE CASSA*/
    				while(check == 0) {
    					new_cassa = rand_r(&seed) % (txt->K);
    					if (cassa[new_cassa]->aperto != 0) 
	       					check = 1;
	       			}   
    				cliente_push(&cassa[new_cassa], &cl);
    				n_cassa = new_cassa;
    				n_visita[n_cassa] = 1;
    			}

	    		else if (sigquit == 0) {
	    			/* ATTESA PER ATTIVARE L'ALGORITMO */
	    			struct timespec ms2 = {(txt->S/1000),((txt->S%1000)*1000000)};
    				nanosleep(&ms2,NULL);
    				/* SE TUTTO E' OK E IL CLIENTE NON STA NELLE PRIME 2 POSIZIONI ATTIVA L'ALGORITMO */
    				if(loading[cl->id] == 0 && cassa[n_cassa]->aperto == 1 && sigquit == 0) {
    					if((pos = cl_position(cassa[n_cassa], cl)) > 2) {
    						new_cassa = alg_decision (cl, n_cassa);
    						/* SE L'ALGORITMO RITORNA UN'ALTRA CASSA, CAMBIO CASSA*/
    						if(new_cassa != n_cassa) {
    							cl_remove(&cassa[n_cassa], cl);
    							cliente_push(&cassa[new_cassa], &cl);
    							n_cassa = new_cassa;
    							n_visita[n_cassa] = 1;
    						}
    					}
    				}
    			}
	    	}

		/* PRENDO IL TEMPO DI USCITA DALLA CASSA */
	    	clock_gettime(CLOCK_REALTIME, &store4); 
	    	time4 = (store4.tv_sec)*1000 + (store4.tv_nsec) / 1000000;

	    	cl->queue_time = time4 - time3;

	    }

	    if(sigquit == 0) {

			/* IN ATTESA CHE LA CASSA SERVA IL CLIENTE*/
	   		pthread_mutex_lock(&mtxservito[n_cassa]);         
	    	while(servito[n_cassa] == 0)
	    		pthread_cond_wait(&client_cond[n_cassa], &mtxservito[n_cassa]);
	    	servito[n_cassa] = 0;
	    	pthread_mutex_unlock(&mtxservito[n_cassa]);

		/* PRENDO ORARIO DI USCITA CLIENTE */
	    	clock_gettime(CLOCK_REALTIME, &store2); 
			time2 = (store2.tv_sec)*1000 + (store2.tv_nsec) / 1000000;
		
			cl->tot_time = time2 - time1;

			/* CONTROLLO QUANTE CASSE HA VISITATO IL CLIENTE */
			for(int i=0; i<txt->K; i++)
				if(n_visita[i] == 1)
					cl->n_casse++;
		
			pthread_mutex_lock(&mtxfile);
    		fprintf(final,"CLIENTE -> | id cliente:%d | n. prodotti acquistati:%d | tempo tot. nel supermercato: %0.3f s | tempo speso in coda: %0.3f s | n. di code visitate: %d | \n",cl->id, cl->n_acquisti, (double) cl->tot_time/1000, (double) cl->queue_time/1000, cl->n_casse);
    		pthread_mutex_unlock(&mtxfile);  
    	
    		free(cl);   

    		pthread_mutex_lock(&mtxattivo);
        	cl_attivi--;
        	pthread_mutex_unlock(&mtxattivo);
        	pthread_exit(NULL); 
    	
    	}
		
	}
		
	else if(cl->n_acquisti == 0 && sigquit == 0) { /* SE NON HA ACQUISTATO NULLA */
		
		pthread_mutex_lock(&mtxuscita);
		/*INVIO SEGNALE DI USCITA CLIENTE AL DIRETTORE*/
		kill(dir_pid, SIGUSR1);
		while(cl_uscita == 0);
		cl_uscita = 0;
		pthread_mutex_unlock(&mtxuscita);
		
		/* PRENDO ORARIO DI USCITA CLIENTE */
		clock_gettime(CLOCK_REALTIME, &store2); 
		time2 = (store2.tv_sec)*1000 + (store2.tv_nsec) / 1000000;
		cl->tot_time = time2 - time1;

		pthread_mutex_lock(&mtxfile);
        fprintf(final,"CLIENTE -> | id cliente:%d | n. prodotti acquistati: 0 | tempo tot. nel supermercato: %0.3f s | tempo speso in coda: %0.3f s | n. di code visitate: 0 | \n", cl->id, (double) cl->tot_time/1000, (double) cl->queue_time/1000);
        pthread_mutex_unlock(&mtxfile);

        free(cl); 

        pthread_mutex_lock(&mtxattivo);
        cl_attivi--;
        pthread_mutex_unlock(&mtxattivo);
        pthread_exit(NULL); 
        
	}

	if(sigquit == 1) { /*IN CASO DI SIGQUIT */
		
		/* PRENDO ORARIO DI USCITA CLIENTE */
		clock_gettime(CLOCK_REALTIME, &store2); 
		time2 = (store2.tv_sec)*1000 + (store2.tv_nsec) / 1000000;
		cl->tot_time = time2 - time1;

		/* CONTROLLO QUANTE CASSE HA VISITATO IL CLIENTE */
		for(int i=0; i<txt->K; i++)
				if(n_visita[i] == 1)
					cl->n_casse++;

		pthread_mutex_lock(&mtxfile);
        fprintf(final,"CLIENTE -> | id cliente:%d | n. prodotti acquistati: 0 | tempo tot. nel supermercato: %0.3f s | tempo speso in coda: %0.3f s | n. di code visitate: %d | \n", cl->id, (double) cl->tot_time/1000, (double) cl->queue_time/1000, cl->n_casse);
        pthread_mutex_unlock(&mtxfile);
        free(cl); 
        pthread_exit(NULL); 
	}

	pthread_exit(NULL); 	
}


void *cassiere_t (void* arg)
{
	struct timespec store;
	struct timespec store2;
  	long time1, time2;
  	int id = *((int*)arg);
  	int servicetime, acquisti;
	unsigned int seed = id + sec; 

	free(arg);
	cassa[id]->aperto = 1;
	
	/* GENERO NUMERO RANDOMICO PER CASSIERE */
	if(cassa[id]->randomtime == 0)
		while (1) {
			if((cassa[id]->randomtime = rand_r(&seed) % 80) > 20) //genera un intero in millisecondi
				break; 
		}

	/* PRENDO TEMPO DI ATTIVAZIONE CASSA */
  	clock_gettime(CLOCK_REALTIME, &store); 
  	time1 = (store.tv_sec)*1000 + (store.tv_nsec) / 1000000;

	while(1) {
       	
       	 /* IN CASO CODA SENZA CLIENTI */
        pthread_mutex_lock(&(cassa[id]->lock));
		while(coda_length(cassa[id]) == 0 && cassa[id]->aperto == 1 && sigquit == 0 && !(sighup == 1 && cl_attivi == 0)) {
     		pthread_cond_wait(&checkout_cond[id], &(cassa[id]->lock));
   		}
     	pthread_mutex_unlock(&(cassa[id]->lock));

    	if (cassa[id]->aperto == 1 && sigquit == 0 && !(sighup == 1 && cl_attivi == 0)) {    /* IN CASO SIA TUTTO OK */
        		
        	/* PRENDE CLIENTE DALLA CODA E LO AVVISO CHE STA PAGANDO */
			customer* first = coda_pop(&cassa[id]);
			loading[first->id] = 1;
			acquisti = first->n_acquisti;
				
			/* VERIFICA ACQUISTI */
			servicetime = cassa[id]->randomtime + (txt->Prod_t * acquisti); //genera intero in millisecondi
			struct timespec ms = {(servicetime/1000),((servicetime%1000)*1000000)}; /* 1: il tempo in secondi, 2: il resto del tempo in nanosecondi(10^-9)*/
			nanosleep(&ms, NULL); 
		
			/*AGGIORNA DATI CASSA */
			cassa[id]->n_prodotti += acquisti;
			cassa[id]->n_clienti++;
      		cassa[id]->service_time += servicetime;
			
			/* SVEGLIA IL CLIENTE SERVITO */
			pthread_mutex_lock(&mtxservito[id]);
			servito[id] = 1;
			pthread_mutex_unlock(&mtxservito[id]);	
			pthread_cond_signal(&client_cond[id]); 
		}
        	
    	if(cassa[id]->aperto == 0 || sigquit == 1 || (sighup == 1 && cl_attivi == 0)) {	/* IN CASO DI CHIUSURA CASSA*/    
            		
      		/* PRENDO ORARIO DI CHIUSURA CASSA */
     		clock_gettime(CLOCK_REALTIME, &store2);
      		time2 = (store2.tv_sec)*1000 + (store2.tv_nsec) / 1000000;

      		if(cassa[id]-> aperto == 0 || sigquit == 1)
      			coda_reset(&cassa[id]);

      		/* AGGIORNO DATI SUPERMERCATO */
      		cassa[id]->time += time2-time1;
     		cassa[id]->n_chiusure++;

      		pthread_exit(NULL);

  		}
  	}
        
}    

void* g_clienti (void* arg)
{
	pthread_t* cliente;
	int diff = txt->C - txt->E;
	int i,j;
	int* p;

	/* ALLOCAZIONI E CREAZIONE DEI THREADS CLIENTI */
	if((cliente = malloc(txt->C * sizeof(pthread_t))) == 0) {
		fprintf(stderr, "Allocazione thread cliente fallito\n");
		exit(EXIT_FAILURE);
	}
	
	for (i=0; i<txt->C; i++) {
		p = malloc(sizeof(int));
		*p = i;
		if (pthread_create(&cliente[i], NULL, cliente_t, (void*) p) != 0) {
            fprintf(stderr,"Attivazione thread cliente fallito!\n");
            exit(EXIT_FAILURE);
      	}
      	pthread_mutex_lock(&mtxattivo);
        cl_attivi++;
        pthread_mutex_unlock(&mtxattivo);
    }
	
	/*IMMISSIONE DI NUOVI CLIENTI*/
    while(sigquit == 0 && sighup == 0) {
    	pthread_mutex_lock(&mtxattivo);
    	if(cl_attivi <= diff) {
    		pthread_mutex_unlock(&mtxattivo);
    		/* REALLOCAZIONE DELLE STRUTTURE DATI PER I CLIENTI*/
    		reallocate(i, i+txt->E);
    		if((cliente = realloc(cliente, (i+txt->E) * sizeof(pthread_t))) == NULL) {
    			fprintf(stderr, "Allocazione thread cliente fallito\n");
				exit(EXIT_FAILURE);
			}

			for (j=i; j<(i+txt->E); j++) {
				p = malloc(sizeof(int));
				*p = j;
				/*CREAZIONE NUOVI THREADS CLIENTE */
				if (pthread_create(&cliente[j], NULL, cliente_t, (void*) p) != 0) {
         		   fprintf(stderr,"Attivazione thread cliente fallito!\n");
                   exit(EXIT_FAILURE);
      			}
      			pthread_mutex_lock(&mtxattivo);
        		cl_attivi++;
        		pthread_mutex_unlock(&mtxattivo);
   		 	} 

   		 	i=j;		
		}

		else 
			pthread_mutex_unlock(&mtxattivo);
    }

    for (j=0; j<i; j++) {
       	if (pthread_join(cliente[j],NULL) == -1 ) 
            	fprintf(stderr,"Join cliente fallita!\n");
    }

    free(cliente);	
    pthread_exit(NULL);
}

void* news_t (void* arg)
{
	int* len_array;
	int* buf;
	int num;

	pthread_mutex_lock(&mtxattivo);
  	while(sigquit == 0 && !(sighup == 1 && cl_attivi == 0)) {
        pthread_mutex_unlock(&mtxattivo);
   		
   		/*ATTESA DEL SEGNALE DI INVIO DELLE NEWS */
   		struct timespec t={(txt->News_t / 1000),((txt->News_t % 1000)*1000000)}; 
     	nanosleep(&t,NULL);
        	
        pthread_mutex_lock(&mtxattivo);        	
        if (sigquit == 0 && !(sighup == 1 && cl_attivi == 0)) {  
        	pthread_mutex_unlock(&mtxattivo);

        	pthread_mutex_lock(&mtxresponso);

        	len_array = all_code_length(cassa, txt->K);
        	buf = malloc(2*sizeof(int));
 			
        	/*INVIO INFORMAZIONE AL DIRETTORE */
        	if(sigquit == 0 && !(sighup == 1 && cl_attivi == 0)){
 				if (writen(fd_skt, (int*) len_array, txt->K * sizeof(int)) < 0) {
        			fprintf(stderr, "news_t: Write fallita!\n");
        			exit(EXIT_FAILURE);
				}
			}

			/* LETTURA CODICE DI RITORNO DEL DIRETTORE */
			if(sigquit == 0 && !(sighup == 1 && cl_attivi == 0)){
				if((num = readn(fd_skt, (int*) buf, 2*sizeof(int))) < 0)  {
					fprintf(stderr, "news_t: Read fallita!\n");
					exit(EXIT_FAILURE);
				}

				if(buf[0] == 1)  
					chiusura = 1;
				
				else if (buf[1] == 1)
					apertura = 1;

			}
			pthread_mutex_unlock(&mtxresponso);

			pthread_cond_signal(&resp_cond);       	

			free(len_array);
			free(buf);
			pthread_mutex_lock(&mtxattivo);
		}
 	}

	pthread_mutex_unlock(&mtxattivo);
	pthread_cond_signal(&resp_cond); 
	pthread_exit(NULL);
}



void* g_casse (void* arg)
{
	pthread_t* cassiere;
	pthread_t d_news;
	int* len_array;
	int* p;
	int k;

	/* ALLOCAZIONE E CREAZIONE DEI THREAD CASSIERI*/
	if((cassiere = malloc(txt->K * sizeof(pthread_t))) == 0) {
		fprintf(stderr, "Allocazione thread cassiere fallito!\n");
		exit(EXIT_FAILURE);
	}

	for (int i=0; i<txt->Open; i++) {  
		p = malloc(sizeof(int));
		*p = i;
    	if (pthread_create(&cassiere[i], NULL, cassiere_t, (void*) p) != 0) {
      			fprintf(stderr,"Attivazione thread cassiere fallito!\n");
      			exit(EXIT_FAILURE);
    	}
    }

    /* ATTIVAZIONE THREAD NEWS DEL DIRETTORE */
  	if (pthread_create(&d_news, NULL, news_t, NULL) != 0) {    
   		fprintf(stderr,"Creazione thread news fallita!\n");
   		exit(EXIT_FAILURE);
  	}
    
    pthread_mutex_lock(&mtxresponso);
    while (sigquit == 0 && !(sighup == 1 && cl_attivi == 0)) {
   		pthread_cond_wait(&resp_cond, &mtxresponso);

   		if(sigquit == 0 && !(sighup == 1 && cl_attivi == 0)){
   			len_array = all_code_length(cassa, txt->K);

			/* CHIUSURA DI UNA CASSA*/
   			if(chiusura == 1 && sigquit == 0 && !(sighup == 1 && cl_attivi == 0)){
   				int j = min_coda(len_array, txt->K);
   				pthread_mutex_lock(&(cassa[j]->lock));
   				cassa[j]->aperto = 0;
   				pthread_mutex_unlock(&(cassa[j]->lock));
   				pthread_cond_signal(&checkout_cond[j]); 
   				chiusura = 0;
   			}

			/* APERTURA DI UNA CASSA */
   			else if (apertura == 1 && sigquit == 0 && !(sighup == 1 && cl_attivi == 0)){
   				for(k=0; k<txt->K; k++)
   					if(cassa[k]->aperto == 0)
   						break;
   				p = malloc(sizeof(int));
				*p = k;
   				if (pthread_create(&cassiere[k], NULL, cassiere_t, (void*) p) != 0) {
      				fprintf(stderr,"Attivazione thread cassiere fallito!\n");
      				exit(EXIT_FAILURE);
    			}
    			apertura = 0;
   			}

   			free(len_array);
   		}
   	}
   	pthread_mutex_unlock(&mtxresponso);

    if (pthread_join(d_news, NULL) == -1 ) {
     	fprintf(stderr,"Join news al direttore fallita!\n");
    }

    for (int i=0; i<txt->K; i++) {
   		pthread_cond_signal(&checkout_cond[i]); 
    	if (pthread_join(cassiere[i], NULL) == -1 )
     		fprintf(stderr,"Join cassiere fallita!\n");
   	}
  	
  	free(cassiere);
  	pthread_exit(NULL);
}

int main (int argc, const char* argv[])
{
	pthread_t gestorecasse; 
	pthread_t gestoreclienti;
	int tot_prodotti=0, tot_clienti=0;
	sec = time(NULL);
	struct sigaction sa;

	/* INIZIALIZZAZIONE */
	if (argc!=2) {
       	fprintf(stderr,"Numero di argomenti non corretto!\n");
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

	/* ALLOCAZIONI STRUTTURE DATI */
	cassa = malloc (txt->K * sizeof(checkout*));
	for (int i=0; i<txt->K; i++)
		cassa[i] = cassa_init(i);

	allocate();
	
	/* CREAZIONE FILE DI LOG*/

	if ((final = fopen("final.log", "w")) == NULL) { 
		fprintf(stderr, "Apertura file .log fallita!\n");
		exit(EXIT_FAILURE);
    }

    /* GESTIONE SEGNALI */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handler;
	sa.sa_flags = SA_RESTART;
	if(sigaction(SIGQUIT, &sa, NULL))
		fprintf(stderr,"Errore SIGQUIT\n");
	if(sigaction(SIGHUP, &sa, NULL))
		fprintf(stderr,"Errore SIGHUP\n");
	if(sigaction(SIGUSR1, &sa, NULL))
		fprintf(stderr,"Errore SIGUSR1\n");

	 /* CREAZIONE SOCKET */
    superm_pid = getpid(); 
	ConnectSocket();
	
	/* CREAZIONE THREAD GESTORE CASSE E CLIENTI*/
	if((pthread_create(&gestorecasse, NULL, g_casse, NULL)) != 0) {		
		fprintf(stderr,"Creazione thread gestore casse fallita!\n");
        exit(EXIT_FAILURE);
    }

	if((pthread_create(&gestoreclienti, NULL, g_clienti, NULL)) != 0) {		
		fprintf(stderr,"Creazione thread gestore clienti fallita!\n");
      	exit(EXIT_FAILURE);
    }

    if (pthread_join(gestorecasse, NULL) != 0) {
        fprintf(stderr,"Join thread gestore casse fallita!\n");
    }
    	
	if (pthread_join(gestoreclienti, NULL) != 0 ) {
        fprintf(stderr,"Join thread gestore clienti fallita!\n");
	}

    for (int i=0; i <txt->K; i++) {	
    	fprintf(final, "CASSIERE -> | id:%d | n. prodotti elaborati :%d | n. clienti serviti:%d | tempo tot. di apertura: %0.3f s | tempo medio di servizio: %0.3f s | n. di chiusure :%d |\n",cassa[i]->id, cassa[i]->n_prodotti, cassa[i]->n_clienti, (double) cassa[i]->time/1000, (cassa[i]->service_time/cassa[i]->n_clienti)/1000, cassa[i]->n_chiusure);
     		tot_clienti += cassa[i]->n_clienti;
        	tot_prodotti += cassa[i]->n_prodotti;
   	}

    fprintf(final, "TOTALE CLIENTI SERVITI: %d\n", tot_clienti);
    fprintf(final, "TOTALE PRODOTTI ACQUISTATI: %d\n", tot_prodotti);

    /* DEALLOCAZIONI */
    for(int i=0; i<txt->K; i++)
    	free(cassa[i]);
    free(cassa);
    free(txt);
    deallocate();
    fclose(final);
    close(fd_skt);
    /* INVIO SEGNALE DI CHIUSURA SUPERMERCATO AL DIRETTORE*/
    kill(dir_pid, SIGUSR2);

    return 0;
}
