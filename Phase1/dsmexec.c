#include "common_impl.h"

/* variables globales */

/* un tableau gerant les infos d'identification */
/* des processus dsm */
dsm_proc_t *proc_array = NULL; 

/* le nombre de processus effectivement crees */
volatile int num_procs_creat = 0;

void usage(void)
{
  fprintf(stdout,"Usage : dsmexec machine_file executable arg1 arg2 ...\n");
  fflush(stdout);
  exit(EXIT_FAILURE);
}

void sigchld_handler(int sig) {
	/* on traite les fils qui se terminent */
	/* pour eviter les zombies */
	while (waitpid((pid_t) -1, NULL, WNOHANG) > 0)
		num_procs_creat--;

	if (num_procs_creat == 0)
		exit(EXIT_SUCCESS);
}

/*******************************************************/
/*********** ATTENTION : BIEN LIRE LA STRUCTURE DU *****/
/*********** MAIN AFIN DE NE PAS AVOIR A REFAIRE *******/
/*********** PLUS TARD LE MEME TRAVAIL DEUX FOIS *******/
/*******************************************************/

int main(int argc, char *argv[])
{
  if (argc < 3){
    usage();
  } else {       
     pid_t pid;
     int num_procs = 0;
     int i;
     
     /* Mise en place d'un traitant pour recuperer les fils zombies*/        
     /* XXX.sa_handler = sigchld_handler; */
     
     /* lecture du fichier de machines */
     /* 1- on recupere le nombre de processus a lancer */
     /* 2- on recupere les noms des machines : le nom de */
     /* la machine est un des elements d'identification */
     char **machines = NULL;
      num_procs = lire_machinefile(argv[1], &machines);

        // Afficher les noms des machines (pour vérification)
        printf("Machines trouvées (%d) :\n", num_procs);
        for (int i = 0; i < num_procs; i++) 
        {
            printf("%s\n", machines[i]);
        }
 


     /* creation de la socket d'ecoute */
     /* + ecoute effective */ 
      char hostname[MAX_STR];  // Nom de la machine à résoudre
      gethostname(hostname, MAX_STR);
      int port;  // Port choisi dynamiquement

      // Création de la socket d'écoute
      int listen_fd = create_listen_socket(5, hostname, &port);

      // Affichage des informations
      printf("Serveur prêt à accepter des connexions sur %s:%d\n", hostname, port);

     // Initialisation pipes
      int pipes_stdout[num_procs][2];
      int pipes_stderr[num_procs][2];

     /* creation des fils */
      for(int i = 0; i < num_procs ; i++) 
      {
      
         /* creation du tube pour rediriger stdout */
         if (pipe(pipes_stdout[i]) == -1) {
            perror("Erreur lors de la création de pipe");
            exit(EXIT_FAILURE);
         }
         /* creation du tube pour rediriger stderr */
         if (pipe(pipes_stderr[i]) == -1) {
            perror("Erreur lors de la création de pipe");
            exit(EXIT_FAILURE);
         }

         pid = fork();
         if(pid == -1) ERROR_EXIT("fork");
         
         if (pid == 0) { /* fils */	
            
            /* redirection stdout */	      
            dup2(pipes_stdout[i][1], STDOUT_FILENO);
            /* redirection stderr */	      	      
            dup2(pipes_stderr[i][1], STDERR_FILENO);

            // Fermeture des extrémités inutiles
            close(pipes_stdout[i][0]);
            close(pipes_stdout[i][1]);
            close(pipes_stderr[i][0]);
            close(pipes_stderr[i][1]);
            
            // Récupère la variable DSM_BIN
            char *dsm_bin = getenv("DSM_BIN");
            
            // Construire le chemin complet vers le programme "dsmwrap"
            char path_to_dsmwrap[512];
            snprintf(path_to_dsmwrap, sizeof(path_to_dsmwrap), "%s/dsmwrap", dsm_bin);
            
            // Test : Écrire dans stdout et stderr
            //printf("Message test sur stdout depuis l'enfant %d\n", i);
            //fprintf(stderr, "Message test sur stderr depuis l'enfant %d\n", i);
            
            /* Creation du tableau d'arguments pour le ssh */ 
            char port_str[6];  // buffer pour contenir la chaîne de caractères du port
            sprintf(port_str, "%d", port);  // Convertir le port en chaîne de caractères

            char *newargv[] = {
               "ssh", machines[i], path_to_dsmwrap, port_str,hostname, NULL
               };

            //printf("Execution de ssh %s\n", machines[i]);

            /* jump to new prog : */
            execvp("ssh",newargv);
            perror("Erreur lors de l'utilisation de execvp");
            exit(EXIT_FAILURE);

         } else  if(pid > 0) { /* pere */		      
            /* fermeture des extremites des tubes non utiles */
            close(pipes_stdout[i][1]);
            close(pipes_stderr[i][1]);
            char buffer[256];

            // Lecture stdout
            int nbytes = read(pipes_stdout[i][0], buffer, sizeof(buffer) - 1);
            if (nbytes > 0) {
               buffer[nbytes] = '\0';
               printf("Parent a reçu stdout de l'enfant %d : %s\n", i, buffer);
            }

            // Lecture stderr
            nbytes = read(pipes_stderr[i][0], buffer, sizeof(buffer) - 1);
            if (nbytes > 0) {
               buffer[nbytes] = '\0';
               printf("Parent a reçu stderr de l'enfant %d : %s\n", i, buffer);
            }
            num_procs_creat++;	      
         }
      }
     
      int client_fds[num_procs];  // Tableau pour stocker les descripteurs des connexions
      memset(client_fds, -1, sizeof(client_fds));  // Initialisation à -1 (aucune connexion)


      for(i = 0; i < num_procs ; i++){
      /* on accepte les connexions des processus dsm */
         
         struct sockaddr_in client_addr;
         socklen_t client_len = sizeof(client_addr);
         int client_fd;
         
         // Accepter une connexion entrante
         if (listen_fd < 0) {
            fprintf(stderr, "Erreur : listen_fd est invalide (%d)\n", listen_fd);
            exit(EXIT_FAILURE);
         }
         
         client_fd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);
         if (client_fd == -1) {
            perror("Erreur lors de l'acceptation");
            i--;  // Réessayer pour ce processus
            continue;
         }
         
         printf("Connexion acceptée de %s:%d\n",
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port));
         
         // Stocker le descripteur de fichier
         client_fds[i] = client_fd;
         
      
      /*  On recupere le nom de la machine distante */
      /* les chaines ont une taille de MAX_STR */
      int length;
      if (read(client_fd, &length, sizeof(int)) == -1) {
         perror("Erreur lors de la lecture de la longueur");
         exit(EXIT_FAILURE);
      }
      char hostname[length];
      if (read(client_fd, hostname, length) == -1) {
         perror("Erreur lors de la lecture du nom d'hôte");
         exit(EXIT_FAILURE);
      }
      printf("Nom de la machine client : %s\n", hostname);




         
      /* On recupere le pid du processus distant  (optionnel)*/
      
      /* On recupere le numero de port de la socket */
      /* d'ecoute des processus distants */
         /* cf code de dsmwrap.c */ 
     }

     /***********************************************************/ 
     /********** ATTENTION : LE PROTOCOLE D'ECHANGE *************/
     /********** DECRIT CI-DESSOUS NE DOIT PAS ETRE *************/
     /********** MODIFIE, NI DEPLACE DANS LE CODE   *************/
     /***********************************************************/
     
     /* 1- envoi du nombre de processus aux processus dsm*/
     /* On envoie cette information sous la forme d'un ENTIER */
     /* (IE PAS UNE CHAINE DE CARACTERES */
     
     /* 2- envoi des rangs aux processus dsm */
     /* chaque processus distant ne reçoit QUE SON numéro de rang */
     /* On envoie cette information sous la forme d'un ENTIER */
     /* (IE PAS UNE CHAINE DE CARACTERES */
     
     /* 3- envoi des infos de connexion aux processus */
     /* Chaque processus distant doit recevoir un nombre de */
     /* structures de type dsm_proc_conn_t égal au nombre TOTAL de */
     /* processus distants, ce qui signifie qu'un processus */
     /* distant recevra ses propres infos de connexion */
     /* (qu'il n'utilisera pas, nous sommes bien d'accords). */

     /***********************************************************/
     /********** FIN DU PROTOCOLE D'ECHANGE DES DONNEES *********/
     /********** ENTRE DSMEXEC ET LES PROCESSUS DISTANTS ********/
     /***********************************************************/
     
     /* gestion des E/S : on recupere les caracteres */
     /* sur les tubes de redirection de stdout/stderr */     
     /* while(1)
         {
            je recupere les infos sur les tubes de redirection
            jusqu'à ce qu'ils soient inactifs (ie fermes par les
            processus dsm ecrivains de l'autre cote ...)
       
         };
      */

     /* on attend les processus fils */
     
     /* on ferme les descripteurs proprement */
     
     /* on ferme la socket d'ecoute */
     
      for (i = 0; i < num_procs; i++) {
         if (client_fds[i] != -1) {
            close(client_fds[i]);
         }
      }
      close(listen_fd);
      
      printf("Free (%d) :\n", num_procs);
      
      nettoyage((char **)machines,num_procs);
  }   
   exit(EXIT_SUCCESS);  
}
