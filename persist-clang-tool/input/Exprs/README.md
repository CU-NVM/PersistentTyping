# Exprs Benchmark
(This README is written assuming you read it from the top directory. It is copied as is to ```Output/Exprs``` once you run through the compiler).
This benchmark is a modified version of https://github.com/nmante/Data-Structures 

This is a multithreaded data structures benchmark. The main benchmark we make use of here is the binary search tree data structures. (See section 6 on paper) 

The benchmark sets up two sets of data structures and two sets of threads in the different numa/regular/split configurations (Table2 on paper).

This mimics a numa annotated data structure and thread structures yet to be recursivley typed to have maximum localized accesses by our clang-tool compiler.


# How to compile and run

```
sudo make UMF=1

./Examples/bin/DSExample -n 10240 -t 40 -D 20 --DS_name=bst --th_config=numa --DS_config=numa -k 160 -i 10
```


# Description
* ```Examples/main.cpp``` is where options are configured and the threads call the initilization and test functions.
* ```Examples/TestSuite.cpp``` is where the initilization and test functions are defined.
* ```Examples/Makefile``` has all the compile options that initilizes our desired #ifdef flags and links flags of our used libraries. **Adjust HOME_DIR variable to your appropriate home directory**
* ```Examples/include``` has all the declaration and definition of our data-structure classes. This library directory is the one that will recursivley typed to numa when passed through our clang tool (compare with ```../Output/Exprs/include``` once you run according to ```../README.md``` )
