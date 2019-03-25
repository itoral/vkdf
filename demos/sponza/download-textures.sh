#!/bin/sh

TEXTURES_URL=http://people.igalia.com/itoral/sponza_textures.tgz

echo "Download and unpack textures for the Sponza model. This needs the 'wget' and 'unrar' tools.\n"
echo "Downloading Sponza texture pack from Crytek site. This can take some time..."


wget $TEXTURES_URL -O sponza_textures.tgz

if [ $? -eq 0 ]; then
   echo "Unpacking textures..."
   umask 022
   mkdir -p textures
   tar zxf sponza_textures.tgz -C textures/
else
   echo "Failed to download!"
fi
