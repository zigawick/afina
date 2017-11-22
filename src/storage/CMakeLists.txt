# build service
set(SOURCE_FILES
    GlobalLock/MapBasedGlobalLockImpl.cpp
    SharedLock/MapBasedSharedMutexImpl.cpp
    SharedLock/shared_mutex.cpp
    Striped/MapBasedStripedImpl.cpp

)

add_library(Storage ${SOURCE_FILES})
target_link_libraries(Storage ${CMAKE_THREAD_LIBS_INIT})
