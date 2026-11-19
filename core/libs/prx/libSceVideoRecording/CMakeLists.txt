# PS4 titles import the recording API from libSceVideoRecording; it is the API libSceVideoRecordingP implements.
add_library(libSceVideoRecording SHARED EXCLUDE_FROM_ALL ../libSceVideoRecordingP/Export.cpp)
target_include_directories(libSceVideoRecording PRIVATE ${LIBS_INCLUDE_DIR})
target_link_libraries(libSceVideoRecording PRIVATE libkernel_sys)
configure_windows_unwind(libSceVideoRecording)
