#!/bin/bash
echo "Testing for existing Splice installation..."
if command -v Splice &> /dev/null; then
        # Accept -y or --yes to skip confirmation (also support --force)
        ASSUME_YES=0
        if [[ "${1-}" == "-y" || "${1-}" == "--yes" || "${1-}" == "--force" ]]; then
            ASSUME_YES=1
        fi

        echo "This will remove the Splice binary from /usr/local/bin."
        if [[ $ASSUME_YES -eq 0 ]]; then
            read -p "Are you sure? [y/N] " -n 1 -r
            echo
            if [[ ! $REPLY =~ ^[Yy]$ ]]; then
                echo "Aborted."
                exit 0
            fi
        fi

        if [ -f /usr/local/bin/Splice ]; then
                sudo rm /usr/local/bin/Splice
                echo "Splice removed from /usr/local/bin."
                exit 0
        else
                echo "No Splice binary found in /usr/local/bin."
                exit 1
        fi
else
    echo "No existing Splice installation found. Nothing to uninstall."
    exit 0
fi