#!/bin/sh
# Generate all files needed to compile AirCat

# Create m4 folder
if [ ! -d m4 ]; then
	mkdir m4
fi

echo "Running autoreconf..."
autoreconf --force --install --verbose -I m4

echo "Now, you can run './configure' and 'make'."

