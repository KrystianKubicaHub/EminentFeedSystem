#!/bin/bash

echo "Analiza kodu źródłowego:"

# Liczba plików
cpp_files=$(find . -type f -name "*.cpp" | wc -l)
hpp_files=$(find . -type f -name "*.hpp" | wc -l)

# Liczba linii
cpp_lines=$(find . -type f -name "*.cpp" -exec cat {} + | wc -l)
hpp_lines=$(find . -type f -name "*.hpp" -exec cat {} + | wc -l)

echo "Pliki .cpp: $cpp_files"
echo "Pliki .hpp: $hpp_files"
echo "Linie .cpp: $cpp_lines"
echo "Linie .hpp: $hpp_lines"
echo "Łącznie linii: $((cpp_lines + hpp_lines))"
