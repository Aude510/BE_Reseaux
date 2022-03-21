#include <mictcp.h>
#include <api/mictcp_core.h>


#define TIMEOUT 100
#define LOSS 50 // loss percentage in network
#define TAILLE_FENETRE 100 // paramètres à faire évoluer 
#define ACCEPTABLE_LOSS 5

/*
 * Permet de créer un socket entre l’application et MIC-TCP
 * Retourne le descripteur du socket ou bien -1 en cas d'erreur
 */
int mic_tcp_socket(start_mode sm)
{
   int result = -1;
   printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
   result = initialize_components(sm); /* Appel obligatoire */
   set_loss_rate(LOSS);

   return result;
}

/*
 * Permet d’attribuer une adresse à un socket.
 * Retourne 0 si succès, et -1 en cas d’échec
 */
int mic_tcp_bind(int socket, mic_tcp_sock_addr addr) // V1   
{
    // TODO qu'est ce qu'il faut faire là ?
   printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
   /*mic_tcp_sock sock; 
   sock.fd=socket; 
   sock.addr=addr; */ 
   return 0; 
}

/*
 * Met le socket en état d'acceptation de connexions
 * Retourne 0 si succès, -1 si erreur
 */
int mic_tcp_accept(int socket, mic_tcp_sock_addr* addr) // V4
{
    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
    return 0; // todo faire la fonction (ou remettre retour à -1?)
}

/*
 * Permet de réclamer l’établissement d’une connexion
 * Retourne 0 si la connexion est établie, et -1 en cas d’échec
 */
int mic_tcp_connect(int socket, mic_tcp_sock_addr addr) // V4
{
    printf("[MIC-TCP] Appel de la fonction: ");  printf(__FUNCTION__); printf("\n");
    return 0; // todo faire la fonction (ou remettre retour à -1?)
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
    

    /* tableau statique initialisé à 0 pour le suivi des paquets perdus */ 
//    static char loss_count[TAILLE_FENETRE] ;

    mic_tcp_sock_addr dest; // TODO trouver la vraie adresse à envoyer 
    mic_tcp_pdu pdu ={
        .header = {
            .source_port=1234, // TODO trouver le vrai source port 
            .dest_port=dest.port, // TODO no leer memoria random 
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

    

    // ----------------- envoi ----------------------
    int sent = IP_send(pdu,dest); 
    // ----------------- attente du ack---------------
    int attente = -1; 
    mic_tcp_pdu pdu_ack ;
    attente = IP_recv(&pdu_ack,&dest,TIMEOUT); 

    // v3 : ne rentrer dans le while que si on est hors du % de pertes acceptables 
    while ( (attente == -1) && !((pdu_ack.header.ack_num)%2==(pdu.header.seq_num+1)%2 )) {
        sent = IP_send(pdu,dest);
        usleep(TIMEOUT);
        attente = IP_recv(&pdu_ack,&dest,TIMEOUT);
    }
    printf("numéro d'ack reçu : %d, numéro de séquence : %d\n",pdu_ack.header.ack_num,seq);
    seq++;
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
    mic_tcp_sock_addr dest;


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
    printf("received message with seq = %d\n",pdu.header.seq_num);

    if ( pdu.header.seq_num%2 == ack%2 ) {
        ack+=1;
        new_pdu.header.ack_num = ack;
        app_buffer_put(pdu.payload);
    }
// avec le printf : ça marche avec tsock_texte
// sans le printf, même avec le sleep, ça marche pas : perte du paquet en boucle/ack non reçus? 
    printf("sending ack with ack_num = %d\n",new_pdu.header.ack_num);

    int res = IP_send(new_pdu,dest);
}
