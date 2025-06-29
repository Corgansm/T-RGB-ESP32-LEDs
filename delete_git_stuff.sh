#!/bin/bash

# This script deletes the .git and .github directories from the current directory.
# It's useful for completely removing Git version control and GitHub-specific
# configurations from a project.

echo "--- Git and GitHub Folder Deletion Script ---"
echo "This script will attempt to delete the following directories:"
echo "  - .git (Git repository data)"
echo "  - .github (GitHub-specific workflows/configurations)"
echo ""
echo "It will only affect the current directory and its subdirectories."
echo ""

# Check if .git directory exists
if [ -d ".git" ]; then
    echo "Found .git directory. Deleting..."
    rm -rf .git
    if [ $? -eq 0 ]; then
        echo "Successfully deleted .git."
    else
        echo "Error: Failed to delete .git."
    fi
else
    echo ".git directory not found. Skipping."
fi

echo ""

# Check if .github directory exists
if [ -d ".github" ]; then
    echo "Found .github directory. Deleting..."
    rm -rf .github
    if [ $? -eq 0 ]; then
        echo "Successfully deleted .github."
    else
        echo "Error: Failed to delete .github."
    fi
else
    echo ".github directory not found. Skipping."
fi

echo ""
echo "Script finished."
echo "---------------------------------------------"
