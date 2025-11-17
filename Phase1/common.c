#include "common_impl.h"

#define PORT 0 // Le systeme choisira un port disponible

/* Vous pouvez ecrire ici toutes les fonctions */
/* qui pourraient etre utilisees par le lanceur */
/* et le processus intermediaire. N'oubliez pas */
/* de declarer le prototype de ces nouvelles */
/* fonctions dans common_impl.h */

// Fonction pour vérifier si une ligne est vide (ne contient que des espaces ou un retour à la ligne)
int ligne_vide(const char *ligne) {
    for (int i = 0; i < strlen(ligne); i++) {
        if (!isspace((unsigned char)ligne[i])) {
            return 0;  // La ligne contient un caractère non-espace
        }
    }
    return 1;  // La ligne est vide ou ne contient que des espaces
}

// Fonction pour lire le fichier de machines
int lire_machinefile(const char *filename, char ***machines) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        perror("Erreur d'ouverture du fichier machinefile");
        exit(EXIT_FAILURE);
    }

    int num_machines = 0;
    char buffer[MAX_STR];

    // Compter le nombre de machines en ignorant les lignes vides
    while (fgets(buffer, MAX_STR, file) != NULL) {
        // Enlever le retour à la ligne
        buffer[strcspn(buffer, "\n")] = '\0';

        if (!ligne_vide(buffer)) {
            num_machines++;
        }
    }

    // Revenir au début du fichier
    fseek(file, 0, SEEK_SET);

    // Allouer de la mémoire pour stocker les noms des machines
    *machines = malloc(num_machines * sizeof(char *));
    if (*machines == NULL) {
        perror("Erreur d'allocation mémoire pour les machines");
        exit(EXIT_FAILURE);
    }

    // Lecture des noms de machines en ignorant les lignes vides
    int index = 0;
    while (fgets(buffer, MAX_STR, file) != NULL) {
        // Enlever le retour à la ligne
        buffer[strcspn(buffer, "\n")] = '\0';

        if (!ligne_vide(buffer)) {
            // Allouer de la mémoire pour chaque nom de machine
            (*machines)[index] = malloc((strlen(buffer) + 1) * sizeof(char));
            if ((*machines)[index] == NULL) {
                perror("Erreur d'allocation mémoire pour un nom de machine");
                exit(EXIT_FAILURE);
            }
            strcpy((*machines)[index], buffer);
            index++;
        }
    }

    fclose(file);
    return num_machines;
}


// Creation socket d'écoute
int create_listen_socket(int nbr_proc, char *hostname, int *port) {
    int listen_fd;
    struct sockaddr_in server_addr;
    socklen_t addr_len = sizeof(server_addr);

    // Résolution du nom de la machine en adresse IP
    struct hostent *host = gethostbyname(hostname);
    if (!host) {
        fprintf(stderr, "Erreur : impossible de résoudre le nom de machine %s\n", hostname);
        exit(EXIT_FAILURE);
    }

    // Vérification du type d'adresse IP
    if (host->h_addr_list[0] == NULL) {
        fprintf(stderr, "Erreur : aucune adresse IP disponible pour %s\n", hostname);
        exit(EXIT_FAILURE);
    }

    // Création de la socket
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) {
        perror("Erreur lors de la création de la socket");
        exit(EXIT_FAILURE);
    }

    // Initialisation de la structure serveur
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(0);  // Laisser le système choisir un port libre

    // Copier la première adresse IP dans la structure server_addr
    memcpy(&server_addr.sin_addr, host->h_addr_list[0], host->h_length);

    // Associer la socket à une adresse et un port
    if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Erreur lors de l'association (bind)");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    // Récupérer le port choisi dynamiquement
    if (getsockname(listen_fd, (struct sockaddr *)&server_addr, &addr_len) == -1) {
        perror("Erreur lors de getsockname");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }
    *port = ntohs(server_addr.sin_port);

    // Afficher l'adresse et le port
    printf("Socket d'écoute créée sur %s:%d\n", hostname, *port);

    // Mettre la socket en mode écoute
    if (listen(listen_fd, nbr_proc) == -1) {
        perror("Erreur lors du listen");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    return listen_fd;
}


// Libération des ressources
void nettoyage(char **machines, int num_machines) 
{
    for (int i = 0; i < num_machines; i++) 
    {
        printf("%s\n", machines[i]);
        free(machines[i]);
    }
    free(machines);
}

