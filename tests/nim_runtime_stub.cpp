// nim_runtime_stub.cpp — Provides the two C globals that the Nim runtime
// embedded in libstorage.a expects when hosted by a non-Nim executable.
// Link this into any test executable that links libstorage.a but does not
// call libstorageNimMain() (i.e. does not actually start a storage node).
extern "C" {
    int    cmdCount = 0;
    char** cmdLine  = nullptr;
}
