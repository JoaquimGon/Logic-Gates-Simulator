# Minimum required version of CMake
cmake_minimum_required (VERSION 3.10)

# Enable Hot Reload for MSVC compilers if supported.
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("Logic-Gates-Simulator")

if(MSVC)
    add_compile_options(/permissive-)                                       # Enforce modern C++ standarts
    string(REGEX REPLACE "/W3" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")  # Shows all warnings
endif()

# Add source to this project's executable.
add_executable (Logic-Gates-Simulator 
    "src/main.cpp" 
    "src/main.h"
    "src/gate.cpp"
    "src/gate.h" 
    "src/circuit.cpp" 
    "src/circuit.h")

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET Logic-Gates-Simulator PROPERTY CXX_STANDARD 20)
endif()