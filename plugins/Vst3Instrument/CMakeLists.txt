if(NOT LMMS_BUILD_WIN32)
	return()
endif()

include(BuildPlugin)
build_plugin(vst3instrument Vst3Instrument.cpp Vst3Instrument.h MOCFILES Vst3Instrument.h EMBEDDED_RESOURCES logo.svg)
target_link_libraries(vst3instrument vst3base)
