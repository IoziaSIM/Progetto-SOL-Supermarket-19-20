#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>

typedef struct {
	int id;
    int n_acquisti;
    int tot_time;
    int queue_time;
    int n_casse;
} customer;

typedef struct queue_list {
	customer* cliente;         
	struct queue_list* next;
} queue;

typedef struct {
	int id;
   	int n_prodotti;
   	int n_clienti;
   	int time;
    float service_time;
    int n_chiusure;
	int aperto;
	int randomtime;
	pthread_mutex_t lock;
	queue* coda;
} checkout;

// funzione che inizializza i dati di una cassa

checkout* cassa_init (int i)
{
	checkout* cassa = malloc (sizeof(checkout));
	cassa->id = i;
	cassa->n_prodotti = 0;
	cassa->n_clienti = 0;
	cassa->time = 0;
	cassa->service_time = 0;
	cassa->n_chiusure = 0;
	cassa->aperto = 0;
	cassa->randomtime = 0;
	pthread_mutex_init(&(cassa->lock), NULL);
	cassa->coda = NULL;
	return cassa;
}

// funzione che inizializza i dati di un cliente

void cliente_init(customer* cliente, int i) 
{
	cliente->id = i;
    cliente->n_acquisti = 0;
    cliente->tot_time = 0;
    cliente->queue_time = 0;
    cliente->n_casse = 0;
}

// funzione che inserisce un cliente nella coda di una cassa

void cliente_push(checkout** cassa, customer** cl)
{
	queue* new_cliente = malloc(sizeof(queue));
	new_cliente->cliente = (*cl);
	new_cliente->next = NULL;
	pthread_mutex_lock(&((*cassa)->lock));
    if ((*cassa)->coda == NULL) {
        (*cassa)->coda = new_cliente;
        pthread_mutex_unlock(&((*cassa)->lock));
        return;
    }
    queue* curr = (*cassa)->coda;
    while(curr->next != NULL) 
    	curr = curr->next;
    curr->next = new_cliente;
	pthread_mutex_unlock(&((*cassa)->lock));
}

// funzione che ritorna il primo cliente presente nella coda di una cassa

customer* coda_pop (checkout** cassa)
{
	queue* remove = (*cassa)->coda;
	pthread_mutex_lock(&((*cassa)->lock));
	(*cassa)->coda = (*cassa)->coda->next;
	customer* cl = remove->cliente;
	free(remove);
	pthread_mutex_unlock(&((*cassa)->lock));
	return cl;
}

// funzione che ritorna quanti clienti sono presenti nella coda di una cassa

int coda_length (checkout* cassa)
{
	queue* curr = cassa->coda;
    int conta = 0;
    while(curr != NULL){
        conta++;
        curr = curr->next;
    }
    return conta;
} 

// funzione che elimina tutti i clienti di una coda di una cassa

void coda_reset (checkout** cassa)
{
	pthread_mutex_lock(&((*cassa)->lock));
	while((*cassa)->coda != NULL) {
        queue* remove = (*cassa)->coda;
        (*cassa)->coda = (*cassa)->coda->next;
        free(remove);
    }    
    pthread_mutex_unlock(&((*cassa)->lock));
}

// funzione che ritorna in un array il numero di clienti presenti nelle code di tutte le casse

int* all_code_length (checkout** cassa, int num)   
{
	int* len_array = malloc(num * sizeof(int));

	for(int i=0; i<num; i++) {
		pthread_mutex_lock(&(cassa[i]->lock));
		
		if(cassa[i]->aperto == 1){
			queue* curr = cassa[i]->coda;
    		int conta = 0;
    		while(curr != NULL){
      		  conta++;
       		  curr = curr->next;
    		}
    		len_array[i] = conta;
   		}

   		else
   			len_array[i] = -1;

   		pthread_mutex_unlock(&(cassa[i]->lock));
   	}

	return len_array;
		
}

// funzione che ritorna la posizione del cliente all'interno della coda di una cassa

int cl_position(checkout* cassa, customer* cl)     
{
	pthread_mutex_lock(&(cassa->lock));
	queue* curr = cassa->coda;
	int conta = 0;
    while(curr->cliente->id != cl->id && curr != NULL){
        conta++;
        curr = curr->next;
    }
	pthread_mutex_unlock(&(cassa->lock));
	if(curr == NULL)
		conta = -1;
	return conta;
}

// funzione che rimuove un cliente dalla coda di una cassa

void cl_remove(checkout** cassa, customer* cl)   
{
	pthread_mutex_lock(&((*cassa)->lock));
	queue* curr = (*cassa)->coda;
	if(curr->cliente->id == cl->id) {
		(*cassa)->coda = (*cassa)->coda->next;
		free(curr);
		pthread_mutex_unlock(&((*cassa)->lock));
		return;
	}
	queue* prec = curr;
	curr=curr->next;
	while(curr->cliente->id != cl->id) { 
		prec = curr;
		curr = curr->next;
	}
	prec->next = curr->next;
	free(curr);
	pthread_mutex_unlock(&((*cassa)->lock));
}

// funzione che ritorna il numero della cassa la cui coda possiede meno clienti

int min_coda(int* array, int size)
{
	int min = 1000000;
	int j = -1;
	for(int i=0; i<size; i++)
		if(array[i] != -1 && array[i] < min) {
			min = array[i];
			j = i;
		}

	return j;
}
