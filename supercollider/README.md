# Squine

Author: rasmus

Squinewave oscillator as supercollider extension Ugen

### Requirements

- CMake >= 3.5
- SuperCollider source code

### Building

Clone the project:

    git clone https://github.com/required-field/squinewave/supercollider
    cd squinewave/supercollider
    mkdir build
    cd build

Then, use CMake to configure and build it:

    cmake .. -DCMAKE_BUILD_TYPE=Release
    cmake --build . --config Release
    cmake --build . --config Release --target install

You may want to manually specify the install location in the first step to point it at your
SuperCollider extensions directory: add the option `-DCMAKE_INSTALL_PREFIX=/path/to/extensions`.

MacOS
on mac this is usually named bla bla "/Application Support" blah with a space.
This must be escaped, eg:

    cmake .. -DCMAKE_INSTALL_PREFIX=~/Library/Application\ Support/SuperCollider/Extensions

NOTE the tilde, it is omitted by SC Platform.userExtensionDir; command)

On Windows, Platform.userExtensionDir reports ```C:\Users\me\AppData\Local\SuperCollider\Extensions``` 
but to set it I had to rephrase with front slashes:

    cmake .. -DCMAKE_INSTALL_PREFIX=C:/Users/me/AppData/Local/SuperCollider/Extensions

CMAKE will report what happened

It's expected that the SuperCollider repo is cloned at `../supercollider` relative to this repo.  
Since we are 2 dirs deep with /squinewave/supercollider, probably your supercollider source is 2 dirs above build.  
So define **SC_PATH** for cmake:

    cmake .. -DSC_PATH=../../supercollider


### Developing

Use the command in `regenerate` to update CMakeLists.txt when you add or remove files from the
project. You don't need to run it if you only change the contents of existing files. You may need to
edit the command if you add, remove, or rename plugins, to match the new plugin paths. Run the
script with `--help` to see all available options.
