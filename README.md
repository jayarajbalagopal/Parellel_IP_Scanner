# Parellel IP Scanner
A Parellelised IP Scanner, borrowing idea from the traditional producer consumer problem. The input file is split into batches and each batch is considered as an entry in the buffer for the producer-consumer workflow.
### COMPILATION
```
g++ -std=c++11 main.cpp -o main
```

### USAGE
```
./main <input file>
```

### OUTPUT
```
TOTAL IPV4 995
TOTAL_IPV6 100
TOTAL_UNIQUE_IPV4 988
TOTAL_UNIQUE_IPV6 100
INVALID 15
```
