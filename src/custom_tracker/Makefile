# Makefile for Custom BYTETrack Tracker Plugin
#
# NOTE: Build this in the Docker container where DeepStream is installed
# The project is mounted at /work/ in the container

# Plugin name
PLUGIN_NAME = gstcustomtracker

# Source files
SOURCES = customtracker.cpp gstcustomtracker.cpp

# Object files
OBJECTS = $(SOURCES:.cpp=.o)

# Target shared library
TARGET = lib$(PLUGIN_NAME).so

# Compiler
CC = g++
CXX = g++

# DeepStream version
DS_VERSION = 6.4

# DeepStream paths (in Docker container)
DS_ROOT = /opt/nvidia/deepstream/deepstream
DS_INC = $(DS_ROOT)/include
DS_LIB = $(DS_ROOT)/lib

# GStreamer paths
GST_INC = /usr/include/gstreamer-1.0
GST_GLIB_INC = /usr/lib/x86_64-linux-gnu/glib-2.0/include
GSTREAMER_1.0_GSTCONFIG = /usr/lib/x86_64-linux-gnu/gstreamer-1.0/include/gst

# Include paths
INCLUDES = -I$(GST_INC) \
           -I$(GST_GLIB_INC) \
           -I$(DS_INC) \
           -I.

# Library paths
LDFLAGS = -L$(DS_LIB) \
          -L/usr/lib/x86_64-linux-gnu

# Libraries to link
LIBS = -lnvdsgst_meta \
       -lnvds_meta \
       -lnvdsbatchmeta \
       -lgstbase-1.0 \
       -lgstreamer-1.0 \
       -lgobject-2.0 \
       -lglib-2.0

# Compiler flags
CFLAGS = -fPIC -Wall -std=c++17 -O2 -g
CXXFLAGS = $(CFLAGS) $(INCLUDES)

# Linker flags
LINK_FLAGS = -shared -Wl,-soname,$(TARGET) $(LDFLAGS) $(LIBS)

# Installation path (GStreamer plugins directory)
INSTALL_PATH = /usr/lib/x86_64-linux-gnu/gstreamer-1.0/plugins/

# Default target
all: $(TARGET)

# Link object files to create shared library
$(TARGET): $(OBJECTS)
	@echo "Linking $(TARGET)..."
	$(CXX) -o $@ $^ $(LINK_FLAGS)
	@echo "Done: $(TARGET)"

# Compile source files
%.o: %.cpp
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Compile header dependencies
customtracker.o: customtracker.h
gstcustomtracker.o: gstcustomtracker.h customtracker.h

# Clean build artifacts
clean:
	@echo "Cleaning..."
	rm -f $(OBJECTS) $(TARGET)
	@echo "Clean complete"

# Install plugin
install: $(TARGET)
	@echo "Installing $(TARGET) to $(INSTALL_PATH)..."
	cp $(TARGET) $(INSTALL_PATH)
	@echo "Plugin installed. Run: gst-inspect-1.0 $(PLUGIN_NAME)"

# Uninstall plugin
uninstall:
	@echo "Uninstalling $(TARGET)..."
	rm -f $(INSTALL_PATH)/$(TARGET)
	@echo "Plugin uninstalled"

# Test compilation
test: $(TARGET)
	@echo "Testing plugin registration..."
	@if [ -f $(INSTALL_PATH)/$(TARGET) ]; then \
		gst-inspect-1.0 $(INSTALL_PATH)/$(TARGET) || true; \
	else \
		echo "Plugin not installed. Run 'make install' first."; \
	fi

# Debug build
debug: CFLAGS += -DDEBUG -g -O0
debug: clean $(TARGET)

# Release build
release: CFLAGS = -fPIC -Wall -std=c++17 -O3
release: clean $(TARGET)

# Info target
info:
	@echo "=== Custom BYTETrack Tracker Plugin ==="
	@echo "Target:        $(TARGET)"
	@echo "Sources:       $(SOURCES)"
	@echo "DeepStream:    $(DS_ROOT)"
	@echo "Install path:  $(INSTALL_PATH)"
	@echo ""
	@echo "Usage:"
	@echo "  make          - Build plugin"
	@echo "  make install  - Install plugin"
	@echo "  make clean    - Clean build"
	@echo "  make test     - Test plugin"

.PHONY: all clean install uninstall test debug release info
