# UNIX-only stuff

if(UNIX AND NOT ANDROID)
    
    # Not sure what this will do on Cygwin and Msys
    # Also, remember OS X X11 is a user installed option so it may not exist.
    find_package(X11)
    
    # Some Unicies need explicit linkage to the Math library or the build fails.
    find_library(MATH_LIBRARY m)
    
    # for ptheads in linux
    find_package(Threads REQUIRED)
    
endif(UNIX AND NOT ANDROID)
