#include <mictcp.h>
#include <api/mictcp_core.h>



#define TIMEOUT 100
#define LOSS 50 // loss percentage in network
#define TAILLE_FENETRE 5 
#define ACCEPTABLE_LOSS 1 //nbs de messages perdus acceptés sur un total de taille_fenêtre 

#define NB_RENVOIS 5 // nb de tentatives d'envoi du message avant abandon 


// variable globale socket initialisée avec valeurs par défaut 
mic_tcp_sock sk = {-1,-1,{NULL,0,0}}; 


int taille_fenetre=TAILLE_FENETRE; 
int acceptable_loss=ACCEPTABLE_LOSS; 

pthread_cond_t cond_accept = PTHREAD_COND_INITIALIZER; 
pthread_mutex_t mutex_accept = PTHREAD_MUTEX_INITIALIZER; 

mic_tcp_sock_addr dest; 

// ---------------- gestion de l'asynchronisme en envoi --------------------------

pthread_mutex_t mutex_send = PTHREAD_MUTEX_INITIALIZER; 

buffer_send buffer_envoi = {0,NULL,NULL};  

int mic_tcp_sync_send(char* mesg, int mesg_size);
void* thread_envoi(void* args);

//------------------------------------------------------------------------------------


/*
 * Permet de créer un socket entre l’application et MIC-TCP
 * Retourne le descripteur du socket ou bien -1 en cas d'erreur
 */
int mic_tcp_socket(start_mode sm)
{
    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
    if (initialize_components(sm)==-1) return -1; /* Appel obligatoire */
    set_loss_rate(LOSS);
    // return un descripteur de socket - variable globale ou statique avec state et sd mises à jour 
    sk.fd=42; 
    sk.state=SK_CREATED; 
    return sk.fd;
}

/*
 * Permet d’attribuer une adresse à un socket.
 * Retourne 0 si succès, et -1 en cas d’échec
 */
int mic_tcp_bind(int socket, mic_tcp_sock_addr addr) // V1   
{
    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
    if (!(sk.fd == socket && sk.state==SK_CREATED)){
        return -1; 
    }
    sk.addr=addr; 
    sk.state=BINDED; 
    return 0; 
    
}

/*
 * Met le socket en état d'acceptation de connexions
 * Retourne 0 si succès, -1 si erreur
 */
int mic_tcp_accept(int socket, mic_tcp_sock_addr* addr) // V4
{
    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
    pthread_cond_wait(&cond_accept,&mutex_accept); 
    sk.state=ESTABLISHED; 

    return 0; // impossible d'échouer, si pas de connexion on continue juste à attendre 
}

/*
 * Permet de réclamer l’établissement d’une connexion
 * Retourne 0 si la connexion est établie, et -1 en cas d’échec
 */
int mic_tcp_connect(int socket, mic_tcp_sock_addr addr) // V4
{
    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
    mic_tcp_pdu pdu_syn = {
    .header = {
            .source_port=sk.addr.port, 
            .dest_port=addr.port,
            .seq_num=0,
            .ack_num=0,
            .ack= 0,
            .syn=1,
            .fin=0,
            .taille_fenetre=TAILLE_FENETRE, 
            .acceptable_loss=ACCEPTABLE_LOSS
        }
    }; 
    IP_send(pdu_syn,addr); 
    mic_tcp_pdu pdu_syn_ack; 
    int attente = IP_recv(&pdu_syn_ack,&addr,TIMEOUT); 
    // si timeout expiré ou que le pdu reçu n'est pas un syn ack, échec 
    if (attente==-1 || !(pdu_syn_ack.header.ack==1 && pdu_syn_ack.header.syn==1)){
        return(-1);  
    }
    taille_fenetre=pdu_syn_ack.header.taille_fenetre; 
    acceptable_loss=pdu_syn_ack.header.acceptable_loss; 
    sk.state=ESTABLISHED; 
    // création du thread pour l'envoi (lecture dans le buffer)
    pthread_t tid; 
    pthread_create(&tid,NULL,thread_envoi,NULL); 
    return (0); 

}



/* Fonctionnement 
    ex : si après un envoi échoué et seq = 1 on a ce tableau : 
    [0,1,0,0,0,0,0...]
    loss_count[1] a été mis à 1 par une perte précédente 
    le compteur lost_mess ne doit pas être modifié car fenêtre glissante 
    ex2 : si après un envoi réussi et seq = 1 on a ce tableau : 
    [0,1,0,0,0,0,0...]
    loss_count[1] a été mis à 1 par une perte précédente 
    le compteur lost_mess doit être décrémenté car fenêtre glissante 
    loss_count[1] doit être mis à 0 
*/
void update_loss(int * lost_mess, char * loss_count, int seq, int is_lost){
    if (is_lost){
        *lost_mess+=!loss_count[seq]; 
        loss_count[seq]=1;
    }else{
        *lost_mess-=loss_count[seq];
        loss_count[seq]=0;
    }
    // print de suivi de la fenêtre glissante 
    printf("lost_mess : %d\n",*lost_mess);
    printf("loss_count : [");
    for (int i=0; i<TAILLE_FENETRE; i++){
        printf("%d ",loss_count[i]); 
    }
    printf("]\n"); 
}



/*
 * Permet de réclamer l’envoi d’une donnée applicative par l'application 
 * Retourne la taille des données passées au buffer, et -1 en cas d'erreur
 */
int mic_tcp_send (int mic_sock, char* mesg, int mesg_size) // v1
{
    // erreur si le socket n'est pas actif 
    if (mic_sock!=sk.fd || sk.state!=ESTABLISHED) return (-1);  

    // création de la nouvelle case à mettre dans le buffer
    case_buffer * nouveau = malloc(sizeof(case_buffer)); 
    nouveau->suivant=NULL; 
    nouveau->size=mesg_size; 
    nouveau->data=malloc(sizeof(char)*mesg_size);
    memcpy(nouveau->data,mesg,mesg_size); 

    // bloquage du mutex 
    pthread_mutex_lock(&mutex_send); 
    if (buffer_envoi.premier==NULL){
        buffer_envoi.premier=nouveau; 
        buffer_envoi.dernier=nouveau; 
    }else{
        buffer_envoi.dernier->suivant = nouveau; 
        buffer_envoi.dernier = nouveau ; 
    }
    buffer_envoi.taille++; 
    pthread_mutex_unlock(&mutex_send); 
    return mesg_size; 
}


// thread qui appelle en boucle mic_tcp_send si il y a des données dans le buffer 
void * thread_envoi(void * args){
    while(1){
        // bloquage du mutex pour l'accès au buffer 
        pthread_mutex_lock(&mutex_send); 

        // rien ne se passe si le buffer est vide 
        if (buffer_envoi.taille==0) {
            pthread_mutex_unlock(&mutex_send);
            usleep(100);
            continue;
        }

        // récupération dans le buffer 
        case_buffer * avant_dernier = buffer_envoi.premier; 
        case_buffer * a_envoyer = buffer_envoi.dernier; 
        for (int i = 0; i < buffer_envoi.taille-2; i++){
            avant_dernier=avant_dernier->suivant; 
        }
        avant_dernier->suivant=NULL; 
        buffer_envoi.taille--; 
        pthread_mutex_unlock(&mutex_send); 

        // envoi des données 
        mic_tcp_sync_send(a_envoyer->data,a_envoyer->size); 
        free(a_envoyer); 

    }

    return NULL; 
}

// fonction d'envoi; prend les données dans le buffer 
int mic_tcp_sync_send(char* mesg, int mesg_size){
    printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");


    // -------------------encapsulation du message ------------------------------------
    static int seq = 0; 
        
    mic_tcp_pdu pdu ={
        .header = {
            .source_port=sk.addr.port, 
            .dest_port=dest.port, 
            .seq_num=seq,
            .ack_num=0,
            .ack= 0,
            .syn=0,
            .fin=0
        },
        .payload = {
            .data=mesg,
            .size=mesg_size
        }
    };


    //-------------------gestion fiabilité partielle-----------------------

    /* tableau statique initialisé à 0 pour le suivi des paquets perdus */ 
    static char loss_count[TAILLE_FENETRE] ;
    // compteur des messages perdus 
    static int lost_mess = 0 ; 

    // ----------------- envoi ----------------------
    int sent = IP_send(pdu,dest); 
    
    // ----------------- attente du ack---------------
    int attente = -1; 
    mic_tcp_pdu pdu_ack ;
    attente = IP_recv(&pdu_ack,&dest,TIMEOUT); 
    // on n'a pas reçu le ack ou ce n'est pas le bon, il faut peut-être renvoyer le message 
    if (attente== -1 || (pdu_ack.header.ack_num)!=(seq+1)){
        // pas besoin de renvoyer le message 
        if (lost_mess<ACCEPTABLE_LOSS){
            update_loss(&lost_mess,loss_count,seq,1);   
            //sent = -1;  
        }else{ // renvoi du message 
            int tentatives = 0 ; 
            while ( (attente == -1) && !((pdu_ack.header.ack_num)==(pdu.header.seq_num+1)) && tentatives < NB_RENVOIS) {
                sent = IP_send(pdu,dest);
                attente = IP_recv(&pdu_ack,&dest,TIMEOUT);
                tentatives++; 
            }
            if (attente==-1) {
                update_loss(&lost_mess,loss_count,seq,1); 
            }else{
                update_loss(&lost_mess,loss_count,seq,0);
            }
        }
    }else{
        update_loss(&lost_mess,loss_count,seq,0); 
    }
    seq=(seq+1)%TAILLE_FENETRE;
    return sent;
}



/*
 * Permet à l’application réceptrice de réclamer la récupération d’une donnée
 * stockée dans les buffers de réception du socket
 * Retourne le nombre d’octets lu ou bien -1 en cas d’erreur
 * NB : cette fonction fait appel à la fonction app_buffer_get()
 */
int mic_tcp_recv (int socket, char* mesg, int max_mesg_size) // v1
{

    mic_tcp_payload app_buff; 
    app_buff.data=mesg; 
    app_buff.size=max_mesg_size;
    printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");
    int res = app_buffer_get(app_buff); 


    return res;
}

/*
 * Permet de réclamer la destruction d’un socket.
 * Engendre la fermeture de la connexion suivant le modèle de TCP.
 * Retourne 0 si tout se passe bien et -1 en cas d'erreur
 */
int mic_tcp_close (int socket)
{
    printf("[MIC-TCP] Appel de la fonction :  "); printf(__FUNCTION__); printf("\n");
    return -1;
}

/*
 * Traitement d’un PDU MIC-TCP reçu (mise à jour des numéros de séquence
 * et d'acquittement, etc.) puis insère les données utiles du PDU dans
 * le buffer de réception du socket. Cette fonction utilise la fonction
 * app_buffer_put().
 */
void process_received_PDU(mic_tcp_pdu pdu, mic_tcp_sock_addr addr)
{
    printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");
 
    static int ack = 0;
    
    // gestion du accept 
    if (pdu.header.syn==1){
        mic_tcp_pdu pdu_syn_ack = {
            .header= {
                .syn = 1, 
                .ack =1, 
                .acceptable_loss=ACCEPTABLE_LOSS,
                .taille_fenetre = TAILLE_FENETRE
            }
        };
        dest=addr; 
        IP_send(pdu_syn_ack,dest); 
        pthread_cond_broadcast(&cond_accept); 
    }

    // creation du message d'ACK
    mic_tcp_pdu new_pdu = {
        .header = {
            .source_port = pdu.header.dest_port,
            .dest_port = pdu.header.source_port,
            .seq_num = 0,
            .ack_num = ack,
            .syn = 0,
            .ack = 1,
            .fin = 0
        },
        .payload = {
            .data = NULL,
            .size = 0
        }
    };

    if ( pdu.header.seq_num == ack ) {
        ack=(ack+1)%TAILLE_FENETRE;
        new_pdu.header.ack_num = ack;
        app_buffer_put(pdu.payload);
    }else if (pdu.header.seq_num>ack){ // cas où un message a été perdu à l'envoi (non reçu)
        ack = (pdu.header.seq_num+1)%TAILLE_FENETRE ; // re synchronisation du ack 
        new_pdu.header.ack_num = ack;
        app_buffer_put(pdu.payload);
    }

    IP_send(new_pdu,dest);
}
