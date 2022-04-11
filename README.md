# BE RESEAU

# Pourquoi faire un protocole pour transporter de la video ?
On veut tolérer un certain pourcentage de pertes et donc on sera capables de transporter la video avec un débit de donnes inferieur offert par le réseau.

# Important 
Si, au lancement du programme, loss_count se remplit de 1 à l'envoi des messages, relancer le programme (problème dans mic_tcp_core, probablement avec random) 

# Remarque 
Les versions à partir de v2 ont été testées majoritairement avec tsock_texte, pour des raisons de praticité (lenteur de la vidéo avant de pouvoir vérifier le fonctionnement), ainsi qu'en raison du mauvais fonctionnement de VLC dans le cas d'une perte de paquets. 

