#!/bin/sh
# Generate all files needed to compile AirCat

echo "Running aclocal..."
aclocal
echo "Running autoheader..."
autoheader
echo "Running autoconf..."
autoconf
echo "Running automake..."
automake --add-missing --copy

echo "Now, you can run './configure' and 'make'."

