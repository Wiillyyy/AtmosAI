# TP 1 : Problème MNIST - IA Embarquée

Ce document synthétise les résultats des différentes simulations menées sur le jeu de données MNIST en utilisant des Perceptrons Multicouches (MLP). L'objectif est de comparer les architectures et les hyperparamètres dans une optique de contraintes d'IA embarquée (mémoire, temps de calcul, précision).

---

## 1. Choix de la Fonction d'Activation

**Conditions de test :**
* Optimiseur : `Adam`
* Fonction coût : `categorical_crossentropy`
* Époques : 5
* Batch size : 32

### 1.1 Tableau des Résultats

| Configuration | Architecture (Couches) | Activation Cachée | Nb Synapses (Poids) | Accuracy (Test) |
| :--- | :--- | :--- | :--- | :--- |
| **a. Baseline** | Entrée(784) -> Sortie(10) | N/A (Softmax) | *7 850* | *92.61%* |
| **b. Profond** | Entrée -> Dense(128) -> Dense(64) -> Sortie | `relu` | *109 386* | *97.24%* |
| **c. Tanh** | Entrée -> Dense(64) -> Sortie | `tanh` | *50 890* | *96.99%* |
| **d. Sigmoid** | Entrée -> Dense(64) -> Sortie | `sigmoid` | *50 890* | *96.39%* |

---

### Architectures générées

Voici les graphes des modèles correspondants aux configurations testées :

**Modèle a. Baseline (Softmax direct)**

<img width="608" height="480" alt="image" src="https://github.com/user-attachments/assets/a74fab49-2f59-443a-9348-e30976a7919a" />


**Modèle b. Profond (ReLU, 2 couches cachées)**

<img width="608" height="480" alt="image" src="https://github.com/user-attachments/assets/5aa2c089-ca44-482e-9722-8050318f57e5" />


**Modèle c. Tanh (1 couche cachée)**

<img width="608" height="480" alt="image" src="https://github.com/user-attachments/assets/6f716811-e1df-40ab-a4a6-e3cf6b7cc002" />


**Modèle d. Sigmoid (1 couche cachée)**

<img width="608" height="480" alt="image" src="https://github.com/user-attachments/assets/99877b0a-4e78-4acf-b7c7-14aeda2b1d27" />


### ** 1.2 Justification des Compromis **

L'analyse des résultats met en évidence plusieurs compromis fondamentaux, particulièrement critiques dans le contexte de l'Intelligence Artificielle Embarquée où les ressources (mémoire, calcul, énergie) sont limitées.

** 1. Compromis Accuracy vs Architecture et Fonction d'Activation **

    L'apport des couches cachées : Le passage d'un modèle linéaire (Baseline à 92.61%) à des modèles profonds permet de faire un bond significatif en précision (gain d'environ 4 à 5%). Les couches cachées permettent d'extraire des caractéristiques non-linéaires des images.

    ReLU vs Tanh vs Sigmoid : L'architecture b (ReLU) obtient la meilleure précision (97.24%). Pour une architecture identique (1 couche de 64 neurones), Tanh (96.99%) surpasse Sigmoid (96.39%). Cela s'explique par le fait que la fonction Tanh est centrée sur zéro (sorties entre -1 et 1), ce qui facilite l'optimisation par rapport à Sigmoid (sorties entre 0 et 1).

2. Compromis Accuracy vs Nombre d'Époques (Vitesse de convergence)

    Les tests ont été figés à 5 époques. À ce stade précoce, ReLU converge très rapidement vers une haute précision car elle ne souffre pas du problème de saturation des gradients pour les valeurs positives.

    La fonction Sigmoid montre une précision légèrement en retrait après 5 époques. Elle est sujette au phénomène de "Vanishing Gradient" (disparition du gradient) aux extrémités, ce qui ralentit l'apprentissage. Pour atteindre la même précision que ReLU ou Tanh, Sigmoid nécessiterait probablement beaucoup plus d'époques, ce qui implique un temps d'entraînement plus long et une plus grande consommation énergétique.

3. Compromis Accuracy vs Nombre de Synapses (Empreinte Mémoire)

    C'est le critère le plus important en IA Embarquée. Le nombre de synapses dicte la dimension de la matrice des poids W, qui doit être stockée en mémoire (généralement en Flash) et chargée en RAM lors de l'inférence.

    Le modèle Baseline est extrêmement léger (7 850 paramètres, soit environ 31 Ko si stocké en flottants 32 bits). Il est idéal pour un microcontrôleur très contraint, bien que sa précision soit perfectible.

    Le modèle ReLU (2 couches) offre la meilleure accuracy, mais son coût mémoire est massif : 109 386 paramètres (environ 437 Ko). Il est 14 fois plus lourd que le modèle Baseline pour un gain de précision d'environ 4.6%.

    Conclusion pour l'embarqué : Si l'application visée nécessite une fiabilité absolue et tourne sur un Raspberry Pi ou un processeur ARM Cortex-A, le modèle (b) est préférable. En revanche, pour un petit microcontrôleur Cortex-M de quelques dizaines de Ko de RAM, on privilégiera des architectures intermédiaires (comme le modèle c/d) ou l'on cherchera à réduire la taille de la couche d'entrée ou à utiliser la quantification (Quantization).


   ## 2. Choix de l'Algorithme d'Optimisation

**Conditions de test :**
* Architecture fixée : 1 couche cachée (64 neurones), activation `tanh` (Mémoire optimisée : 50 890 synapses)
* Fonction coût : `categorical_crossentropy`
* Époques : 5
* Batch size : 32

### 2.1 Tableau Comparatif des Optimiseurs

| Optimiseur | Accuracy (Test) | Temps d'exécution / Époque | Comportement observé |
| :--- | :--- | :--- | :--- |
| **SGD** | *92.84%* | *6s* | Base simple, progression souvent plus lente nécessitant plus d'époques. |
| **Adam** | **96.99%** | *5s* | Très rapide, adapte le pas d'apprentissage dynamiquement. |
| **RMSprop** | **96.95%** | *6s* | Excellent compromis, très stable. |
| **Adagrad** | **89.14%** | *6s* | Diminue le taux d'apprentissage, peut s'essouffler sur peu d'époques. |

### 2.2 Analyse pour l'IA Embarquée (Justification)

L'analyse de ces différents algorithmes d'optimisation amène une conclusion essentielle pour le déploiement sur système embarqué :

* **Aucun impact sur l'inférence (le système cible) :** Le choix de l'optimiseur n'a **aucune influence** sur le modèle final une fois compilé pour la cible. Que le réseau soit entraîné avec SGD ou Adam, la taille en mémoire reste strictement identique (50 890 paramètres pour notre architecture Tanh), tout comme le nombre d'opérations mathématiques nécessaires pour faire une prédiction. L'optimiseur n'existe plus lors de l'exécution sur le microcontrôleur.
* **Efficacité de la phase de développement (Training) :** Bien que l'optimiseur ne tourne pas sur la cible embarquée, il est crucial pour l'ingénieur. **Adam** est ici le meilleur choix. Il permet d'atteindre la convergence maximale (près de 97%) en un minimum d'époques. Cela permet d'économiser un temps précieux et des ressources de calcul (énergie/GPU) lors de la phase d'entraînement par rapport à SGD ou Adagrad.
* 

## 3. Choix de la Fonction Coût

**Conditions de test :**
* Architecture fixée : 1 couche cachée (64 neurones), `tanh`, optimiseur `Adam`.
* Différence clé : Formattage des labels et couche de sortie.

### 3.1 Tableau Comparatif

| Fonction Coût | Problème simulé | Format des Labels (y) | Couche de sortie | Accuracy (Test) |
| :--- | :--- | :--- | :--- | :--- |
| **Categorical** | Multi-Classes (0 à 9) | One-Hot `[0,0,0,1,...]` | `Dense(10, softmax)` | **96.99%** |
| **Sparse Categorical** | Multi-Classes (0 à 9) | Entiers `[3]` | `Dense(10, linéaire/logits)` | *96.78* |
| **Binary** | Binaire ("Est-ce un 5 ?") | Binaire `[1]` ou `[0]` | `Dense(1, sigmoid)` | *99.49* |

### 3.2 Analyse de l'impact pour l'Embarqué
* **Mémoire RAM (Préparation des données) :** L'utilisation de `sparse_categorical_crossentropy` est extrêmement avantageuse. Elle évite la création de lourdes matrices One-Hot en RAM. Pour un dataset de 60 000 images, on stocke un vecteur de 60 000 octets au lieu de 600 000, ce qui est crucial pour le "On-Device Learning" sur microcontrôleur.
* **Complexité réseau (Binaire) :** Dans le cas de `binary_crossentropy`, la couche de sortie est réduite à 1 seul neurone au lieu de 10. Le nombre de paramètres (synapses) diminue, allégeant encore plus le modèle en Flash et le temps de calcul CPU.