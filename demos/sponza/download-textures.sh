#!/bin/sh
echo "Download and unpack textures for the Sponza model. This needs the 'wget' and 'unrar' tools.\n"
echo "Downloading Sponza texture pack from Crytek site. This can take some time..."
wget http://www.crytek.com/download/sponza_textures.rar -O sponza_textures.rar
if [ $? -eq 0 ]; then
   echo "Unpacking textures..."
   unrar x sponza_textures.rar
else
   echo "Failed to download!"
fi
