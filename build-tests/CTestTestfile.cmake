# CMake generated Testfile for 
# Source directory: G:/Carnage
# Build directory: G:/Carnage/build-tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[CarnageTests]=] "G:/Carnage/build-tests/CarnageTests.exe")
set_tests_properties([=[CarnageTests]=] PROPERTIES  _BACKTRACE_TRIPLES "G:/Carnage/CMakeLists.txt;108;add_test;G:/Carnage/CMakeLists.txt;0;")
subdirs("_deps/glfw-build")
subdirs("_deps/glm-build")
subdirs("_deps/glad-build")
subdirs("_deps/catch2-build")
