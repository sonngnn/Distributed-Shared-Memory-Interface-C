#include "common_impl.h"

int main(int argc, char *argv[])
{   
   /* processus intermediaire pour "nettoyer" */
   fflush(stdout);
   /* la liste des arguments qu'on va passer */
   
   if (argc != 3) 
   {
      fprintf(stderr, "Usage: %s <port>, <adresse>\n", argv[0]);
      exit(EXIT_FAILURE);
   }
   printf("===test===\n");
   /* a la commande a executer finalement  */
   
   /* creation d'une socket pour se connecter au */
   /* au lanceur et envoyer/recevoir les infos */
   /* necessaires pour la phase dsm_init */   
   int sock_fd;
   struct sockaddr_in server_addr;
   char hostname[MAX_STR];
   int pid = getpid();
   int port = atoi(argv[1]);
   char *addr_dsmexec = argv[2];
   
   // Récupération du nom de la machine
   if (gethostname(hostname, MAX_STR) == -1) 
   {
      perror("Erreur récupération nom machine");
      exit(EXIT_FAILURE);
   }
   
   // Résolution du nom de la machine en adresse IP
   struct hostent *host = gethostbyname(addr_dsmexec);
   if (!host) {
      fprintf(stderr, "Erreur : impossible de résoudre le nom de machine %s\n", addr_dsmexec);
      exit(EXIT_FAILURE);
   }
    
   // Création de la socket
   sock_fd = socket(AF_INET, SOCK_STREAM, 0);
   if (sock_fd == -1) 
   {
      perror("Erreur création socket");
      exit(EXIT_FAILURE);
   }

   // Configuration de l'adresse du serveur
   memset(&server_addr, 0, sizeof(server_addr));
   server_addr.sin_family = AF_INET;
   server_addr.sin_port = htons(port);

   // Copier l'adresse IP résolue dans server_addr
   memcpy(&server_addr.sin_addr, host->h_addr_list[0], host->h_length);
   
   // Connexion au serveur dsmexec
   if (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
      perror("Erreur connexion dsmexec");
      close(sock_fd);
      exit(EXIT_FAILURE);
   }
   fprintf(stdout, "Connexion établie avec le serveur %s:%d\n", argv[1], port);


   /* Envoi du nom de machine au lanceur */
   int length = strlen(hostname) + 1;  
      if (write(sock_fd, &length, sizeof(int)) == -1) {
         perror("Erreur lors de l'envoi de la longueur");
         exit(EXIT_FAILURE);
      }

      if (write(sock_fd, hostname, length) == -1) {
         perror("Erreur lors de l'envoi du nom d'hôte");
         exit(EXIT_FAILURE);
      }
   /* Envoi du pid au lanceur (optionnel) */

   /* Creation de la socket d'ecoute pour les */
   /* connexions avec les autres processus dsm */

   /* Envoi du numero de port au lanceur */
   /* pour qu'il le propage à tous les autres */
   /* processus dsm */
 
   /* on execute la bonne commande */

   /* attention au chemin à utiliser ! */

   /************** ATTENTION **************/
   /* vous remarquerez que ce n'est pas   */
   /* ce processus qui récupère son rang, */
   /* ni le nombre de processus           */
   /* ni les informations de connexion    */
   /* (cf protocole dans dsmexec)         */
   /***************************************/
  
   return 0;
}
