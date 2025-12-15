#!/bin/bash

echo "=============================================="
echo "  INFORMAÇÕES DO AMBIENTE DE TESTE"
echo "=============================================="

echo -e "\n=== SISTEMA OPERACIONAL ==="
uname -a

echo -e "\n=== DISTRIBUIÇÃO LINUX ==="
if [ -f /etc/os-release ]; then
    cat /etc/os-release | grep -E "PRETTY_NAME|VERSION"
fi

echo -e "\n=== PROCESSADOR ==="
lscpu | grep -E "Model name|Architecture|CPU\(s\)|Thread|Core|Socket|CPU MHz|Cache"

echo -e "\n=== MEMÓRIA ==="
free -h

echo -e "\n=== COMPILADOR GCC ==="
gcc --version | head -1

echo -e "\n=== SUPORTE OPENMP ==="
echo "Verificando suporte OpenMP..."
cat > /tmp/test_omp.c << 'EOF'
#include <stdio.h>
#include <omp.h>
int main() {
    #pragma omp parallel
    {
        #pragma omp single
        printf("OpenMP version: %d\n", _OPENMP);
    }
    printf("Max threads: %d\n", omp_get_max_threads());
    return 0;
}
EOF

gcc -fopenmp /tmp/test_omp.c -o /tmp/test_omp 2>/dev/null
if [ $? -eq 0 ]; then
    /tmp/test_omp
    rm -f /tmp/test_omp /tmp/test_omp.c
else
    echo "OpenMP não disponível ou erro na compilação"
fi

echo -e "\n=== PYTHON ==="
python3 --version

echo -e "\n=== BIBLIOTECAS PYTHON ==="
echo "NumPy: $(python3 -c 'import numpy; print(numpy.__version__)' 2>/dev/null || echo 'não instalado')"
echo "Matplotlib: $(python3 -c 'import matplotlib; print(matplotlib.__version__)' 2>/dev/null || echo 'não instalado')"
echo "Pandas: $(python3 -c 'import pandas; print(pandas.__version__)' 2>/dev/null || echo 'não instalado')"

echo -e "\n=== RESUMO PARA O RELATÓRIO ==="
echo "----------------------------------------"
echo "Sistema: $(uname -s) $(uname -r)"
echo "CPU: $(lscpu | grep "Model name" | cut -d: -f2 | xargs)"
echo "Cores Físicos: $(lscpu | grep "Core(s) per socket" | cut -d: -f2 | xargs)"
echo "Threads por Core: $(lscpu | grep "Thread(s) per core" | cut -d: -f2 | xargs)"
echo "Total de Threads: $(lscpu | grep "^CPU(s):" | cut -d: -f2 | xargs)"
echo "Memória RAM: $(free -h | grep Mem | awk '{print $2}')"
echo "Compilador: $(gcc --version | head -1)"
echo "----------------------------------------"

echo -e "\n✓ Coleta de informações concluída!"
echo "  Copie o resumo acima para o seu relatório."
