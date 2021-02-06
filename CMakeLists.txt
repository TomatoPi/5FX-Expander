cmake_minimum_required(VERSION 3.0.0)
project(5FX-Expander VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FindPkgConfig)

pkg_search_module(LSFZ liquidsfz)
if (LSFZ_FOUND)

  message(STATUS "liquidsfz found")
  message(STATUS "liquidsfz flags : ${LFSZ_CFLAGS}")
  message(STATUS "liquidsfz libraries : ${LSFZ_LDFLAGS}")
  add_compile_options(${LFSZ_CFLAGS})
  add_link_options(${LSFZ_LDFLAGS})

else()

  message(FATAL_ERROR "liquidsfz not found")

endif()

link_libraries(JackWrap)
link_libraries(jack)
link_libraries(lo)

set(CONFIG_FILES config/default.cfg)

add_executable(5FX-Expander 
  src/expander.cpp)
set_target_properties(5FX-Expander PROPERTIES RESSOURCE ${CONFIG_FILES})
install(TARGETS 5FX-Expander)
install(FILES ${CONFIG_FILES} DESTINATION ~/.5FX/5FX-Expander)
