# teleinfo2020

Téléinfo (TIC) en mode standard envoyé via mqtt

Projet complet (circuit + code) pour recupérer la téléinfo en mode standard sur un linky EDF et l'envoyer via MQTT a jeedom (ou n'importe quelle autre solution)

Pour la carte je me suis largement inspiré de Mr Hallard, pour le code c'est une ré ecriture complète.

Les principales fonctionnalités sont :
* gestion de l'hordatage du mode standard
* gestion d'un cache sur une trentaine d'etquette pour eviter les retransmission incessante
* gestion d'un afficher 7 segments a 2 chiffre pour afficher le pourcentage de puissance instannée (SINSTS/PCOUP)
* envoi de l'adresse IP au demarrage
* suivi et envoi de 3 indicateurs (depassement de buffer de l'uart, erreur de transmission, cache hit)

J'ai mis quelques photos du prototype que j'ai construit.

