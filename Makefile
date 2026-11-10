# Compilador y estándar
CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra

# Directorios
SRC_DIR  := src
INC_DIR  := include
OBJ_DIR  := obj

# Nombre del ejecutable
TARGET   := solver_ci

# Buscar todos los archivos .cc en /src
SOURCES  := $(wildcard $(SRC_DIR)/*.cc)
OBJECTS  := $(SOURCES:$(SRC_DIR)/%.cc=$(OBJ_DIR)/%.o)

# Flags de inclusión
CPPFLAGS := -I$(INC_DIR)

# --- Configuración de Perfiles ---

# Por defecto, si solo escribes 'make', se usará 'release'
.PHONY: all
all: release

# Perfil Release: Optimización máxima y sin símbolos de depuración
.PHONY: release
release: CXXFLAGS += -O3 -DNDEBUG
release: $(TARGET)

# Perfil Debug: Símbolos de depuración y sin optimización
.PHONY: debug
debug: CXXFLAGS += -g -O0
debug: $(TARGET)

# --- Reglas de Compilación ---

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cc | $(OBJ_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# --- Utilidades ---

.PHONY: clean
clean:
	rm -rf $(OBJ_DIR) $(TARGET)

.PHONY: run
run: release
	./$(TARGET)