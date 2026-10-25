#!/usr/bin/env bash
# NGP LINK : une copie du moteur pour le coeur enveloppe. Chaque symbole global des objets du moteur recoit le prefixe
# donne (a_ ou b_), dans ses definitions comme dans ses references : les deux copies se lient dans la meme DLL sans
# partager une seule variable. Les symboles venus d'ailleurs (C, Windows, libgcc) ne sont pas definis par le moteur
# et gardent leur nom.
# Usage : prefixer.sh <prefixe> <archive de sortie> <objets du moteur...>
set -e
prefixe=$1
sortie=$2
shift 2
dossier="${sortie%.a}.objets"
rm -rf "$dossier"
mkdir -p "$dossier"
nm --defined-only -g "$@" | awk 'NF == 3 { print $3 }' | sort -u | awk -v p="$prefixe" '{ print $1, p $1 }' > "$dossier/noms.txt"
i=0
for objet in "$@"; do
  i=$((i + 1))
  objcopy --redefine-syms="$dossier/noms.txt" "$objet" "$dossier/$i.o"
done
rm -f "$sortie"
ar rcs "$sortie" "$dossier"/*.o
