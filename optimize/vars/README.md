### Named Variables Accessor

Objectives:

* Support sparse indexing. (`x[0]`, `x[1]`, `x[123456]`, etc.).
* Support string indexing. (`x["foo"]`, `x["regularization"]["c"]`, etc.).
* Support multidimensional indexing. (`x[1][2]`, `x[0]["my_feature"]`, etc.)
* Of course, support the good old dense vectors as well. (`x["model"].DenseDoubleVector(1000);`).

Strong typing is enforced, and attempts to use the variable in the context different from what it was before will result in an exception.

Serves both the variables vector and the constants vector. The variables vector is what is differentiated and optimized; the variables vector is also used to define a starting point. The constants vector is its own universe, with the free parameters that are used, but not optimized over, such as the right-hand-sides of rows, or the regulatization parameters. The ultimate goal is that thanks to the existence of the contants vector each form of the function to be optimized only needs to be differentiated and compiled once.

One of the goals of this named variables accessor is to make using Current's Optimizer more pleasant. I remember from the FnCAS times that manually re-mapping various model parameters to indexes and vice versa is a painful task, and, moving forward, as the Optimizer is now thought of as a standalone product, I would like to alleviate this pain once and for all.
