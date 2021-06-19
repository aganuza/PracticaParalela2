#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include <gsl/gsl_rng.h>
#include <gsl/gsl_randist.h>
#include <stdbool.h>
#include <mpi.h>
#include "estructuras.h"
#include "funcionesParal.h"



int main(int argc, char *argv[]){

	if (argc != 2) {
		printf ("\n Formato incorrecto: ./main fichero_parametros.txt \n");
		exit (-1);
	}

	MPI_Init(&argc, &argv);

	//Inicializar las variables de los parametros
	int world_rank, world_size;
	//Parametros para paralelizar
	struct persona *personaskter, *contagproces, *sumacontagiados;
	//Parametros de simulación
	struct poblacion pobl;
	struct persona *personas;
	//Parametros gsl
	int seed, lag;
	float mu, beta, alfa;
	gsl_rng *r;
	//Parametros MPI
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    //printf("debug1 %d\n", world_rank);
	//Leer los parametros del fichero
	if(world_rank == 0){

		FILE *f1;
		f1=fopen("data.txt","r");
	    int a;

	    if(f1==NULL){
	        printf("ERROR! No se pudo abrir el documento");
	        exit(-1);
	    }
	    //printf("debug2 %d\n", world_rank);
	    //Meter datos en la población
	    fscanf(f1, "%d", &a);
	    pobl.per_max=a;
	    fscanf(f1, "%d", &a);
	    pobl.radio_con=a;
	    fscanf(f1, "%d", &a);
	    pobl.periodo_inc=a;
	    fscanf(f1, "%d", &a);
	    pobl.periodo_rec=a;
	    fscanf(f1, "%d", &a);
	    pobl.vel_max=a;
	    fscanf(f1, "%d", &a);
	    pobl.vac_nec=a;
	    fscanf(f1, "%d", &a);
	    pobl.probabilidad_cont=a;
	    fscanf(f1, "%d", &a);
	    pobl.cambiovel=a;
	    fscanf(f1, "%d", &a);
	    pobl.limite=a;
	
	}
	
	MPI_Datatype pobla; //Tipo MPI para el struct poblacion
	Crear_Tipo2(&pobl.per_max, &pobl.radio_con, &pobl.periodo_inc, &pobl.periodo_rec, &pobl.vel_max, &pobl.vac_nec, &pobl.probabilidad_cont, &pobl.cambiovel, &pobl.limite, &pobla);
	//Broadcast para compartir pobl con los demas procesadores
	MPI_Bcast(&pobl, 1, pobla, 0, MPI_COMM_WORLD);

	//Inicializar el vector de personas con valores aletorios
	if(world_rank == 0){
		int lag, ale;
		//Edades aleatorias usando libreria gsl
		seed = 1;
	    mu = 100;
	    alfa = 3;
	    beta = 3;
		gsl_rng_env_setup();
	    r = gsl_rng_alloc (gsl_rng_default);
	    gsl_rng_set(r, seed);

	    //printf("debug3 %d\n", world_rank);
		//Inicializar los parametros
		personas = malloc(pobl.per_max*sizeof(struct persona));
		for (lag=0; lag<pobl.per_max; lag++){
			personas[lag].estado = 0;
			personas[lag].edad = roundf(mu * gsl_ran_beta(r, alfa, beta));
			personas[lag].tiempo = 0;
			personas[lag].riesgo = rand()%101;
			personas[lag].posicion[0] = rand()%pobl.limite+1;
			personas[lag].posicion[1] = rand()%pobl.limite+1;
			personas[lag].velocidad[0] = (rand()%pobl.vel_max*2+1)-pobl.vel_max;
			personas[lag].velocidad[1] = (rand()%pobl.vel_max*2+1)-pobl.vel_max;
		}
		gsl_rng_free(r);

		//printf("debug4 %d\n", world_rank);
		//Elegir el paciente 0
		ale = rand()%pobl.per_max+1;
		personas[ale].estado = 2;
	}

	//Iniciar el tiempo de ejecucion
	double t;
    struct timeval stop, start;
    if(world_rank == 0){
		gettimeofday(&start, NULL);
		//printf("debug5 %d\n", world_rank);
	}
	
	//En este array se guaradaran las personas contagiadas de todos los otros procesadores, lo que se envia y recoge del allgather
	sumacontagiados = malloc((pobl.per_max)*sizeof(struct persona));
	MPI_Datatype person; //Tipo MPI para el struct persona
	//Aqui se crea el tipo person, que es el equivalente MPI del struct persona
	Crear_Tipo(&(personas[0].estado), &(personas[0].edad), &(personas[0].tiempo), &(personas[0].riesgo), personas[0].posicion, personas[0].velocidad, &person);
	//printf("debug6 %d\n", world_rank);
	//Vector con el que trabajara cada uno de los procesadores, que sera una parte del vector original de personas
	personaskter = malloc((pobl.per_max/world_size)*sizeof(struct persona));
	//Se reparte el vector original personas entre cada uno de los procesadores, en el nuevo vector local personaskter
	//printf("debug7 %d\n", world_rank);
	//printf("debug7 %d\n",((pobl.per_max)/world_size));
	MPI_Scatter(&personas[0], ((pobl.per_max)/world_size), person, personaskter, ((pobl.per_max)/world_size), person, 0, MPI_COMM_WORLD);
	//printf("debug8 %d\n", world_rank);
	
	//Parametros locales de cada procesador
	int i,k,j,p,vacunados,vivos;
	int cont = 0;
	bool vac=true;
	int porcent;
	bool sumistrada = false;
	int t_ejec = 100;
	int contagiados = 0;
	//printf("debug9 %d\n", world_rank);
 	vacunados = 0;
	vivos = ((pobl.per_max)/world_size);
	//Para ver las personas en estado 2 o 1 en cada procesador y crear un array con estas en contagproces
    contagproces = malloc((pobl.per_max/world_size)*sizeof(struct persona));
    //printf("debug10 %d\n", world_rank);

    //Iniciar simulación
	while(cont<t_ejec && vivos != 0){
    
		mover(personaskter, &pobl, ((pobl.per_max)/world_size));
		//printf("bucle1 %d\n", cont);

		//Array que guardara todas las personas contagiadas de cada procesador para luego comunicar
		//Inicializarlo vacío para que no de errores de acceso a la memoria
		for (lag=0; lag<(pobl.per_max)/world_size; lag++){
			contagproces[lag].estado = 0;
			contagproces[lag].edad = 0;
			contagproces[lag].tiempo = 0;
			contagproces[lag].riesgo = 0;
			contagproces[lag].posicion[0] = 0;
			contagproces[lag].posicion[1] = 0;
			contagproces[lag].velocidad[0] = 0;
			contagproces[lag].velocidad[1] = 0;
		}

        //printf("bucle2 %d\n", world_rank);
		//Correr el vector local de cada procesador para buscar los contagiados
		for(i=0; i<(pobl.per_max/world_size); i++){
			if((personaskter[i].estado==1 || personaskter[i].estado==2)){
				//meterlo en la lista contagproces
				//contagproces[i] = personaskter[i];
				memcpy(&contagproces[i], &personaskter[i],sizeof(struct persona));
			}
		}
		//printf("bucle3 %d\n", world_rank);

		//LLamada MPI para garantizar la comunicacion entre procesadores, cada procesador pasa su vector de contagiados, y estos se juntan en el vector sumacontagiados
		MPI_Allgather(&contagproces[0], (pobl.per_max)/world_size, person, sumacontagiados, (pobl.per_max)/world_size, person, MPI_COMM_WORLD);
		
        //printf("bucle4 %d\n", world_rank);
		//En modo local, cada procesador controla el contagio entre sus personas
		for(i=0; i<(pobl.per_max/world_size)-1; i++){
			for (j=i+1; j<((pobl.per_max/world_size)); j++){
				if(fabs(personaskter[i].posicion[0]-personaskter[j].posicion[0])<=pobl.radio_con && fabs(personaskter[i].posicion[1]-personaskter[j].posicion[1])<=pobl.radio_con){
					if((personaskter[i].estado==1 || personaskter[i].estado==2) && personaskter[j].estado==0){
						if(pobl.probabilidad_cont >= (rand()%101)) {
							personaskter[j].estado = 1;
						}
					}else if((personaskter[j].estado==1 || personaskter[j].estado==2) && personaskter[i].estado==0) {
						if(pobl.probabilidad_cont >= (rand()%101)) {
							personaskter[i].estado = 1;
						}
					}
				}
			}
		}
		
		//Aqui cada procesador comarara con los contagiados de los otros procesadores
		for(i=0; i<(pobl.per_max/world_size); i++){
			for (j=0; j<(pobl.per_max); j++){
				if(fabs(personaskter[i].posicion[0]-sumacontagiados[j].posicion[0])<=pobl.radio_con && fabs(personaskter[i].posicion[1]-sumacontagiados[j].posicion[1])<=pobl.radio_con){
					if((sumacontagiados[j].estado==1 || sumacontagiados[j].estado==2) && personaskter[i].estado==0){
						if(pobl.probabilidad_cont >= (rand()%101)) {
							personaskter[i].estado = 1;
						}
					}
				}
			}
		}
		
		//printf("bucle5 %d\n", world_rank);
		//Aqui correremos toda la lista de la poblacion para tratarlos
		for(p=0; p<((pobl.per_max)/world_size); p++){
			//si una persona esta en estado 1 se empezara a sumar su tiempo
			if(personaskter[p].estado==1){
				personaskter[p].tiempo++;
				//cuando llegue al mismmo tiempo que el periodo de inc pasara a estado 2 y el tiempo de pondra a 0
				if(personaskter[p].tiempo >= pobl.periodo_inc){
					personaskter[p].estado=2;
					personaskter[p].tiempo=0;
				}
				//cuando una persona este en estado 2 se le ira sumando tiempo
			}else if(personaskter[p].estado==2){
				personaskter[p].tiempo++;
				//cuando la persona este aun tik de la recuperacion, se calculara con el riesgo si este morira, de ser asi, pasara a estado 5, si no a estado 3
				if(personaskter[p].tiempo == (pobl.periodo_rec-1)){
					if(personaskter[p].riesgo >= (rand()%101)){
						personaskter[p].estado=5;
						vivos--;
					}else{
						personaskter[p].tiempo+=1;
					}
				}else if(personaskter[p].tiempo >= pobl.periodo_rec){
					personaskter[p].estado=3;
					personaskter[p].tiempo=0;
					vacunados++;
				}
			}
		}
        //printf("bucle6 %d\n", world_rank);
		//printf("%d %d %d %d\n", vivos, vacunados,porcent,world_rank);
		
		porcent = (100*vacunados/vivos);
		if(porcent<pobl.vac_nec){
			sumistrada = false;
			for(k = 0; k<=((pobl.per_max)/world_size) && !sumistrada; k++){
				if(personaskter[k].estado == 0){
					personaskter[k].estado=4;
					vacunados++;
					sumistrada = true;
				}
			}
		}else{
			vac=false;
		}
		cont++;
	}
	//printf("debug11 %d\n", world_rank);
	//Gather, recuperación de todos los datos mandados de vuelta en el proceso 0
	MPI_Gather(&personaskter[0], (pobl.per_max)/world_size, person, personas, (pobl.per_max)/world_size, person, 0, MPI_COMM_WORLD);
	//printf("debug12 %d\n", world_rank);
	if(world_rank == 0){
	 	//printf("debug13 %d\n", world_rank);
	 	//Tomar el tiempo de ejecución
	 	gettimeofday(&stop, NULL);
	 	t = (double)(stop.tv_usec - start.tv_usec) / 1000000 + (double)(stop.tv_sec - start.tv_sec);
        printf("Tiempo de ejecución (en segundos): %.6f s\n",t); 	
		//free(personas);
        //Escribir resultados y posiciones en fichero
	 	simulacion(&pobl, personas, t);
	 }
	//printf("debug14 %d\n", world_rank);
	//free(sumacontagiados);	
    //free(personaskter);
    //free(contagproces);
	//printf("debug15 %d\n", world_rank);
	MPI_Finalize();
	//return(0);
}
