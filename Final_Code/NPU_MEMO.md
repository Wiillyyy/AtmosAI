## NPU / Neural-ART : integration et limites rencontrees

Le STM32N657X0 integre un accelerateur materiel **ST Neural-ART**, concu pour executer des reseaux de neurones avec une meilleure efficacite energetique qu'une execution CPU classique.

L'objectif initial du projet etait donc d'executer la prediction meteo H+1 directement sur le NPU de la carte. Le modele a ete integre via la chaine ST/X-CUBE-AI, avec les appels runtime necessaires cote firmware.

Dans le code, la partie NPU a bien ete etudiee et integree jusqu'a l'initialisation :

```c
LL_ATON_RT_RuntimeInit();
stai_network_init(...);
```

Cependant, l'execution reelle de l'inference via le NPU provoquait une erreur bas niveau au moment du lancement du reseau :

```text
Epoch Controller ERROR interrupt: EC_IRQ = 0x00000008
Epoch Controller opcode counter: 0x00000000
Epoch Controller label: 0x00000000
assertion "0" failed: ll_aton_runtime.c, function: __LL_ATON_RT_IrqErr
```

Cette erreur apparait des le debut de l'execution NPU, avant meme que le reseau ne progresse dans ses opcodes. Cela indique que le probleme ne vient probablement pas du calcul du modele lui-meme, mais plutot d'un acces memoire impossible au moment ou le NPU tente de lire le graphe, les poids ou les buffers necessaires.

### Hypothese principale

L'hypothese la plus probable est un probleme de configuration memoire / securite autour du STM32N6.

Sur STM32N6, le NPU n'accede pas a la memoire comme le CPU. Il possede ses propres ports maitres AXI et doit etre autorise a lire les regions memoire contenant :

- le graphe compile,
- les poids du reseau,
- les buffers d'entree/sortie,
- les buffers internes du runtime ATON.

Meme si le CPU peut lire une zone memoire, cela ne garantit pas que le NPU puisse y acceder. Le STM32N6 utilise des mecanismes de protection comme le **RIF/RISAF/RIMC** pour controler les acces memoire selon le master, le CID, le niveau secure/non-secure et les permissions.

L'erreur `EC_IRQ = 0x00000008` est coherente avec un acces bloque tres tot par ce type de configuration.

### Pistes testees

Plusieurs pistes ont ete essayees pour isoler le probleme :

- deplacement des poids NPU entre differentes regions memoire,
- verification du linker script,
- tentative d'eviter les conflits avec l'OCTOFLASH,
- copie des buffers en RAM accessible,
- alignement memoire,
- flush / invalidation cache,
- verification de l'initialisation du runtime ATON,
- tentative de configuration manuelle de registres lies au RIF/RISAF,
- desactivation du lancement effectif NPU pour eviter le crash runtime.

Malgre ces essais, l'erreur Epoch Controller persistait. Cela suggere que la configuration necessaire n'est pas uniquement une correction simple dans le code applicatif, mais depend probablement d'une configuration bas niveau generee proprement par CubeMX/X-CUBE-AI pour le STM32N6.

### Pourquoi ne pas avoir regenere tout le projet CubeMX ?

Une solution possible aurait ete de repartir d'un projet CubeMX neuf avec X-CUBE-AI configure explicitement pour le NPU. Cela aurait probablement genere automatiquement :

- le bon linker script,
- les bonnes zones memoire,
- les bons buffers,
- la configuration RIF/RISAF/RIMC,
- l'initialisation runtime adaptee au STM32N6.

Cependant, le projet final contenait deja une partie applicative importante et fonctionnelle :

- acquisition capteurs,
- Ethernet / DHCP,
- HTTP POST vers le VPS,
- GET de commandes depuis le serveur,
- mode danse,
- inference meteo CPU,
- telemetrie puissance / CPU,
- IMU,
- dashboard web temps reel.

Regenerer entierement le projet CubeMX a la fin du developpement aurait presente un risque eleve de casser la configuration reseau, les middlewares, le code applicatif ou les fichiers modifies manuellement.

Le choix final a donc ete de conserver une version stable, demontrable et complete du projet, avec une inference locale executee sur CPU.

### Choix final : fallback CPU stable

La version finale utilise donc une inference CPU locale pour garantir la stabilite de la demonstration.

Ce choix permet de conserver la chaine complete :

```text
Capteurs STM32
  -> features meteo
  -> inference locale H+1
  -> POST HTTP vers VPS
  -> stockage SQLite
  -> dashboard web temps reel
```

Le NPU a ete etudie, initialise et teste, mais son execution complete n'a pas ete conservee dans la version finale a cause d'un blocage bas niveau memoire / securite qui faisait planter le runtime.

L'objectif du projet etant de livrer une station meteo fonctionnelle de bout en bout, la priorite a ete donnee a une demonstration fiable plutot qu'a une execution NPU instable.

### Etat final

Dans la version finale :

- l'inference meteo est bien executee localement sur la carte,
- le modele CPU fonctionne de maniere stable,
- la carte poste les resultats vers le VPS toutes les 5 secondes,
- le dashboard affiche les mesures et predictions en temps reel,
- la communication serveur vers carte est demontree via le mode danse,
- le NPU n'est pas utilise pour l'inference finale, mais son integration a ete investiguee.

## Questions / Reponses pour l'oral

### Pourquoi vouliez-vous utiliser le NPU ?

Le NPU Neural-ART du STM32N657X0 est concu pour accelerer les reseaux de neurones. L'idee initiale etait donc de deporter l'inference meteo sur cet accelerateur pour reduire la charge CPU et montrer une utilisation avancee du STM32N6.

### Est-ce que le NPU fonctionne dans la version finale ?

Non, pas pour l'inference finale. Le runtime NPU a ete integre et teste, mais l'execution du reseau provoquait une erreur bas niveau `Epoch Controller`. Pour garantir une demonstration stable, la version finale utilise un fallback CPU.

### Est-ce que l'intelligence artificielle tourne quand meme sur la carte ?

Oui. L'inference H+1 est executee localement sur le STM32. Elle n'est pas calculee sur le VPS. Le serveur recoit uniquement les mesures et le resultat via HTTP POST.

### Pourquoi l'erreur NPU apparait-elle ?

L'erreur apparait des le debut de l'execution NPU, avec un compteur d'opcode a zero. Cela indique probablement que le NPU n'arrive pas a lire correctement les donnees necessaires au reseau : poids, graphe ou buffers. Le probleme semble lie aux acces memoire et aux protections materielles du STM32N6.

### Pourquoi le CPU peut lire la memoire mais pas le NPU ?

Le CPU et le NPU ne sont pas le meme master materiel sur le bus AXI. Le CPU peut avoir acces a une zone memoire alors que le NPU, lui, peut etre bloque par la configuration RIF/RISAF/RIMC. Sur STM32N6, les droits d'acces dependent du master, du CID et du contexte secure/non-secure.

### Pourquoi ne pas avoir juste desactive l'assertion ?

Desactiver l'assertion aurait pu empecher le crash immediat, mais cela n'aurait pas resolu le probleme materiel sous-jacent. Le NPU aurait quand meme echoue a executer correctement le reseau. Pour une demo, il est preferable d'eviter un comportement instable ou silencieusement incorrect.

### Pourquoi ne pas avoir utilise CubeMX pour regenerer le projet ?

A ce stade, le projet contenait deja beaucoup de code fonctionnel : reseau, capteurs, HTTP, dashboard, IMU, commandes serveur. Regenerer le projet aurait pu ecraser ou modifier des fichiers critiques. Le risque de casser une demo stable etait trop eleve a deux jours de la soutenance.

### Quelle aurait ete la vraie solution pour faire fonctionner le NPU ?

La solution la plus propre aurait ete de repartir d'un projet STM32N6 minimal genere proprement avec X-CUBE-AI NPU, puis de valider d'abord une inference NPU simple. Ensuite seulement, il aurait fallu reintegrer progressivement Ethernet, les capteurs, les POST HTTP et le dashboard.

### Pourquoi avoir garde le CPU alors que le NPU est plus performant ?

Parce que le modele meteo utilise est leger. Sur CPU, l'inference prend environ 26 ms, alors que le cycle complet de mesure est d'environ 5 secondes. La charge CPU reste faible, donc le CPU suffit largement pour la demonstration finale.

### Est-ce que le VPS fait l'inference ?

Non. Le VPS sert a recevoir, stocker et afficher les donnees. La prediction H+1 est calculee localement sur la carte, puis envoyee au serveur.

### Est-ce que le projet est moins interessant sans le NPU ?

Non, car la chaine embarquee reste complete : acquisition capteurs, inference locale, communication reseau, stockage VPS, dashboard temps reel et commande distante. Le NPU etait un objectif bonus technique, mais le coeur du projet fonctionne.

### Si vous aviez plus de temps, que feriez-vous ?

Je repartirais d'un projet STM32N6 minimal genere proprement avec X-CUBE-AI NPU, puis je validerais d'abord une inference NPU simple. Ensuite seulement, je reintegrerais progressivement Ethernet, les capteurs, les POST HTTP et le dashboard, pour eviter de melanger les problemes applicatifs et les problemes bas niveau NPU.

### Comment prouvez-vous que la carte communique bien avec le VPS ?

La carte envoie une requete HTTP POST toutes les 5 secondes avec les mesures et la prediction. Le dashboard se met a jour avec ces donnees. En plus, l'interface admin permet d'envoyer une commande au serveur, que la carte recupere ensuite en GET pour declencher le mode danse.

### Pourquoi ce choix est defendable techniquement ?

Parce qu'un systeme embarque doit avant tout etre fiable en demonstration. Garder une inference CPU stable permet de montrer toute la chaine fonctionnelle de bout en bout, sans planter la carte a cause d'un probleme bas niveau NPU non resolu.
