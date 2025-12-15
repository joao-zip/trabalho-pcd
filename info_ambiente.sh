#!/bin/bash

echo "=========================================="
echo " Configuração do Sistema"
echo "=========================================="

echo -e "\nSistema Operacional:"
uname -a

if [ -f /etc/os-release ]; then
    echo ""
    cat /etc/os-release | grep -E "PRETTY_NAME|VERSION"
fi

echo -e "\nProcessador:"
lscpu | grep -E "Model name|Architecture|CPU\(s\):|Thread|Core|Socket|CPU MHz|Cache"

echo -e "\nMemória:"
free -h

echo -e "\nCompilador:"
gcc --version | head -1

echo -e "\nOpenMP:"
cat > /tmp/test_omp.c << 'EOF'
#include <stdio.h>
#include <omp.h>
int main() {
    #pragma omp parallel
    {
        #pragma omp single
        printf("Versão: %d\n", _OPENMP);
    }
    printf("Threads disponíveis: %d\n", omp_get_max_threads());
    return 0;
}
EOF

gcc -fopenmp /tmp/test_omp.c -o /tmp/test_omp 2>/dev/null
if [ $? -eq 0 ]; then
    /tmp/test_omp
    rm -f /tmp/test_omp /tmp/test_omp.c
else
    echo "Não disponível"
fi

echo -e "\n=========================================="
echo " Resumo"
echo "=========================================="
echo "Sistema: $(uname -s) $(uname -r)"
echo "CPU: $(lscpu | grep "Model name" | cut -d: -f2 | xargs)"
echo "Cores: $(lscpu | grep "Core(s) per socket" | cut -d: -f2 | xargs)"
echo "Threads/Core: $(lscpu | grep "Thread(s) per core" | cut -d: -f2 | xargs)"
echo "Threads Total: $(lscpu | grep "^CPU(s):" | cut -d: -f2 | xargs)"
echo "RAM: $(free -h | grep Mem | awk '{print $2}')"
echo "GCC: $(gcc --version | head -1)"
echo "=========================================="
