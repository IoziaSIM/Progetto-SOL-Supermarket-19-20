#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct config {
    int K; 
    int C; 
    int E; 
    int T; 
    int P; 
    int S; 
    int S1;
    int S2; 
    int Open; 
    int Prod_t;
    int News_t; 
} config;

// funzione che controlla che i parametri inseriti siano corretti

int txt_control(config* txt) {
    if (!(txt->P >= 0 && txt->T > 10 && txt->K > 0 && txt->S > 0 && (txt->E > 0 && txt->E < txt->C) && txt->C > 0 && txt->S1 > 0  && txt->S2 > 1 && txt->S1 <= txt->K && txt->Open > 0 && txt->Open <= txt->K && txt->Prod_t >= 0 && txt->News_t > 0)){
        fprintf(stderr,"Configurazione non valida\n");
        return -1;
    }
    else return 1;
}

// funzione che legge da un file i parametri inseriti

config* test(const char* pmetro) 
{
	int i=0;
   	config* txt;
   	FILE* fdc = NULL;
   	char* buf;
   	char* tmp;

   	if ((fdc = fopen(pmetro, "r")) == NULL) {
       	fclose(fdc);
       	return NULL;
   	}

   	if ((txt = malloc(sizeof(config))) == NULL) {
       	fclose(fdc);
       	free(txt);
       	return NULL;    
   	}	

   	if ((buf = malloc(256*sizeof(char))) == NULL) {
       	fclose(fdc); 
       	free(txt); 
       	free(buf);
       	return NULL;
   	}

   	while(fgets(buf, 256, fdc) != NULL) {  
       
       	tmp = buf;
       	while(*buf != '=') 
       		buf++; 
       	buf++;
       	switch (i) {
       		case 0: txt->K = atoi(buf);
             	break;
            case 1: txt->C = atoi(buf);
                break;
            case 2: txt->E = atoi(buf);
                break;
            case 3: txt->T = atoi(buf);
              	break;
            case 4: txt->P = atoi(buf);
              	break;
            case 5: txt->S = atoi(buf);
               	break;
            case 6: txt->S1 = atoi(buf);
              	break;
            case 7: txt->S2 = atoi(buf);
              	break;
            case 8: txt->Open = atoi(buf);
              	break;
            case 9: txt->Prod_t = atoi(buf);
              	break;
            case 10: txt->News_t = atoi(buf);
              	break;

       		default:
               	break;
       	}

   	    i++;
      	buf = tmp;
   	}
    	
   	if ((txt_control(txt)) == -1) {
       	fclose(fdc); 
      	free(txt); 
       	free(buf);
       	return NULL;
   	}
    
   	free(buf);
   	fclose(fdc); 
   
   	return txt;
}
