#include <mictcp.h>
#include <api/mictcp_core.h>

// TODO : faire valider v3 par prof et passer à v4.2
// v3 marche bien avec texte, video marche pas même avec un pourcent de perte... 

#define TIMEOUT 100
#define LOSS 50 // loss percentage in network
#define TAILLE_FENETRE 5 // paramètres à faire évoluer 
#define ACCEPTABLE_LOSS 1 //nbs de messages perdus acceptés sur un total de taille_fenêtre 
//TRY changer nb_renvois pour un nb plus petit 

#define NB_RENVOIS 5 // nb de tentatives d'envoi du message avant abandon 

// TODO : enlever les prints de debug et autres trucs inutiles 
// variable globale socket initialisée avec valeurs par défaut 
mic_tcp_sock sk = {-1,-1,{NULL,0,0}}; 


int taille_fenetre=TAILLE_FENETRE; 
int acceptable_loss=ACCEPTABLE_LOSS; 

pthread_cond_t cond = PTHREAD_COND_INITIALIZER; 
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; 

mic_tcp_sock_addr dest; 

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
    pthread_cond_wait(&cond,&mutex); 
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
    // si timeout expiré ou pdu reçu n'est pas un syn ack, échec 
    if (attente==-1 || !(pdu_syn_ack.header.ack==1 && pdu_syn_ack.header.syn==1)){
        printf("échec de connexion\n"); 
        return(-1);  
    }
    taille_fenetre=pdu_syn_ack.header.taille_fenetre; 
    acceptable_loss=pdu_syn_ack.header.acceptable_loss; 
    sk.state=ESTABLISHED; 
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
}



/*
 * Permet de réclamer l’envoi d’une donnée applicative
 * Retourne la taille des données envoyées, et -1 en cas d'erreur
 */
int mic_tcp_send (int mic_sock, char* mesg, int mesg_size) // v1
{
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
            if (tentatives == NB_RENVOIS) {
                update_loss(&lost_mess,loss_count,seq,1); 
                //sent = -1; 
            }
        }
    }else{
        update_loss(&lost_mess,loss_count,seq,0); 
    }
    



    // printf("numéro d'ack reçu : %d, numéro de séquence : %d\n",pdu_ack.header.ack_num,seq);
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

    static int ack = 0;
    

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
        pthread_cond_broadcast(&cond); 
    }

    // creation du message d'ACK
    // by default we set the value to ack and then if we receive the message we augment it by 1 to demand the new message
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

    printf("[MIC-TCP] Appel de la fonction: "); printf(__FUNCTION__); printf("\n");
    // printf("received message with seq = %d\n",pdu.header.seq_num);

    if ( pdu.header.seq_num == ack ) {
        ack=(ack+1)%TAILLE_FENETRE;
        new_pdu.header.ack_num = ack;
        app_buffer_put(pdu.payload);
    }else if (pdu.header.seq_num>ack){ // cas où un message a été perdu à l'envoi (non reçu)
        ack = (pdu.header.seq_num+1)%TAILLE_FENETRE ; // re synchronisation du ack 
        new_pdu.header.ack_num = ack;
        app_buffer_put(pdu.payload);
    }
// avec le printf : ça marche avec tsock_texte
// sans le printf, même avec le sleep, ça marche pas : perte du paquet en boucle/ack non reçus? 
    //printf("sending ack with ack_num = %d\n",new_pdu.header.ack_num);

    IP_send(new_pdu,dest);
}
