## TP1: MNIST Classification Problem 

This TP focuses on the classification of handwritten digits (0 to 9) using the classic **MNIST** dataset. 

* **Objective**: Implement a **Dense Neural Network** (Multi-Layer Perceptron / MLP) featuring an input layer of 784 neurons (28x28 flattened pixels) and an output layer of 10 neurons (one for each class).

* **Experiments**:
    * **Activation Functions**: Comparison of *softmax*, *ReLU*, *tanh*, and *sigmoid*.
    * **Optimizers**: Evaluation of *SGD* (classical), *Adam* (modern and efficient), *RMSprop* (adaptive learning rate), and *Adagrad*.
    * **Loss Functions**: Selection of the appropriate loss function (*categorical_crossentropy* vs. *sparse_categorical_crossentropy*) while justifying the trade-offs between accuracy, training time, and model complexity.

### Implementation Details

To train the neural network using the **MNIST** dataset, each **[28 × 28]** image is transformed into a flattened **[1 × 784]** vector. Consequently, the spatial position of pixels is no longer explicitly exploited by the model. We will implement a **Dense Neural Network** model, also known as a **Multi-Layer Perceptron (MLP)** or **Fully Connected** network.

The architecture consists of:
* An **input layer** of dimension 784.
* A **dense output layer** of 10 neurons, producing a probability distribution across the 10 classes.
* A variable number of **hidden layers** based on specific requirements.

Each output represents the probability that a given image belongs to a specific class. The learning process is performed via **gradient descent** by minimizing the **cross-entropy**.



#### Optimization and Metrics
* **Gradient Descent**: An optimization algorithm that trains machines by reducing the error between predicted and actual results.
* **Cross-Entropy**: A metric used to reflect the accuracy of probabilistic forecasts. It is essential for modern forecasting systems, as it plays a key role in producing superior predictions. Cross-entropy is particularly vital for estimating models that capture the probabilities of rare events, which often prove to be the most costly.

### Experimental Protocol

We have prepared the code and tested several activation models:
* **Baseline (Softmax)**
* **ReLU**
* **tanh**
* **sigmoid**

We will compare these results to select the best compromise between **efficiency and cost**.

In the second phase, we will determine the most suitable architecture for our project by comparing the following optimizers:
* **SGD**
* **Adam**
* **RMSprop**
* **Adagrad**

Finally, we will select the cost function (Loss Function) based on our specific needs from the following options:
* `binary_crossentropy`
* `categorical_crossentropy`
* `sparse_categorical_crossentropy`

## 👤 Auteur

William / Franck / Mostapha — L3 TRI // ESET  
Module ETRS606 — IA Embarquée  
Université Savoie Mont Blanc