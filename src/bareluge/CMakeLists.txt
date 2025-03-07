# Include all .cpp files to the build target
file(GLOB_RECURSE bareluge_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_LIST_DIR}/*.cpp
)

target_sources(deluge PUBLIC ${bareluge_SOURCES})
