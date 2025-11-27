CC = gcc
CFLAGS = -I./src -Wall -Wextra -O2 -std=c11
SRC = src/vision.c src/robots.c src/calibrate.c src/common.c
BUILD_DIR = build
RESULTS_DIR = results

# Detectar plataforma
ifeq ($(OS),Windows_NT)
    LIBS = -lws2_32 -lpthread
    EXE_EXT = .exe
    MKDIR = mkdir
    RM = del /Q
    RM_DIR = rmdir /S /Q
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Linux)
        LIBS = -lpthread -lm
        EXE_EXT =
    else
        LIBS = -lpthread -lm
        EXE_EXT =
    endif
    MKDIR = mkdir -p
    RM = rm -f
    RM_DIR = rm -rf
endif

# Targets principales
all: directories $(BUILD_DIR)/vision$(EXE_EXT) $(BUILD_DIR)/robots$(EXE_EXT) $(BUILD_DIR)/calibrate$(EXE_EXT)

directories:
	@$(MKDIR) $(BUILD_DIR) 2>/dev/null || true
	@$(MKDIR) $(RESULTS_DIR) 2>/dev/null || true
	@$(MKDIR) $(RESULTS_DIR)/plots 2>/dev/null || true

# Compilar vision
$(BUILD_DIR)/vision$(EXE_EXT): src/vision.c src/common.c
	@echo "Compilando vision..."
	$(CC) $(CFLAGS) $^ -o $@ $(LIBS)
	@echo "✓ vision compilado"

# Compilar robots
$(BUILD_DIR)/robots$(EXE_EXT): src/robots.c src/common.c
	@echo "Compilando robots..."
	$(CC) $(CFLAGS) $^ -o $@ $(LIBS)
	@echo "✓ robots compilado"

# Compilar calibrate
$(BUILD_DIR)/calibrate$(EXE_EXT): src/calibrate.c src/common.c
	@echo "Compilando calibrate..."
	$(CC) $(CFLAGS) $^ -o $@ $(LIBS)
	@echo "✓ calibrate compilado"

# Test rápido
test: all
	@echo "\n=== Test Rápido ==="
	@echo "Iniciando robots en background..."
	@$(BUILD_DIR)/robots$(EXE_EXT) 9001 2 10 30 200 200 0.0 > /tmp/robots_test.log 2>&1 & echo $$! > /tmp/robots_test.pid
	@sleep 1
	@echo "Enviando datos desde vision..."
	@$(BUILD_DIR)/vision$(EXE_EXT) 127.0.0.1 9001 5 30 1234
	@sleep 2
	@echo "Limpiando..."
	@kill `cat /tmp/robots_test.pid` 2>/dev/null || true
	@$(RM) /tmp/robots_test.pid /tmp/robots_test.log
	@echo "✓ Test completado"

# Ejecutar calibración completa
calibrate: all
	@echo "\n╔════════════════════════════════════════════════╗"
	@echo "║  Ejecutando Calibración del Sistema           ║"
	@echo "╚════════════════════════════════════════════════╝"
	@echo "Esto puede tomar varios minutos...\n"
	$(BUILD_DIR)/calibrate$(EXE_EXT) 10 50 10 30 200 200 $(RESULTS_DIR)
	@echo "\n✓ Calibración completada"
	@echo "Ejecute: make analyze para generar gráficos"

# Analizar resultados (requiere Python3 y matplotlib)
analyze:
	@echo "\n=== Generando Gráficos ==="
	@command -v python3 >/dev/null 2>&1 || { echo "Error: Python3 no encontrado"; exit 1; }
	python3 scripts/plot_results.py $(RESULTS_DIR)
	@echo "✓ Gráficos generados en $(RESULTS_DIR)/"

# Ejecutar simulación demo
demo: all
	@echo "\n╔════════════════════════════════════════════════╗"
	@echo "║  Demo: Simulación con 15 mangos y 4 robots    ║"
	@echo "╚════════════════════════════════════════════════╝\n"
	@$(BUILD_DIR)/robots$(EXE_EXT) 9000 4 10 30 200 200 0.05 & PID=$$!; \
	sleep 1; \
	$(BUILD_DIR)/vision$(EXE_EXT) 127.0.0.1 9000 15 30 1234; \
	wait $$PID

# Limpiar archivos generados
clean:
	@echo "Limpiando archivos compilados..."
	@$(RM_DIR) $(BUILD_DIR) 2>/dev/null || true
	@$(RM) src/*.o *.o 2>/dev/null || true
	@echo "✓ Limpieza completada"

# Limpiar todo incluyendo resultados
cleanall: clean
	@echo "Limpiando resultados..."
	@$(RM_DIR) $(RESULTS_DIR) 2>/dev/null || true
	@$(MKDIR) $(RESULTS_DIR) 2>/dev/null || true
	@echo "✓ Limpieza total completada"

# Verificar setup del proyecto
check:
	@echo "\n=== Verificando Entorno ==="
	@echo -n "GCC: "
	@$(CC) --version | head -n1 || echo "✗ No encontrado"
	@echo -n "Threads: "
	@echo "✓ pthreads disponible"
	@echo -n "Python3: "
	@python3 --version 2>/dev/null || echo "✗ No encontrado (requerido para análisis)"
	@echo -n "Matplotlib: "
	@python3 -c "import matplotlib" 2>/dev/null && echo "✓" || echo "✗ (pip3 install matplotlib)"
	@echo -n "Pandas: "
	@python3 -c "import pandas" 2>/dev/null && echo "✓" || echo "✗ (pip3 install pandas)"
	@echo -n "Numpy: "
	@python3 -c "import numpy" 2>/dev/null && echo "✓" || echo "✗ (pip3 install numpy)"
	@echo ""

# Ayuda
help:
	@echo "MangoNeado - Sistema de Etiquetado Automatizado"
	@echo ""
	@echo "Targets disponibles:"
	@echo "  make              - Compilar todos los programas"
	@echo "  make test         - Ejecutar test rápido"
	@echo "  make demo         - Demostración interactiva"
	@echo "  make calibrate    - Ejecutar calibración completa (Punto 2 y 3)"
	@echo "  make analyze      - Generar gráficos de resultados"
	@echo "  make check        - Verificar dependencias del sistema"
	@echo "  make clean        - Limpiar archivos compilados"
	@echo "  make cleanall     - Limpiar todo (incluyendo resultados)"
	@echo "  make help         - Mostrar esta ayuda"
	@echo ""
	@echo "Flujo de trabajo típico:"
	@echo "  1. make           # Compilar"
	@echo "  2. make check     # Verificar dependencias"
	@echo "  3. make demo      # Probar funcionamiento"
	@echo "  4. make calibrate # Generar datos (Punto 2 y 3)"
	@echo "  5. make analyze   # Crear gráficos requeridos"
	@echo ""

.PHONY: all directories test calibrate analyze demo clean cleanall check help