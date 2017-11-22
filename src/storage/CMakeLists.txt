# build service
set(SOURCE_FILES
    GlobalLock/MapBasedGlobalLockImpl.cpp
    SharedMutex/MapBasedSharedMutexImpl.cpp
)

add_library(Storage ${SOURCE_FILES})
target_link_libraries(Storage ${CMAKE_THREAD_LIBS_INIT})
