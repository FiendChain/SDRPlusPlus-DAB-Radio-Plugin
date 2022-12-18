## Introduction
SDR++ DAB radio plugin. 

NOTE: The release build was compiled against a nightly SDR++ build.

![Screenshot](docs/plugin_screenshot.png)

Core algorithms for DAB radio are used from https://github.com/FiendChain/DAB-Radio.

SDR++ core code was taken from https://github.com/AlexandreRouma/SDRPlusPlus/tree/master/core.

| Directory | Description |
| --- | --- |
| core | SDR++ core library used to link against for DLL module |
| dab | Core algorithms used for DAB radio decoding |
| dab_plugin | Plugin code to use dab algorithms as a SDR++ plugin |
| vendor | third party dependencies for dab code |
| cmake | Find*.cmake files for third party cmake targets |

## Download instructions
1. Download from releases page or build using the instructions below.

## Usage instructions
1. Paste <code>dab_plugin.dll</code> into <code>modules/</code> folder inside your SDR++ install.
2. Open SDR++.
3. Inside <code>Module Manager</code> tab on the left panel add <code>dab_decoder</code> as a plugin.
4. Browse to a valid DAB frequency using the following link: https://www.wohnort.org/dab/.
5. Check the OFDM/State tab to see if the OFDM demodulator is active.
6. Select the DAB/Channels tab and select one of the DAB+ channels.

## Build instructions
Built on Windows 10 with Visual Studio 2022.

1. Install Visual Studio 2022 with C++ build kit.
2. Install vcpkg and integrate install.
3. Open up the x64 C++ developer environment.
4. <code>fx cmake-conf</code> to configure cmake.
5. <code>fx build release build/ALL_BUILD.vcxproj</code> to build plugin.
6. Copy <code>dab_plugin.dll</code> from <code>build/dab_plugin/Release/dab_plugin.dll</code>.
7. Paste <code>dab_plugin.dll</code> into <code>modules/</code> folder inside your SDR++ install.

## TODO
- Improve the user interface so that you can view as much information as the original GUI found at https://github.com/FiendChain/DAB-Radio.
- Improve integration with SDR++.
- Add support for crossplatform build system.
    - DAB code uses intrinsics for x86 architecture which needs to be made multiplatform

