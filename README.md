# amdados

Description goes here...

## Quickstart

Ensure you have GCC 5 installed and set as your default C/C++ compiler.
Furthermore CMake 3.5 (or later) is required for the build and testing process.
Simply execute the following commands to build the project and run all tests.

    $ mkdir build
    $ cd build
    $ cmake ../code
    $ make -j8
    $ ctest -j8

## Advanced Options

### Configuration

Following options can be supplied to CMake

| Option              | Values          |
| ------------------- | --------------- |
| -DCMAKE_BUILD_TYPE  | Release / Debug |
| -DBUILD_SHARED_LIBS | ON / OFF        |
| -DBUILD_TESTS       | ON / OFF        |
| -DBUILD_DOCS        | ON / OFF        |
| -DUSE_ASSERT        | ON / OFF        |
| -DUSE_VALGRIND      | ON / OFF        |
| -DUSE_ALLSCALECC    | ON / OFF        |
| -DENABLE_PROFILING  | ON / OFF        |
| -DTHIRD_PARTY_DIR   | \<path\>        |

The files `cmake/build_settings.cmake` and `code/CMakeLists.txt` state their
default value.

## Using the AllScale Compiler

To use the AllScale compiler, you first have to setup the required dependencies
in order to build it. A dependency installer is provided, running the following
commands should be sufficient on most systems. See
`scripts/dependencies/README.md` for more details.

    $ scripts/dependencies/installer
    $ scripts/dependencies/third_party_linker

To build this project using the AllScale compiler, simply set the corresponding
CMake option. You may want to use a separate build directory to easily switch
between GCC and AllScaleCC.

    $ mkdir build_with_allscalecc
    $ cd build_with_allscalecc
    $ cmake -DUSE_ALLSCALECC=ON ..
    $ make -j8
    $ ctest -j8

## Development

### Adding new Modules

The setup script can be run again to add new modules, just provide the same
project name.

    $ scripts/setup/run amdados frontend backend utils

### Adding new Parts to Modules

There is a utility script to add new *parts* to an existing module. The project
name and module name must be provided followed by a list of *parts* to
generate. Folders will be created along the way.

    $ scripts/setup/add_part amdados frontend sema extensions/malloc_extension

This will add the files `sema.h`, `sema.cpp` and `sema_test.cc` to the
*frontend* module. Furthermore new subfolders `extensions` will be created
containing `malloc_extension.h`, `malloc_extension.cpp` and
`malloc_extension_test.cc` in their respective subdirectories.

### Executable Bit

When working on Windows via SMB share, consider setting following Git setting.

    $ git config core.filemode false

### Licensor

A script, together with a Git hook, is provided to automatically add a license
header to each source file upon commit. See `scripts/license`.

### Eclipse Project

    $ cmake -G "Eclipse CDT4 - Unix Makefiles" /path/to/project

### Visual Studio Solution

    $ cmake -G "Visual Studio 14 Win64" -DBUILD_SHARED_LIBS=OFF Z:\path\to\project

Add path for third-party libraries when needed.

## Troubleshooting

### Getting GCC 5 / CMake 3.5 / Valgrind (for Testing)

The dependency installer can setup these required tools for you. Its README
(`scripts/dependencies/README.md`) holds the details.

It is preferred to use the operating system's package manager, if applicable.

### No Source Folder in Eclipse Project

Make sure your build folder is located outside the source folder. Eclipse is
not capable of dealing with such a setup correctly.

### Armadillo Library

The Armadillo matrix library is used only (!) for testing of matrix operations.
It is not included in the repository (see .gitignore).
Armadillo is installed along with other dependencies, see below.

### Dependecies

In order to compile the project on machine without Internet connection
(e.g. blade behind firewall), some prerequisites should be downloaded beforehand.
From the project root folder, one should execute:
    bash ./scripts/download.sh
This will install all necessary dependencies (including the Armadillo library)
into the folder "api".

### Python

Python-based prototype (./python) is a simplified version of the application
for testing and comparison against C++ implementation.

Prerequisites:
(1) Python3 + numpy + scipy + matplotlib.
(2) 'ffmpeg' utility for generating AVI files (installable from the standard
    Ubuntu 16.04 repository).
(3) optional 'mplayer' for playing AVI files, e.g.
        mplayer -nosound -x 512 -y 512 output/field.avi
    'mplayer' is also installable from the standard Ubuntu 16.04 repository.

In order to run Python prototype, standing in the project root directory:
        python3 ./python/Amdados2D.py

Results will appear in the "./output" folder, such as a sequence of fields,
AVI file where all the field are put together, etc.
Suppose the script had finished successfully, then user can run the following
commands to visualize the results (from the project root directory):

    # play the [t]rue density field AVI file:
        bash ./scripts/python/show_results.sh -t

    # play [b]oth fields - the true density field and data assimilation solution:
        bash ./scripts/python/show_results.sh -b

    # plot the relative [d]ifference between the true density field and
    # data assimilation solution:
        bash ./scripts/python/show_results.sh -d

Note, the output of the Python scrip depends on few state flags
defined at the beginning of Amdados2D.py script file (see the description therein):
    conf.generate_video
    conf.generate_text_output
    conf.generate_observations_only

There are also 1D and 2D Matlab implementations of the same algorithm as in
Amdados2D.py called Amdados1D.m and Amdados2D.m respectively. Matlab version
is faster and more readable. It can be run in terminal (from the project root
directory) as described at the beginning of each script. The script Amdados2D.m
produces only "./output/both_fields.avi".

