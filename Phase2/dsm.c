#include "dsm_impl.h"

#define TEST_MSG "HELLO"
#define TEST_MSG_LEN 5

int DSM_NODE_NUM; /* nombre de processus dsm */
int DSM_NODE_ID;  /* rang (= numero) du processus */ 

static dsm_proc_conn_t *procs = NULL;
static dsm_page_info_t table_page[PAGE_NUMBER];
static pthread_t comm_daemon;
pthread_mutex_t page_mutex;


/* indique l'adresse de debut de la page de numero numpage */
static char *num2address( int numpage )
{ 
   char *pointer = (char *)(BASE_ADDR+(numpage*(PAGE_SIZE)));
   
   if( pointer >= (char *)TOP_ADDR ){
      fprintf(stderr,"[%i] Invalid address !\n", DSM_NODE_ID);
      return NULL;
   }
   else return pointer;
}

/* cette fonction permet de recuperer un numero de page */
/* a partir  d'une adresse  quelconque */
static int address2num( char *addr )
{
  return (((intptr_t)(addr - BASE_ADDR))/(PAGE_SIZE));
}

/* cette fonction permet de recuperer l'adresse d'une page */
/* a partir d'une adresse quelconque (dans la page)        */
static char *address2pgaddr( char *addr )
{
  return  (char *)(((intptr_t) addr) & ~(PAGE_SIZE-1)); 
}

/* fonctions pouvant etre utiles */
static void dsm_change_info( int numpage, dsm_page_state_t state, dsm_page_owner_t owner)
{
   if ((numpage >= 0) && (numpage < PAGE_NUMBER)) {	
      if (state != NO_CHANGE )
         table_page[numpage].status = state;
      if (owner >= 0 )
         table_page[numpage].owner = owner;
         return;
   }
   else {
	fprintf(stderr,"[%i] Invalid page number !\n", DSM_NODE_ID);
      return;
   }
}

static dsm_page_owner_t get_owner( int numpage)
{
   return table_page[numpage].owner;
}

static dsm_page_state_t get_status( int numpage)
{
   return table_page[numpage].status;
}

/* Allocation d'une nouvelle page */
static void dsm_alloc_page( int numpage )
{
   char *page_addr = num2address( numpage );
   mmap(page_addr, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
   return ;
}

/* Changement de la protection d'une page */
static void dsm_protect_page( int numpage , int prot)
{
   char *page_addr = num2address( numpage );
   mprotect(page_addr, PAGE_SIZE, prot);
   return;
}

static void dsm_free_page( int numpage )
{
   char *page_addr = num2address( numpage );
   munmap(page_addr, PAGE_SIZE);
   return;
}

static int dsm_send(int dest,void *buf,size_t size)
{
   /* a completer */
   ssize_t bytes_sent = 0;
   while (bytes_sent < size) {
      ssize_t ret = write(dest, buf + bytes_sent, size - bytes_sent);
      if (ret == -1) {
         perror("Erreur lors de l'envoi des données");
         return -1;
      }
      bytes_sent += ret;
   }
   return 0;
}

static int dsm_recv(int from,void *buf,size_t size)
{
   /* a completer */
   ssize_t bytes_received = 0;
   while (bytes_received < size) {
      ssize_t ret = read(from, buf + bytes_received, size - bytes_received);
      if (ret == -1) {
            perror("Erreur lors de la réception des données");
            return -1;
      } else if (ret == 0) {
            fprintf(stderr, "Connexion fermée par le processus distant\n");
            return -1;
      }
      bytes_received += ret;
   }
  return 0;
}


static void *dsm_comm_daemon( void *arg)
{
   struct pollfd poll_fds[DSM_NODE_NUM]; // Tableau de pollfd pour surveiller plusieurs sockets
   int i;

   // Initialisation des descripteurs pour poll
   for (i = 0; i < DSM_NODE_NUM; i++)
   {
      poll_fds[i].fd = procs[i].fd;     
      poll_fds[i].events = POLLIN;     
      poll_fds[i].revents = 0;
   }
   while (1)
   {
      printf("[%i] Waiting for incoming requests...\n", DSM_NODE_ID);

      int ret = poll(poll_fds, DSM_NODE_NUM, -1); 

      if (ret > 0){
         for (i = 0; i < DSM_NODE_NUM; i++)
         {
            if (poll_fds[i].revents & POLLIN)
            {
               dsm_req_t request;
               if (dsm_recv(poll_fds[i].fd, &request, sizeof(request)) == 0)
               {
                  pthread_mutex_unlock(&page_mutex);
                  printf("[%i] Received a request from process %d\n", DSM_NODE_ID, request.source);

                  // Gérer les actions selon le type de requête
                  switch (request.type)
                  {
                     case DSM_REQ: // Requête pour une page
                     {
                        printf("[%i] Processing DSM_REQ for page %d\n", DSM_NODE_ID, request.page_num);
                        if (get_owner(request.page_num) == DSM_NODE_ID)
                        {
                           void *page_addr = num2address(request.page_num);
                           if (dsm_send(poll_fds[i].fd, page_addr, PAGE_SIZE) == -1)
                           {
                              perror("Error sending page");
                           }
                           else
                           {
                              printf("[%i] Sent page %d to process %d\n", DSM_NODE_ID, request.page_num, request.source);
                              pthread_mutex_unlock(&page_mutex);
                           }
                        }
                        else
                        {
                           // Transférer la requête au propriétaire actuel
                           dsm_page_owner_t owner = get_owner(request.page_num);
                           printf("[%i] Forwarding request for page %d to process %d\n", DSM_NODE_ID, request.page_num, owner);

                           if (dsm_send(procs[owner].fd, &request, sizeof(request)) == -1)
                           {
                              perror("Error forwarding request");
                           }
                        }
                        break;
                     }

                     case DSM_PAGE: // Notification de page mise à jour
                     {
                        printf("[%i] Received DSM_PAGE update for page %d\n", DSM_NODE_ID, request.page_num);
                        // Mettre à jour
                        dsm_change_info(request.page_num, READ_ONLY, request.source);
                        dsm_protect_page(request.page_num, PROT_READ);
                        break;
                     }

                     default: // Requête inconnue
                        printf("[%i] Unknown request type: %d\n", DSM_NODE_ID, request.type);
                        break;
                  }
               }
               else
               {
                     fprintf(stderr, "[%i] Error receiving request from process %d\n", DSM_NODE_ID, i);
               }
            }
         }
      }
        else if (ret == -1)
        {
            perror("Error in poll");
        }

      /**/
      printf("[%i] Waiting for incoming reqs \n", DSM_NODE_ID);
      sleep(2);
      /**/

   }

   return NULL;
}




static void dsm_handler( int * addr )
{  
   /* A modifier */
   // Il nous faut la page qui pose probleme => le proprietaire => stocker ca dans une variable de type dsm_req_t => envoyer 
   // Obtenir l'adresse fautive à partir de SIGSEGV
   int numpage = address2num((char *)addr);  

   if (numpage < 0 || numpage >= PAGE_NUMBER) {
      fprintf(stderr, "[%i] Adresse invalide détectée: %p\n", DSM_NODE_ID, addr);
      abort();  // Adresse hors plage gérée par DSM
   }

   // Identifier le propriétaire actuel de la page
   dsm_page_owner_t owner = get_owner(numpage);
   if (owner == DSM_NODE_ID) {
      fprintf(stderr, "[%i] Erreur: Accès invalide sur une page que je possède\n", DSM_NODE_ID);
      abort();  // Ne devrait pas arriver
   }

   printf("[%i] Demande de la page %d au processus %d\n", DSM_NODE_ID, numpage, owner);

   // Préparer une requête DSM_REQ
   dsm_req_t request;
   request.source = DSM_NODE_ID;
   request.page_num = numpage;
   request.type = DSM_REQ;  // Définir le type de requête

   // Utiliser un mutex pour éviter les conflits d'accès
   pthread_mutex_lock(&page_mutex);

   // Envoyer la requête au propriétaire
   if (dsm_send(procs[owner].fd, &request, sizeof(request)) == -1) {
      perror("Erreur lors de l'envoi de la requête DSM_REQ");
      abort();
   }

   pthread_mutex_lock(&page_mutex);

   // Recevoir la page du propriétaire
   void *page_addr = num2address(numpage);
   if (dsm_recv(procs[owner].fd, page_addr, PAGE_SIZE) == -1) {
      perror("Erreur lors de la réception de la page");
      pthread_mutex_unlock(&page_mutex);
      abort();
   }

   // Mettre à jour la table des pages
   dsm_change_info(numpage, READ_ONLY, DSM_NODE_ID);

   // Modifier les permissions de la page pour permettre l'accès en lecture
   dsm_protect_page(numpage, PROT_READ);

   //pthread_mutex_unlock(&page_mutex);

   printf("[%i] Page %d reçue et mise à jour\n", DSM_NODE_ID, numpage);

   // Informer les autres processus pour qu'ils actualisent leurs tables
   for (int i = 0; i < DSM_NODE_NUM; i++) {
      if (i != DSM_NODE_ID) {
         dsm_req_t update_request;
         update_request.source = DSM_NODE_ID;
         update_request.page_num = numpage;
         update_request.type = DSM_PAGE;  // Indiquer la fin de la mise à jour

         if (dsm_send(procs[i].fd, &update_request, sizeof(update_request)) == -1) {
            perror("Erreur lors de l'envoi de la mise à jour DSM_FINALIZE");
         }
      }
   }


   printf("[%i] FAULTY  ACCESS !!! \n",DSM_NODE_ID);
   abort();
}

/* traitant de signal adequat */
static void segv_handler(int sig, siginfo_t *info, void *context)
{
   /* A completer */
   /* adresse qui a provoque une erreur */
   void  *addr = info->si_addr;   
  /* Si ceci ne fonctionne pas, utiliser a la place :*/
  /*
   #ifdef __x86_64__
   void *addr = (void *)(context->uc_mcontext.gregs[REG_CR2]);
   #elif __i386__
   void *addr = (void *)(context->uc_mcontext.cr2);
   #else
   void  addr = info->si_addr;
   #endif
   */
   /*
   pour plus tard (question ++):
   dsm_access_t access  = (((ucontext_t *)context)->uc_mcontext.gregs[REG_ERR] & 2) ? WRITE_ACCESS : READ_ACCESS;   
  */   
   /* adresse de la page dont fait partie l'adresse qui a provoque la faute */
   void  *page_addr  = (void *)(((unsigned long) addr) & ~(PAGE_SIZE-1));

   if ((addr >= (void *)BASE_ADDR) && (addr < (void *)TOP_ADDR))
     {
	dsm_handler(addr);
     }
   else
     {
	/* SIGSEGV normal : ne rien faire*/
     }
}

/* Seules ces deux dernieres fonctions sont visibles et utilisables */
/* dans les programmes utilisateurs de la DSM                       */
char *dsm_init(int argc, char *argv[])
{   
   struct sigaction act;
   int index;  
   pthread_mutex_init(&page_mutex, NULL); 

   /* Récupération de la valeur des variables d'environnement */
   /* DSMEXEC_FD et MASTER_FD                                 */
   char *dsmexec_fd_str = getenv("DSMEXEC_FD");
   char *master_fd_str = getenv("MASTER_FD");

   if (dsmexec_fd_str == NULL || master_fd_str == NULL) {
      fprintf(stderr, "Erreur : les variables d'environnement DSMEXEC_FD ou MASTER_FD ne sont pas définies.\n");
      exit(EXIT_FAILURE);
   }

   /* Conversion des chaînes en entiers */
   errno = 0;
   int DSMEXEC_FD = atoi(dsmexec_fd_str);
   int MASTER_FD = atoi(master_fd_str);
   
   /* Affichage pour vérifier */
   printf("DSMEXEC_FD = %d\n", DSMEXEC_FD);
   printf("MASTER_FD = %d\n", MASTER_FD);

   /* reception du nombre de processus dsm envoye */
   /* par le lanceur de programmes (DSM_NODE_NUM) */
   if (read(DSMEXEC_FD, &DSM_NODE_NUM, sizeof(int)) == -1) {
         perror("Erreur lors de la reception du nombre de processus dsm envoye");
         exit(EXIT_FAILURE);
      }

   /* reception de mon numero de processus dsm envoye */
   /* par le lanceur de programmes (DSM_NODE_ID)      */
   if (read(DSMEXEC_FD, &DSM_NODE_ID, sizeof(int)) == -1) {
         perror("Erreur lors de la reception de mon numero de processus dsm envoye");
         exit(EXIT_FAILURE);
      }

   // Test
   printf("Valeur de DSM_NODE_NUM %d\n",DSM_NODE_NUM);
   printf("Valeur de DSM_NODE_ID %d\n",DSM_NODE_ID);
   
   /* reception des informations de connexion des autres */
   /* processus envoyees par le lanceur :                */
   /* nom de machine, numero de port, etc.               */
   // Allocation du tableau de connexions 
   procs = malloc(DSM_NODE_NUM * sizeof(dsm_proc_conn_t));
   if (procs == NULL) {
      perror("Erreur d'allocation mémoire pour les processus distants");
      exit(EXIT_FAILURE);
   }

   // Réception des informations des processus distants
   for (int i = 0; i < DSM_NODE_NUM; i++) {
      struct dsm_proc_conn proc;

      // Lecture d'une structure complète en une seule fois
      size_t size = sizeof(struct dsm_proc_conn);
      size_t received = 0;
      while (received < size) {
         ssize_t ret = read(DSMEXEC_FD, ((char *)&proc) + received, size - received);
         if (ret > 0) {
               received += ret;
         } else if (ret == 0) {
               fprintf(stderr, "Erreur : la connexion a été fermée de manière inattendue.\n");
               exit(EXIT_FAILURE);
         } else {
               perror("Erreur lors de la lecture");
               exit(EXIT_FAILURE);
         }
      }

    // Copie des données dans la structure procs[i]
    procs[i] = proc;
   }
   /* Vérification : Affichage des informations reçues */
   for (int i = 0; i < DSM_NODE_NUM; i++) {
      
      printf("=====================Processus %d : Machine = %s, Port = %d, Fd = %d, Fd_for_exit = %d\n", 
            procs[i].rank, procs[i].machine, procs[i].port_num,procs[i].fd,procs[i].fd_for_exit);
   }

   /* initialisation des connexions              */ 
   /* avec les autres processus : connect/accept */
   if (listen(MASTER_FD, DSM_NODE_NUM) == -1) {
      perror("Erreur lors de l'appel à listen");
      exit(EXIT_FAILURE);
   }

   for (int i = 0; i < DSM_NODE_NUM; i++) {
      if (procs[i].rank > DSM_NODE_ID) {
         // Résolution du nom de machine en adresse IP
         struct hostent *host = gethostbyname(procs[i].machine);
         if (host == NULL) {
               fprintf(stderr, "Erreur lors de la résolution DNS pour %s\n", procs[i].machine);
               exit(EXIT_FAILURE);
         }

         // Création de la structure sockaddr_in
         struct sockaddr_in addr;
         memset(&addr, 0, sizeof(addr));
         addr.sin_family = AF_INET;
         addr.sin_port = htons(procs[i].port_num);
         memcpy(&addr.sin_addr, host->h_addr_list[0], host->h_length);

         // Création d'une socket pour se connecter au processus supérieur
         int client_fd = socket(AF_INET, SOCK_STREAM, 0);
         if (client_fd == -1) {
               perror("Erreur lors de la création de la socket client");
               exit(EXIT_FAILURE);
         }

         // Connexion à la socket distante
         while(connect(client_fd, (struct sockaddr *)&addr, sizeof(addr)) == ECONNREFUSED){
            sleep(1);  // Temporisation avant une nouvelle tentative
         }

         // Stocker le descripteur de la connexion
         procs[i].fd = client_fd;

         // Envoi d'un message de test après connexion
         if (write(procs[i].fd, TEST_MSG, TEST_MSG_LEN) == -1) {
               perror("Erreur lors de l'envoi du message de test");
               exit(EXIT_FAILURE);
         }
         
        printf("Message de test envoyé au processus de rang %d\n", procs[i].rank);
      } else if (procs[i].rank < DSM_NODE_ID) {
         // Acceptation des connexions des processus de rang inférieur
         struct sockaddr_in client_addr;
         socklen_t client_len = sizeof(client_addr);
         int new_fd = accept(MASTER_FD, (struct sockaddr *)&client_addr, &client_len);
         if (new_fd == -1) {
               perror("Erreur lors de l'acceptation de la connexion");
               exit(EXIT_FAILURE);
         }

         // Stocker le descripteur de la connexion
         procs[i].fd = new_fd;
         
         // Réception du message de test après acceptation
         char buffer[TEST_MSG_LEN + 1] = {0};
         if (read(procs[i].fd, buffer, TEST_MSG_LEN) == -1) {
               perror("Erreur lors de la réception du message de test");
               exit(EXIT_FAILURE);
         }

         // Vérification du message
         if (strcmp(buffer, TEST_MSG) == 0) {
               printf("Message de test reçu du processus de rang %d : %s\n", procs[i].rank, buffer);
         } else {
               fprintf(stderr, "Message de test incorrect reçu : %s\n", buffer);
               exit(EXIT_FAILURE);
            }
      }
   }
   



   /* Allocation des pages en tourniquet */
   for(index = 0; index < PAGE_NUMBER; index ++){	
     if ((index % DSM_NODE_NUM) == DSM_NODE_ID)
       dsm_alloc_page(index);	     
     dsm_change_info( index, WRITE, index % DSM_NODE_NUM);
   }
   
   /* mise en place du traitant de SIGSEGV */
   act.sa_flags = SA_SIGINFO; 
   act.sa_sigaction = segv_handler;
   sigaction(SIGSEGV, &act, NULL);
   
   /* creation du thread de communication           */
   /* ce thread va attendre et traiter les requetes */
   /* des autres processus                          */
   pthread_create(&comm_daemon, NULL, dsm_comm_daemon, NULL);
   pthread_mutex_destroy(&page_mutex);
   
   /* Adresse de début de la zone de mémoire partagée */
   return ((char *)BASE_ADDR);
}

void dsm_finalize( void )
{
   /* fermer proprement les connexions avec les autres processus */

   /* terminer correctement le thread de communication */
   /* pour le moment, on peut faire :                  */
   pthread_cancel(comm_daemon);
   
  return;
}

//Attention finalize pas trivial (cancel à ne pas garder) il faut bien réfléchir à son implication pour lui et les autres 
