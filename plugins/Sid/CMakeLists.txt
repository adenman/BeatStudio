INCLUDE(BuildPlugin)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resid/resid/sid.cc")
	message(WARNING "reSID submodule not found, skipping SID plugin.")
	return()
endif()

if(NOT LMMS_HAVE_SID)
	return()
endif()

BUILD_PLUGIN(sid
	SidInstrument.cpp
	SidInstrument.h
	MOCFILES SidInstrument.h
	EMBEDDED_RESOURCES *.png)

add_subdirectory(resid)
target_link_libraries(sid resid)
