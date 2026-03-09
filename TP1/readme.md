## TP1: MNIST Classification Problem 

This TP focuses on the classification of handwritten digits (0 to 9) using the classic **MNIST** dataset. 

* **Objective**: Implement a **Dense Neural Network** (Multi-Layer Perceptron / MLP) featuring an input layer of 784 neurons (28x28 flattened pixels) and an output layer of 10 neurons (one for each class).

* **Experiments**:
    * **Activation Functions**: Comparison of *softmax*, *ReLU*, *tanh*, and *sigmoid*.
    * **Optimizers**: Evaluation of *SGD* (classical), *Adam* (modern and efficient), *RMSprop* (adaptive learning rate), and *Adagrad*.
    * **Loss Functions**: Selection of the appropriate loss function (*categorical_crossentropy* vs. *sparse_categorical_crossentropy*) while justifying the trade-offs between accuracy, training time, and model complexity.
