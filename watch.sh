#!/bin/bash

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Print banner
echo -e "${YELLOW}Starting file watcher for C++ files...${NC}"
echo "Press Ctrl+C to stop"
echo

# Ensure make exists
if ! command -v make &> /dev/null; then
    echo -e "${RED}Error: make is not installed${NC}"
    exit 1
fi

# Monitor function
monitor_and_build() {
    fswatch -0 -e ".*" -i "\.cpp$" . | while read -d "" file; do
        # Get timestamp
        timestamp=$(date '+%Y-%m-%d %H:%M:%S')
        
        # Clear screen for better visibility
        clear
        
        echo -e "${YELLOW}[${timestamp}] File changed: ${file}${NC}"
        echo -e "${YELLOW}Running make...${NC}"
        echo "----------------------------------------"
        
        # Run make and capture output
        if make; then
            echo "----------------------------------------"
            echo -e "${GREEN}✓ Compilation successful${NC}"
        else
            echo "----------------------------------------"
            echo -e "${RED}✗ Compilation failed${NC}"
        fi
        echo
    done
}

# Try to do initial build
echo -e "${YELLOW}Performing initial build...${NC}"
if make; then
    echo -e "${GREEN}✓ Initial build successful${NC}"
else
    echo -e "${RED}✗ Initial build failed${NC}"
    echo -e "${YELLOW}Continuing to watch for changes...${NC}"
fi
echo

# Start monitoring
monitor_and_build

