OpenGX
======

OpenGX is an OpenGL-like wrapper which sits on GX subsystem. It implements the most common OpenGL calls such as immediate mode, texturing, array drawing (indexed and not indexed), matrix operations and basic lighting. It features some advanced stuff such as automatic texture compression (ARB extension) and mipmapping. Beware! Some parts are currently NOT opengl compilant (speaking strictly).
TODO list

  * Fix lighting transform (which is buggy) and implement spotlights (untested)
  * Fix texture allocation. Now it's mandatory to allocate a texture name using glGen and you can't just bind the texture and use it
  * Complete glGet call
  * Add support for attribute push/pop
  * Add support for glReadPixels and similar calls

List of features (detailed but not exhaustive)

  * Texture conversion/compression. Accepts RGB,RGBA,COMPRESSED_RGBA and LUMINANCE_ALPHA.
  * Matrix math stuff including glu calls
  * Texture mipmapping (and gluBuildMipMaps)
  * Texture sub-images
  * Texture coordinate generation
  * Ambient and diffuse, emission and specular lighting
  * Indexed and not indexed draw modes
  * Blending support
  * Alpha compare
  * Call lists
  * Fog
  * Selection mode

There are several limitations on the implementation of many features, mostly
due to the hardware. They are detailed in the PDF documentation, but feel free
to file an issue if unsure.

Design documentation
--------------------

Please see the `opengx.pdf` file which is [automatically generated with each
build](https://github.com/devkitPro/opengx/actions/workflows/docs.yml).


Build instructions
------------------

The project is built with [cmake](https://cmake.org/) and requires the `libogc`
package from [devkitPro](https://devkitpro.org/). Once these dependencies are
installed, build opengx as follows:

    # Use GameCube.cmake to build for the GameCube instead
    cmake -S. -Bbuild -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Wii.cmake"
    cd build
    make
    # Optional, to install it into devkitPro's portslib:
    sudo -E PATH=$PATH make install
